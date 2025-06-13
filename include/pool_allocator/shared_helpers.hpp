#pragma once

#include "pool_allocator.h"
#include <memory>

// Construct shared pointer with custom deleter using pool allocator
template <typename Allocator, typename... Args>
std::shared_ptr<typename Allocator::value_type> pool_make_shared(Allocator& allocator,
                                                                 Args&&... args)
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

    // Custom deleter for shared pointer
    auto deleter = [allocator_ptr = &allocator](T* p)
    {
        p->~T();                         // Call the destructor
        allocator_ptr->deallocate(p, 1); // Deallocate memory
    };

    return std::shared_ptr<T>(ptr, deleter); // Return shared pointer with custom deleter
}
