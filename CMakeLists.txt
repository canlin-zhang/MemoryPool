cmake_minimum_required(VERSION 3.18)
project(MemoryPool LANGUAGES CXX)

# if (${CMAKE_VERSION} VERSION_GREATER "3.24")
#     cmake_policy(SET CMP0135 NEW)
# endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# # Export compile commands
# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Export include directories
add_library(pool_allocator INTERFACE)
target_include_directories(pool_allocator
    INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pool_allocator INTERFACE "$<$<CONFIG:Debug>:-fsanitize=address,undefined>")
    target_link_options(pool_allocator INTERFACE "$<$<CONFIG:Debug>:-fsanitize=address,undefined>")
endif()

# Add tests directory
enable_testing()
add_subdirectory(tests)