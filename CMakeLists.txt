cmake_minimum_required(VERSION 3.18)
project(MemoryPool LANGUAGES CXX)

if (${CMAKE_VERSION} VERSION_GREATER "3.24")
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add tests directory
enable_testing()
add_subdirectory(tests)