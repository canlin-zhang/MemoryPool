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

- **Warnings**: GCC/Clang: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`; MSVC: `/W4 /WX`
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

`PoolAllocator<T, BlockSize, Mode>` is a header-only template class providing an STL-compliant memory pool. Internal classes live in `pool_allocator_detail` namespace.

### Class hierarchy (layered composition)

```
PoolAllocator<T, BlockSize, Mode>    ← public API, STL allocator interface
├── ObjectOpsMixin<PoolAllocator, T> ← CRTP: construct/destroy/new_object/delete_object/make_unique
├── Backend = backend_for<Mode, T>   ← the ONE enum→backend-type decode: SlotVector<T> | SlotList<T>
└── ComboAlloc = FreeSlotStore<T, BlockStore, Backend>
    ├── free_store: Backend          ← freed slots reused before bumping
    └── BlockStore<T, Alloc, BlockSize, Backend>
        ├── BumpBlock<pointer>       ← bump pointer within current block
        └── blocks: Backend          ← blocks held for destruction
```

**Policy-on-type composition.** `backend_for<Mode, T>` is the single point that maps the public `Mode` enum to a concrete backend type. `BlockStore`/`FreeSlotStore` are parameterized on that backend *type* and never branch on the mode — no `use_list`, no `if constexpr`. Each backend carries its own traits (`slots_per_block_overhead`, `nothrow_transfer`) that the stores read off the type.

1. **`BumpBlock<Pointer>`** — lightweight bump-pointer tracker. `init()`, `allocate_one()`, `take_range()`.
2. **`SlotList<T>` / `SlotVector<T>`** — the two store backends, one shared interface (`reserve`/`push`/`pop`/`size`/`empty`/`splice`) plus the two `static constexpr` traits above. `SlotList` is a singly-linked list whose link lives inline in each slot's bytes (no nodes → `splice` allocates nothing → noexcept; `slots_per_block_overhead=1`, `nothrow_transfer=true`); `SlotVector` wraps `std::vector<T*>` (`push`/`splice` may throw; `slots_per_block_overhead=0`, `nothrow_transfer=false`). `SlotList` requires `sizeof(T) >= sizeof(T*)`. See `docs/adr/0001-slotlist-inline-links.md`.
3. **`BlockStore<T, Alloc, BlockSize, Blocks>`** — owns blocks; bumps within the current block. Carves the bump range past `Blocks::slots_per_block_overhead` (so `SlotList` reserves slot 0 for its chain link → `BlockSize/sizeof(T) - 1` usable/block; `SlotVector` uses all slots). `allocate()` reserves the block-chain slot *before* acquiring the raw block, so a `SlotVector` grow can't leak it (strong guarantee). `take_bump_range()` drains the current block; `transfer_blocks()` splices block chains.
4. **`FreeSlotStore<T, Alloc, FreeStore>`** — free-slot recycling over a parent (the `BlockStore`). `allocate()` tries the free store first, else the parent. Orchestrates `transfer_all` = drain source bump → splice free slots → splice blocks.
5. **`ObjectOpsMixin<Derived, T>`** — CRTP mixin: `construct`, `destroy`, `new_object`, `delete_object`, `make_unique_with`.
6. **`PoolAllocator<T, BlockSize, Mode>`** — public class. Decodes `Mode` to a backend via `backend_for` once, then composes `FreeSlotStore<BlockStore>` on it. `transfer_free`/`transfer_all` guarded by a per-instance `std::atomic<bool>` spinlock (`SpinLockGuard`, destination-only; noexcept lock so `Noexcept`-mode transfer can't throw). Debug metrics: `allocated_bytes()`, `num_slots_available()`, `num_bump_available()`.

### Transfer mode policy

- `TransferMode Mode` is the 3rd (defaulted) template param: `Fast` (default) or `Noexcept`.
- **`Fast`** — vector-backed stores; `transfer_free`/`transfer_all` may throw `bad_alloc`. Historical behavior; fastest hot path.
- **`Noexcept`** — list-backed stores; `transfer_free`/`transfer_all` are `noexcept` (they allocate nothing). Requires `sizeof(T) >= sizeof(T*)`. Use when transfer runs where a throw is fatal (e.g. inside a destructor). Costs ~2.4x on the alloc/dealloc hot path.

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
└── pool_allocator.tcc    # Template implementations (BumpBlock, SlotList, BlockStore, FreeSlotStore,
                          #   ObjectOpsMixin, PoolAllocator transfer/make_unique/max_size)
tests/
├── basic_type_test.cpp            # int, double, char allocation + construct + throwing-ctor cleanup
├── complex_type_test.cpp          # std::string, std::vector, std::map, aligned structs
├── slot_list_test.cpp             # SlotList primitive: push/pop/splice/move in isolation
├── transfer_test.cpp              # transfer_all/free; randomized model (Fast + Noexcept)
├── noexcept_transfer_test.cpp     # TransferMode::Noexcept: reuse, block accounting, noexcept transfer
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
