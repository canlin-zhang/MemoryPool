#pragma once

#include "pool_allocator.h"
#include "pool_deleter.hpp"
#include <memory>

// Make unique function
template <typename Allocator, typename... Args>
std::unique_ptr<typename Allocator::value_type, PoolDeleter<Allocator>>
pool_make_unique(Allocator& allocator, Args&&... args)
{
    using T = typename Allocator::value_type;
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

    return std::unique_ptr<T, PoolDeleter<Allocator>>(ptr, PoolDeleter<Allocator>{&allocator});
}
