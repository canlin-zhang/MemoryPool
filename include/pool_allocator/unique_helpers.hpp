#pragma once

#include "pool_allocator.h"
#include "pool_deleter.hpp"
#include <memory>

// Make unique function
template <typename T, size_t BlockSize, typename... Args>
std::unique_ptr<T, PoolDeleter<T, BlockSize>>
pool_make_unique(PoolAllocator<T, BlockSize>& allocator, Args&&... args)
{
    T* ptr = allocator.allocate(1); // Allocate memory for one object
    // Exception safety handling
    try
    {
        new (ptr) T(std::forward<Args>(args)...); // Construct the object in the allocated memory
    }
    catch (...)
    {
        allocator.deallocate(ptr, 1); // Deallocate memory if construction fails
        throw;                        // Re-throw the exception
    }

    return std::unique_ptr<T, PoolDeleter<T, BlockSize>>(ptr,
                                                         PoolDeleter<T, BlockSize>{&allocator});
}
