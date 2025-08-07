#pragma once

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

#include <limits>

// Default constructor
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize>::PoolAllocator() noexcept
{
    // Default constructor initializes an valid, empty allocator
    valid = true;
    current_block_slot = 0; // No current block slot
}

// Destructor
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize>::~PoolAllocator() noexcept
{
    // If the allocator is not valid, do nothing
    // This is because we guarantee that an invalid allocator:
    // 1. Cannot allocate or deallocate memory
    // 2. Cannot construct or destroy objects
    // 3. Cannot export or import memory blocks
    // 4. Does not have any free slots or memory blocks
    if (!valid)
    {
        return;
    }

    // Invalidate the allocator
    valid = false;

    // Clear the free slots stack
    while (!free_slots.empty())
    {
        free_slots.pop();
    }

    // Reset the current block slot
    current_block_slot = 0;

    // Free all memory blocks
    for (pointer block : memory_blocks)
    {
        ::operator delete(block, std::align_val_t(alignof(T)));
    }
}

// Address functions
template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::pointer
PoolAllocator<T, BlockSize>::addressof(reference x) const noexcept
{
    assert_valid();

    return std::addressof(x);
}

template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::const_pointer
PoolAllocator<T, BlockSize>::addressof(const_reference x) const noexcept
{
    assert_valid();

    return std::addressof(x);
}

template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::allocateBlock()
{
    // Calculate item alignment
    constexpr size_type num_items = BlockSize / sizeof(T);

    if (num_items < 1)
    {
        throw std::bad_alloc();
    }

    // Allocate a new block of memory
    pointer new_block = reinterpret_cast<pointer>(
        ::operator new(BlockSize, std::align_val_t(alignof(T))));

    // Push the new block to the free blocks stack
    memory_blocks.emplace_back(new_block);

    // Reset block counter
    current_block_slot = 0;
}

// Allocate a single object
template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::pointer
PoolAllocator<T, BlockSize>::allocate(size_type n)
{
    assert_valid();

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
        constexpr size_type num_items = BlockSize / sizeof(T);

        // Check free slots first
        if (!free_slots.empty())
        {
            // Pop the top slot from the free slots stack
            pointer p = free_slots.top();
            free_slots.pop();
            return p;
        }
        // Check current block slot
        else if (!memory_blocks.empty() && current_block_slot < num_items)
        {
            // Increment by 1
            pointer p = memory_blocks.back() + current_block_slot;
            current_block_slot++;
            return p;
        }
        // Allocate a new block
        else
        {
            // Allocate a new block of memory
            allocateBlock();
            pointer p = memory_blocks.back();
            // Reset current block slot
            current_block_slot++; // Start from the first slot
            // Return the first slot in the new block
            return p;
        }
    }
}

// Deallocate a single object
template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::deallocate(pointer p, size_type n)
{
    assert_valid();

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
    // We push it back to the available slots
    else
    {
        // Push pointer back to free slots stack
        if (p != nullptr)
        {
            free_slots.push(p);
        }
    }
}

template <typename T, size_t BlockSize>
inline std::vector<typename PoolAllocator<T, BlockSize>::pointer>
PoolAllocator<T, BlockSize>::export_pool()
{
    assert_valid();

    // Invalidate the pool
    valid = false;

    // Pop everything off the free slots stack
    while (!free_slots.empty())
    {
        free_slots.pop();
    }
    // Reset the current block slot
    current_block_slot = 0;

    // Return empty vector if no blocks are available
    if (memory_blocks.empty())
    {
        return std::vector<pointer>();
    }
    // Return a vector of pointers to the memory blocks
    else
    {
        std::vector<pointer> exported_blocks;
        exported_blocks.reserve(memory_blocks.size());

        // Move the blocks to the exported vector
        for (pointer block : memory_blocks)
        {
            exported_blocks.emplace_back(block);
        }

        // Clear the memory blocks
        memory_blocks.clear();

        return exported_blocks;
    }
}

template <typename T, size_t BlockSize>
inline void
PoolAllocator<T, BlockSize>::import_pool(const std::vector<pointer>& blocks)
{
    // An invalid allocator can import a pool (reviving from a previous export)
    // But we have to assert it's completely empty beforehand
    if (!valid)
    {
        assert(memory_blocks.empty() &&
               "Invalid allocator still has memory blocks");
        assert(free_slots.empty() && "Invalid allocator still has free slots");
        assert(current_block_slot == 0 &&
               "Invalid allocator still has a current block slot");
        valid = true; // Mark the allocator as valid again
    }

    // Reset current block slot
    current_block_slot = 0;

    // Insert the blocks to the front of the memory blocks vector
    for (pointer block : blocks)
    {
        assert(block != nullptr &&
               "There are null pointers in the provided blocks");
        // Push the block to the memory blocks vector
        memory_blocks.emplace_back(block);
    }
}

template <typename T, size_t BlockSize>
inline void
PoolAllocator<T, BlockSize>::import_pool(
    const PoolAllocator<T, BlockSize>& other)
{
    // An invalid allocator CAN import a pool (reviving from a previous export)
    // But its emptiness check is done in the other import_pool function
    // But we have to assert the other allocator is valid
    assert(other.valid && "Other allocator is not valid");

    // Export the blocks from the other allocator
    std::vector<pointer> blocks = other.export_pool();
    // Import the blocks into this allocator
    import_pool(blocks);
}

// Construct an object in the allocated memory
template <typename T, size_t BlockSize>
template <class U, class... Args>
void
PoolAllocator<T, BlockSize>::construct(U* p, Args&&... args) noexcept
{
    assert_valid();

    // Use placement new to construct the object in the allocated memory
    new (p) U(std::forward<Args>(args)...);
}

// Destroy an object in the allocated memory
template <typename T, size_t BlockSize>
template <class U>
void
PoolAllocator<T, BlockSize>::destroy(U* p) noexcept
{
    assert_valid();

    // Call the destructor of the object
    p->~U();
}

// Maximum size of the pool
template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::size_type
PoolAllocator<T, BlockSize>::max_size() const noexcept
{
    assert_valid();

    // Calculate the maximum number of objects that can be allocated in a
    // block
    return std::numeric_limits<PoolAllocator<T, BlockSize>::size_type>::max() /
           sizeof(T);
}

// Unique pointer support
// Deleter for unique_ptr
template <typename T, size_t BlockSize>
template <typename U>
void
PoolAllocator<T, BlockSize>::Deleter::operator()(U* ptr) const noexcept
{
    // We don't need valid assertion since that's already checked by
    // delete_object

    static_assert(sizeof(U) > 0,
                  "Deleter cannot be used with incomplete types");
    // Call delete_object on the allocator
    allocator->delete_object(ptr);
}

// Create a unique pointer with a custom deleter
template <typename T, size_t BlockSize>
template <class... Args>
inline std::unique_ptr<T, typename PoolAllocator<T, BlockSize>::Deleter>
PoolAllocator<T, BlockSize>::make_unique(Args&&... args)
{
    assert_valid();

    pointer raw = allocate(1);
    try
    {
        // Construct the object in the allocated memory
        construct(raw, std::forward<Args>(args)...);
    }
    catch (...)
    {
        deallocate(raw, 1);
        throw;
    }
    return std::unique_ptr<T, Deleter>(raw, Deleter{this});
}

// Create a new object in the pool
// Default constructor
template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::pointer
PoolAllocator<T, BlockSize>::new_object()
{
    assert_valid();

    // Allocate a single object
    pointer p = allocate(1);
    try
    {
        // Construct the object in the allocated memory
        construct(p);
    }
    catch (...)
    {
        deallocate(p, 1);
        throw;
    }
    return p;
}

// Create a new object in the pool with arguments
template <typename T, size_t BlockSize>
template <class... Args>
typename PoolAllocator<T, BlockSize>::pointer
PoolAllocator<T, BlockSize>::new_object(Args&&... args)
{
    assert_valid();

    // Allocate a single object
    pointer p = allocate(1);
    try
    {
        // Construct the object in the allocated memory with arguments
        construct(p, std::forward<Args>(args)...);
    }
    catch (...)
    {
        deallocate(p, 1);
        throw;
    }
    return p;
}

// Delete an object in the pool
template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::delete_object(pointer p)
{
    assert_valid();

    // Call the destructor of the object
    destroy(p);
    // Deallocate the object
    deallocate(p, 1);
}

template <typename T, size_t BlockSize>
inline bool
PoolAllocator<T, BlockSize>::has_free_slots()
{
    if (!valid)
    {
        assert(memory_blocks.empty());
        return false; // Invalid allocators have no free slots
    }
    else
    {
        if (memory_blocks.empty())
        {
            return false; // No memory blocks, no free slots
        }
        else if (current_block_slot < BlockSize)
        {
            return true; // Current block has free slots
        }
        else
        {
            return !free_slots.empty(); // Check the free slots stack
        }
    }
}

template <typename T, size_t BlockSize>
inline bool
PoolAllocator<T, BlockSize>::has_blocks()
{
    if (!valid)
    {
        assert(memory_blocks.empty());
        return false; // Invalid allocators have no blocks
    }
    else
    {
        return !memory_blocks.empty(); // Check if there are any memory blocks
    }
}

template <typename T, size_t BlockSize>
inline typename PoolAllocator<T, BlockSize>::size_type
PoolAllocator<T, BlockSize>::total_size()
{
    if (!valid)
    {
        assert(memory_blocks.empty());
        return 0; // Invalid allocators have no total size
    }
    else
    {
        return memory_blocks.size() * BlockSize;
    }
}
