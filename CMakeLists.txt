cmake_minimum_required(VERSION 3.18)
project(MemoryPool LANGUAGES CXX)

if (${CMAKE_VERSION} VERSION_GREATER "3.24")
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Export compile commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Export include directories
add_library(pool_allocator INTERFACE)
target_include_directories(pool_allocator
    INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Add tests directory
enable_testing()
add_subdirectory(tests)