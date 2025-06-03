cmake_minimum_required(VERSION 3.18)
project(MemoryPool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export include directories
add_library(pool_allocator INTERFACE)
target_include_directories(pool_allocator
    INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pool_allocator INTERFACE -O2)
    target_link_options(pool_allocator INTERFACE)
endif()

# Add tests directory
enable_testing()
add_subdirectory(tests)
