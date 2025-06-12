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

# ASAN options
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
if(ENABLE_ASAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(
            -fsanitize=address
            -fno-omit-frame-pointer
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow)
        add_link_options(
            -fsanitize=address)
    else()
        message(WARNING "ASAN is only supported with GCC and Clang.")
    endif()
endif()

# Enable code coverage reporting
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)
if(ENABLE_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O0 -g --coverage)
    add_link_options(--coverage)
  endif()
endif()

# Export include directories
add_library(pool_allocator INTERFACE)
target_include_directories(pool_allocator
    INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pool_allocator INTERFACE)
    target_link_options(pool_allocator INTERFACE)
endif()

# Install rules
install(DIRECTORY include/ DESTINATION include)
install(TARGETS pool_allocator EXPORT MemoryPoolTargets)
install(EXPORT MemoryPoolTargets
    FILE MemoryPoolTargets.cmake
    NAMESPACE MemoryPool::
    DESTINATION cmake
)
install(FILES cmake/MemoryPoolConfig.cmake DESTINATION cmake)

# Coverage target
if(ENABLE_COVERAGE)
  find_program(LCOV_EXEC lcov)
  find_program(GENHTML_EXEC genhtml)
  if(LCOV_EXEC AND GENHTML_EXEC)
    add_custom_target(coverage
      COMMAND ${CMAKE_COMMAND} --build . --target all
      COMMAND ctest --output-on-failure
      COMMAND ${LCOV_EXEC} --capture --directory ${CMAKE_BINARY_DIR} --output-file coverage.info --ignore-errors gcov,gcov
      COMMAND ${LCOV_EXEC} --remove coverage.info '/usr/*' --output-file coverage.filtered.info
      COMMAND ${GENHTML_EXEC} coverage.filtered.info --output-directory coverage-report
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Generating code coverage report"
      VERBATIM
    )
  else()
    message(WARNING "lcov or genhtml not found - coverage target unavailable")
  endif()
endif()

# Add tests directory
option(ENABLE_TESTS "Enable unit tests" OFF)
if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
