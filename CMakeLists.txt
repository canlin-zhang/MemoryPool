cmake_minimum_required(VERSION 3.18)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include (FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    URL_HASH SHA256=edd885a1ab32b6999515a880f669efadb80b3f880215f315985fa3f6eca7c4d3
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
