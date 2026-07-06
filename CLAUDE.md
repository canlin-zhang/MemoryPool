# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

## Build & Test Commands

```bash
# Configure (with tests)
cmake -S . -B build -DENABLE_TESTS=ON

# Configure with sanitizers
cmake -S . -B build -DENABLE_TESTS=ON -DENABLE_ASAN=ON -DENABLE_UBSAN=ON

# Configure with coverage
cmake -S . -B build -DENABLE_TESTS=ON -DENABLE_COVERAGE=ON

# Build
cmake --build build

# Run tests
cd build && ctest --output-on-failure

# Run a single test
cd build && ctest -R <test_name> --output-on-failure
```

## Code Style Summary

- **Standard**: C++17 (`CMAKE_CXX_STANDARD 17`)
- **Based on**: LLVM style with Allman braces (`.clang-format`)
- **Indentation**: 4 spaces, no tabs (also in `.editorconfig`)
- **Column limit**: 100 characters
- **Include guards**: `#pragma once`
- **Pointer alignment**: left (`int* p`)
- **Attributes**: `[[nodiscard]]` on functions returning allocated pointers; `noexcept` on non-throwing functions
- **Comments**: `//!` for Doxygen-style class/function headlines; `//` for inline notes
- **Naming**: `PascalCase` for classes/templates, `snake_case` for variables and member functions (STL convention)
- **Format before committing**: `clang-format -i include/pool_allocator/*.h include/pool_allocator/*.tcc`

## Architecture

`PoolAllocator<T, BlockSize>` is a header-only template class providing an STL-compliant memory pool. Internal classes live in `pool_allocator_detail` namespace.

### Class hierarchy (layered composition)

```
PoolAllocator<T, BlockSize>          ← public API, STL allocator interface
├── ObjectOpsMixin<PoolAllocator, T> ← CRTP: construct/destroy/new_object/delete_object/make_unique
└── ComboAlloc = StackAllocator<T, BumpAlloc>
    ├── free_slots (std::vector<T*>)     ← freed slots reused before bumping
    └── BumpAllocator<T, Alloc, BlockSize>
        ├── BumpBlock<pointer>           ← bump pointer within current block
        └── blocks (std::vector<T*>)     ← allocated blocks for destruction
```

1. **`pool_allocator_detail::BumpBlock<Pointer>`** — lightweight bump-pointer tracker. `init()`, `allocate_one()`, `export_remaining()`.
2. **`pool_allocator_detail::BumpAllocator<T, Alloc, BlockSize>`** — owns allocated blocks; bumps within the current block. Does NOT handle individual deallocations. `export_all()` returns remaining bump slots + blocks. `import_blocks()` takes ownership of blocks from another allocator.
3. **`pool_allocator_detail::StackAllocator<T, Alloc>`** — wraps a parent allocator with a free-list stack. `allocate()` tries free slots first, then falls through to parent. `transfer_free()` / `transfer_all()` move slots/blocks.
4. **`pool_allocator_detail::ObjectOpsMixin<Derived, T>`** — CRTP mixin providing `construct`, `destroy`, `new_object` (with perfect forwarding), `delete_object`, and `make_unique_with`.
5. **`PoolAllocator<T, BlockSize>`** — the public class. Composes `StackAllocator<BumpAllocator>`. Provides `transfer_free`/`transfer_all` with a per-instance `std::mutex` (locks destination only). Exposes debug metrics: `allocated_bytes()`, `num_slots_available()`, `num_bump_available()`.

### Transfer semantics

- `transfer_free(from)` — moves free slots from `from` into `this`. `from` keeps its blocks.
- `transfer_all(from)` — moves free slots AND all blocks from `from` into `this`. `from` is left empty.
- Thread safety: only the destination (`this`) is locked. The caller must own `from`'s thread.

### Key design decisions

- Move construction/assignment are **deleted** on `PoolAllocator` — use `transfer_all` instead
- `allocate(n)` with `n > 1` falls through to the parent `std::allocator`
- Blocks are never freed until the allocator is destroyed (memory is reused but not released)
- Incomplete/forward-declared types are supported for `make_unique` and `new_object`

## File Map

```
include/pool_allocator/
├── pool_allocator.h      # Class declarations, PoolAllocator, inline methods, operators
└── pool_allocator.tcc    # Template implementations (BumpBlock, BumpAllocator, StackAllocator,
                          #   ObjectOpsMixin, PoolAllocator transfer/make_unique/max_size)
tests/
├── basic_type_test.cpp            # int, double, char allocation + construct
├── complex_type_test.cpp          # std::string, std::vector, std::map, aligned structs
├── transfer_test.cpp              # transfer_all, transfer_free, randomized model validation
├── incomplete_struct_test.cpp     # Forward-declaration compile-time test
├── performance_test_random.cpp    # Benchmark: PoolAllocator vs std::allocator vs StackAllocator
├── StackAlloc.h                   # Example: PoolAllocator as STL allocator backing a linked stack
└── CMakeLists.txt                 # FetchContent(googletest), glob tests, gtest_discover_tests
cmake/
└── MemoryPoolConfig.cmake         # Installed CMake package config
.github/workflows/
├── cmake-multi-platform.yml       # CI: Ubuntu (gcc, clang) + Windows (MSVC), ASAN+UBSAN
└── coverage.yml                   # CI: lcov → HTML report artifact
```

## CI

- PRs and pushes to `master` trigger multi-platform build+test with ASAN and UBSAN
- Coverage workflow uploads HTML report as a build artifact
- Both workflows ignore `**.md` changes via `paths-ignore`

## Sanitizer Options

Available CMake options for debug builds:

| Option | Flag | Effect |
|--------|------|--------|
| `ENABLE_UBSAN` | `-fsanitize=undefined -fno-sanitize-recover=all` | Undefined behavior sanitizer with extra warnings |
| `ENABLE_ASAN` | `-fsanitize=address -fno-omit-frame-pointer` | Address sanitizer |
| `ENABLE_COVERAGE` | `--coverage` | Code coverage instrumentation (gcc/clang only) |
