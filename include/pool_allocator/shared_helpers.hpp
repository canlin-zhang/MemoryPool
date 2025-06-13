#pragma once

#include "pool_allocator.h"
#include <memory>

template <typename T, size_t BlockSize, typename... Args>
std::shared_ptr<T> pool_make_shared(PoolAllocator<T, BlockSize>& allocator, Args&&... args)
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

    auto deleter = [&allocator](T* ptr)
    {
        if (ptr)
        {
            ptr->~T();                    // Call the destructor
            allocator.deallocate(ptr, 1); // Deallocate memory
        }
    };

    return std::shared_ptr<T>(ptr, deleter); // Return a shared_ptr with custom deleter
}
