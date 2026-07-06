# Contributing to MemoryPool

Thanks for your interest in contributing! This document outlines the conventions and workflow for contributing to this project.

## Quick Start

```bash
git clone https://github.com/canlin-zhang/MemoryPool.git
cd MemoryPool
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure
```

## Code Style

This project uses [clang-format](https://clang.llvm.org/docs/ClangFormat.html) to enforce consistent formatting. The configuration is in `.clang-format` at the repo root.

Key style points:

- **C++ Standard**: C++17
- **Based on**: LLVM style with modifications
- **Braces**: Allman style (braces on their own line)
- **Indentation**: 4 spaces, no tabs
- **Column limit**: 100 characters
- **Include guards**: `#pragma once`
- **Pointer alignment**: left (`int* p`)
- **Comments**: Doxygen-style `//!` for class/function documentation headline, `///` for details, `//` for inline notes
- **Attributes**: `[[nodiscard]]` on functions returning allocated pointers; `noexcept` on non-throwing functions
- **Naming**: `PascalCase` for classes, `snake_case` for variables, `snake_case` for member functions (following the STL convention)

Run clang-format before submitting:

```bash
clang-format -i include/pool_allocator/*.h include/pool_allocator/*.tcc
```

EditorConfig (`.editorconfig`) and clang-format should handle most formatting automatically in supported editors.

## Pull Request Guidelines

- Keep PRs **small and focused** — one logical change per PR
- **Run tests locally** before opening a PR (see below)
- Link your PR to an existing issue, or open one first for discussion
- Use **merge commits** (not squash or rebase) when merging
- Write descriptive commit messages explaining *what* and *why*

### Before submitting

```bash
# Build with sanitizers enabled (catches undefined behavior and memory errors)
cmake -S . -B build -DENABLE_TESTS=ON -DENABLE_ASAN=ON -DENABLE_UBSAN=ON
cmake --build build
cd build && ctest --output-on-failure
```

## Continuous Integration

All PRs and pushes to `master` trigger CI via GitHub Actions:

| Workflow | What it does |
|----------|-------------|
| **CMake on multiple platforms** | Builds and runs tests on Ubuntu (gcc, clang) and Windows (MSVC) with ASAN + UBSAN |
| **Coverage** | Builds with coverage instrumentation, runs tests, uploads report as artifact |

CI must pass before merging.

## Project Structure

```
MemoryPool/
├── include/pool_allocator/
│   ├── pool_allocator.h      # Public header (class declarations + inline methods)
│   └── pool_allocator.tcc    # Template implementation (included by .h)
├── tests/                    # GoogleTest test suite
│   ├── basic_type_test.cpp   # Fundamental type allocations
│   ├── complex_type_test.cpp # STL container and aligned type allocations
│   ├── transfer_test.cpp     # transfer_all / transfer_free + randomized model testing
│   ├── incomplete_struct_test.cpp  # Forward-declaration compile test
│   ├── performance_test_random.cpp # Benchmark vs std::allocator
│   └── StackAlloc.h          # Example: using PoolAllocator as an STL allocator
├── cmake/                    # CMake package config for install
├── .github/workflows/        # CI definitions
├── .clang-format             # Code formatting rules
├── .editorconfig             # Editor settings
└── CMakeLists.txt            # Build definition
```

## Architecture

`PoolAllocator<T, BlockSize>` is the public class. Internally it composes three layers:

1. **`BumpAllocator`** — allocates large blocks from the heap and bumps a pointer for fast sequential allocation. Does not handle individual deallocations.
2. **`StackAllocator`** — wraps `BumpAllocator` with a free-list stack so deallocated slots are reused before bumping again.
3. **`ObjectOpsMixin`** (CRTP) — provides `construct`/`destroy`/`new_object`/`delete_object`/`make_unique` on top of any allocator implementing `allocate`/`deallocate`.

The `transfer_free` and `transfer_all` methods move memory between allocators and are protected by a per-allocator `std::mutex` on the destination side.

## Testing

- Tests use [GoogleTest](https://github.com/google/googletest) (fetched automatically by CMake)
- `ctest` discovers and runs all test executables
- The randomized transfer test validates allocator state against a mathematical model over 1000 random operations
- Performance tests compare against `std::allocator` as a baseline

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details. By contributing, you agree that your contributions will be licensed under the same terms.
