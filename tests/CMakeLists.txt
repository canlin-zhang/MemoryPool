# Download and link GoogleTest
include(FetchContent)
FetchContent_declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    URL_HASH SHA256=edd885a1ab32b6999515a880f669efadb80b3f880215f315985fa3f6eca7c4d3
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Glob all test source files
file(GLOB TEST_SOURCES "*.cpp")

foreach(test_src ${TEST_SOURCES})
    # Get name without extension
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src})

    target_include_directories(${test_name}
        PRIVATE
        ${CMAKE_SOURCE_DIR}  # For MemoryPool.h and .tcc
    )

    target_link_libraries(${test_name}
        PRIVATE
        GTest::gtest_main
        pool_allocator
    )

    include(GoogleTest)
    gtest_discover_tests(${test_name})
endforeach()