#pragma once

#include <utility>

// Allocate and construct a new object using allocator
template <typename T, typename Allocator, typename... Args>
T* new_object(Allocator& allocator, Args&&... args)
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
    return ptr; // Return the pointer to the constructed object
}

// Delete object using allocator
template <typename T, typename Allocator>
void delete_object(Allocator& allocator, T* ptr)
{
    if (ptr)
    {
        ptr->~T();                    // Call the destructor
        allocator.deallocate(ptr, 1); // Deallocate memory
    }
}
