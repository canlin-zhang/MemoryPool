cmake_minimum_required(VERSION 3.18)
project(MemoryPool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# UBSAN options
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
if(ENABLE_UBSAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(
            -fsanitize=undefined
            -fno-sanitize-recover=all
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow)
        add_link_options(
            -fsanitize=undefined)
    else()
        message(WARNING "UBSAN is only supported with GCC and Clang.")
    endif()
endif()

# Export include directories
add_library(pool_allocator INTERFACE)
target_include_directories(pool_allocator
    INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pool_allocator INTERFACE)
    target_link_options(pool_allocator INTERFACE)
endif()

# Add tests directory
option(ENABLE_TESTS "Enable unit tests" OFF)
if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
