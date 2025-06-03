/* Original copyright info */
/*-
 * Copyright (c) 2013 Cosku Acay, http://www.coskuacay.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* Modified by Canlin Zhang
 * Changes:
 * 1. Added incomplete struct/class support (forward declaration)
 * 2. Changed block and slot tracking to use std::stack backed by std::vector
 * 3. Added unique_ptr its necessary helper functions
 */

#include "pool_allocator.h"

#ifndef POOL_ALLOCATOR_TCC
#define POOL_ALLOCATOR_TCC

// Default constructor
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize>::PoolAllocator() noexcept
{
}

// Copy constructor
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize>::PoolAllocator(const PoolAllocator &other) noexcept
{
    // Nothing should be done here
}

// Move constructor
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize>::PoolAllocator(PoolAllocator &&other) noexcept
{
    // Move available blocks and slots from the other allocator
    available_slots = std::move(other.available_slots);
    available_blocks = std::move(other.available_blocks);
    // Clear the other allocator's stacks
    while (!other.available_slots.empty())
    {
        other.available_slots.pop();
    }
    while (!other.available_blocks.empty())
    {
        other.available_blocks.pop();
    }
}

// Templated copy
template <typename T, size_t BlockSize>
template <class U>
PoolAllocator<T, BlockSize>::PoolAllocator(const PoolAllocator<U, BlockSize> &other) noexcept
{
    // Nothing should be done here
}

// Destructor
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize>::~PoolAllocator() noexcept
{

    // Deallocate all blocks, clear slots
    while (!available_blocks.empty())
    {
        pointer block = available_blocks.top();
        available_blocks.pop();
        std::allocator<T>().deallocate(block, BlockSize / sizeof(T));
    }
}

// Move assignment operator
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize> &
PoolAllocator<T, BlockSize>::operator=(PoolAllocator &&other) noexcept
{
    // Move available blocks and slots from the other allocator
    available_slots = std::move(other.available_slots);
    available_blocks = std::move(other.available_blocks);
    // Clear the other allocator's stacks
    while (!other.available_slots.empty())
    {
        other.available_slots.pop();
    }
    while (!other.available_blocks.empty())
    {
        other.available_blocks.pop();
    }

    return *this;
}

// Address functions
template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::pointer
PoolAllocator<T, BlockSize>::addressof(reference x) const noexcept
{
    return std::addressof(x);
}

template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::const_pointer
PoolAllocator<T, BlockSize>::addressof(const_reference x) const noexcept
{
    return std::addressof(x);
}

// Allocate a memory block
template <typename T, size_t BlockSize>
void PoolAllocator<T, BlockSize>::allocateBlock()
{
    // Calculate number of objects in a block
    size_type numObjects = BlockSize / sizeof(T);
    if (numObjects < 1)
    {
        throw std::bad_alloc();
    }
    else
    {
        // Allocate a new block of memory
        pointer block = std::allocator<T>().allocate(numObjects);
        // Push the block onto the stack of available blocks
        available_blocks.push(block);
        // Push all slots in the block onto the stack of available slots
        for (size_type i = 0; i < numObjects; ++i)
        {
            available_slots.push(block + i);
        }
    }
}

// Allocate a single object
template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::pointer
PoolAllocator<T, BlockSize>::allocate(size_type n)
{
    // Do nothing if n is 0
    if (n == 0)
    {
        return nullptr;
    }
    // For multiple objects, we revert to std::allocator
    else if (n > 1)
    {
        return std::allocator<T>().allocate(n);
    }
    // Handle single object allocation
    else
    {
        // If there are no available slots, allocate a new block
        if (available_slots.empty())
        {
            allocateBlock();
        }
        // Get the pointer to the next available slot
        // Sanity check
        assert(!available_slots.empty());
        // Get last available slot
        pointer p = available_slots.top();
        available_slots.pop();
        return p;
    }
}

// Deallocate a single object
template <typename T, size_t BlockSize>
void PoolAllocator<T, BlockSize>::deallocate(pointer p, size_type n)
{
    // Do nothing if n is 0
    if (n == 0)
    {
        return;
    }
    // For multiple objects, we revert to std::allocator
    else if (n > 1)
    {
        std::allocator<T>().deallocate(p, n);
    }
    // Handle single object deallocation
    else
    {
        // Push the pointer back onto the stack of available slots
        available_slots.push(p);
        // We don't touch the blocks here
        // Because they are managed separately
    }
}

// Construct an object in the allocated memory
template <typename T, size_t BlockSize>
template <class U, class... Args>
void PoolAllocator<T, BlockSize>::construct(U *p, Args &&...args)
{
    // Use placement new to construct the object in the allocated memory
    new (p) U(std::forward<Args>(args)...);
}
// Destroy an object in the allocated memory
template <typename T, size_t BlockSize>
template <class U>
void PoolAllocator<T, BlockSize>::destroy(U *p)
{
    // Call the destructor of the object
    p->~U();
}

// Maximum size of the pool
template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::size_type
PoolAllocator<T, BlockSize>::max_size() const noexcept
{
    // Calculate the maximum number of objects that can be allocated in a block
    return size_type(-1) / sizeof(T);
}

// Unique pointer support
// Deleter for unique_ptr
template <typename T, size_t BlockSize>
template <typename U>
void PoolAllocator<T, BlockSize>::Deleter::operator()(U *ptr) const noexcept
{
    static_assert(sizeof(U) > 0, "Deleter cannot be used with incomplete types");
    // Call delete_object on the allocator
    if (allocator)
    {
        allocator->delete_object(ptr);
    }
    else
    {
        // If allocator is null, we cannot delete the object
        // This should not happen in normal usage
        throw std::runtime_error("Allocator is null in Deleter");
    }
}

// Create a unique pointer with a custom deleter
template <typename T, size_t BlockSize>
template <class... Args>
inline std::unique_ptr<T, typename PoolAllocator<T, BlockSize>::Deleter>
PoolAllocator<T, BlockSize>::make_unique(Args &&...args)
{
    pointer raw = allocate(1);
    construct(raw, std::forward<Args>(args)...);
    return std::unique_ptr<T, Deleter>(raw, Deleter{this});
}

// Create a new object in the pool
// Default constructor
template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::pointer
PoolAllocator<T, BlockSize>::new_object()
{
    // Allocate a single object
    pointer p = allocate(1);
    // Construct the object in the allocated memory
    construct(p);
    return p;
}

// Create a new object in the pool with arguments
template <typename T, size_t BlockSize>
template <class... Args>
typename PoolAllocator<T, BlockSize>::pointer
PoolAllocator<T, BlockSize>::new_object(Args &&...args)
{
    // Allocate a single object
    pointer p = allocate(1);
    // Construct the object in the allocated memory with arguments
    construct(p, std::forward<Args>(args)...);
    return p;
}

// Delete an object in the pool
template <typename T, size_t BlockSize>
void PoolAllocator<T, BlockSize>::delete_object(pointer p)
{
    // Call the destructor of the object
    destroy(p);
    // Deallocate the object
    deallocate(p, 1);
}

#endif // POOL_ALLOCATOR_TCC