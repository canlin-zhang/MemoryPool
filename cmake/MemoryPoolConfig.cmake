@PACKAGE_INIT@

include(CMakeFindDependencyMacro)

set_and_check(MemoryPool_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../include")

add_library(MemoryPool::pool_allocator INTERFACE IMPORTED)
set_target_properties(MemoryPool::pool_allocator PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${MemoryPool_INCLUDE_DIRS}"
)

# Optionally, export version
set(MemoryPool_VERSION "1.0.0")
