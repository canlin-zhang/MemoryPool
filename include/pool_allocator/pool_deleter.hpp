#pragma once

template <typename Allocator>
struct PoolDeleter
{
    using T = typename Allocator::value_type; // Type of the value in the allocator
    Allocator* allocator = nullptr;           // Pointer to the allocator

    // Deleter function
    void operator()(typename Allocator::pointer ptr) const
    {
        if (ptr)
        {
            ptr->~T();                     // Call the destructor
            allocator->deallocate(ptr, 1); // Deallocate memory
        }
    }
};
