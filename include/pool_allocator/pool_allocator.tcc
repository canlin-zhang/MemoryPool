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
    // Calculate item alignment
    constexpr size_type items_per_block = BlockSize / sizeof(T);
    static_assert(items_per_block > 0, "Block size is too small for the type T");
    this->num_items = items_per_block;
    this->item_size = sizeof(T);
}

// Destructor
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize>::~PoolAllocator() noexcept
{
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
    return std::addressof(x);
}

template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::const_pointer
PoolAllocator<T, BlockSize>::addressof(const_reference x) const noexcept
{
    return std::addressof(x);
}

template <typename T, size_t BlockSize>
inline ExportedAlloc<T, BlockSize>
PoolAllocator<T, BlockSize>::export_free()
{
    ExportedAlloc<T, BlockSize> exported;
    exported.free_slots = std::move(free_slots);
    // Clear the free slots stack
    free_slots = std::stack<pointer, std::vector<pointer>>();

    // No memory blocks to export
    exported.memory_blocks = std::vector<pointer>();
    // No current block slot to export
    exported.current_block_slot = 0;

    return exported;
}

template <typename T, size_t BlockSize>
ExportedAlloc<T, BlockSize>
PoolAllocator<T, BlockSize>::export_all()
{
    ExportedAlloc<T, BlockSize> exported;
    // Move the free slots to the exported struct
    exported.free_slots = std::move(free_slots);
    // Clear the free slots stack
    free_slots = std::stack<pointer, std::vector<pointer>>();

    // Move memory blocks to the exported struct
    exported.memory_blocks = std::move(memory_blocks);
    // Clear the memory blocks (don't free them)
    memory_blocks = std::vector<pointer>();

    // Move the current block slot to the exported struct
    exported.current_block_slot = current_block_slot;
    // Reset the current block slot in the allocator
    current_block_slot = 0;

    return exported;
}

template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::import_free(ExportedAlloc<T, BlockSize>& exported)
{
    // Append the free slots from the exported allocator efficiently
    free_slots.c.insert(free_slots.c.end(),
                        std::make_move_iterator(exported.free_slots.c.begin()),
                        std::make_move_iterator(exported.free_slots.c.end()));

    // Clear the imported free slots to avoid potential reuse
    exported.free_slots = std::stack<pointer, std::vector<pointer>>();
}

template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::import_free(PoolAllocator<T, BlockSize>& from)
{
    assert(&from != this && "Cannot import directly from self");

    // Export and Import the free slots
    auto exported = from.export_free();
    import_free(exported);
}

template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::import_all(ExportedAlloc<T, BlockSize>& exported)
{
    // Import free slots
    import_free(exported);

    // Compare the bump allocator counter
    // If the current has more free slots (smaller counter), we append the new memory blocks at the
    // back.
    if (!exported.memory_blocks.empty())
    {
        // Convert the partially free (bump allocated) blocks from the imported allocator to free
        // slots
        for (size_type i = exported.current_block_slot; i < this->num_items; ++i)
        {
            // Push the pointer to the free slots stack
            free_slots.push(exported.memory_blocks.back() + i);
        }

        // Always append the new memory blocks at the front so we don't change the back blocks
        memory_blocks.insert(memory_blocks.begin(), exported.memory_blocks.begin(),
                             exported.memory_blocks.end());
    }

    // We don't need to change the current_block_slot here
    // As the imported allocator's partially free slots are already accounted for

    // Clean up the imported structure to avoid potential reuse
    // Clear the imported memory blocks
    exported.memory_blocks = std::vector<pointer>();
    // Reset the imported current block slot
    exported.current_block_slot = 0;
}

template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::import_all(PoolAllocator<T, BlockSize>& from)
{
    assert(&from != this && "Cannot import directly from self");

    // Export and Import the free slots
    auto exported = from.export_all();
    import_all(exported);
}

template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::allocateBlock()
{
    // Allocate a new block of memory
    pointer new_block =
        reinterpret_cast<pointer>(::operator new(BlockSize, std::align_val_t(alignof(T))));

    // Push the new block to the free blocks stack
    memory_blocks.push_back(new_block);

    // Reset block counter
    current_block_slot = 0;
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
        constexpr size_type items_per_block = BlockSize / sizeof(T);

        // Check free slots first
        if (!free_slots.empty())
        {
            // Pop the top slot from the free slots stack
            pointer p = free_slots.top();
            free_slots.pop();
            return p;
        }
        // Check current block slot
        else if (!memory_blocks.empty() && current_block_slot < items_per_block)
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

// Construct an object in the allocated memory
template <typename T, size_t BlockSize>
template <class U, class... Args>
void
PoolAllocator<T, BlockSize>::construct(U* p, Args&&... args) noexcept
{
    // Use placement new to construct the object in the allocated memory
    new (p) U(std::forward<Args>(args)...);
}
// Destroy an object in the allocated memory
template <typename T, size_t BlockSize>
template <class U>
void
PoolAllocator<T, BlockSize>::destroy(U* p) noexcept
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
    return std::numeric_limits<PoolAllocator<T, BlockSize>::size_type>::max() / sizeof(T);
}

// Unique pointer support
// Deleter for unique_ptr
template <typename T, size_t BlockSize>
template <typename U>
void
PoolAllocator<T, BlockSize>::Deleter::operator()(U* ptr) const noexcept
{
    static_assert(sizeof(U) > 0, "Deleter cannot be used with incomplete types");
    // Call delete_object on the allocator
    allocator->delete_object(ptr);
}

// Create a unique pointer with a custom deleter
template <typename T, size_t BlockSize>
template <class... Args>
inline std::unique_ptr<T, typename PoolAllocator<T, BlockSize>::Deleter>
PoolAllocator<T, BlockSize>::make_unique(Args&&... args)
{
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
    // Call the destructor of the object
    destroy(p);
    // Deallocate the object
    deallocate(p, 1);
}
