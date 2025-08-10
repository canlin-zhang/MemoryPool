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
#include <new> // for std::align_val_t

// Default constructor
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize>::PoolAllocator() noexcept
{
    // Check block size vs T size; blocks must hold at least one T
    static_assert(BlockSize / sizeof(T) > 0, "Block size is too small for the type T");
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

template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::ExportedAlloc
PoolAllocator<T, BlockSize>::export_free()
{
    ExportedAlloc exported;
    // this clears this->free_slots so we can't re-export same slots
    std::swap(exported.free_slots, this->free_slots);
    return exported;
}

template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::ExportedAlloc
PoolAllocator<T, BlockSize>::export_all()
{
    ExportedAlloc exported = this->export_free();
    // Before moving the free slots, unwind the partially free bump-allocation block
    // Add its free slots to the exported free slots
    if (this->current_block_slot)
    {
        const pointer end = cur_block_end();
        exported.free_slots.reserve(end - this->current_block_slot);
        // Convert the partially free (bump allocated) blocks to free slots
        while (this->current_block_slot < end)
            exported.free_slots.push_back(this->current_block_slot++);
    }

    // Move memory blocks to the exported struct
    std::swap(exported.memory_blocks, this->memory_blocks);

    // Reset the current block slot in the allocator
    this->current_block_slot = nullptr;

    return exported;
}

template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::import(ExportedAlloc exported)
{
    // Append the free slots from the exported allocator
    free_slots.insert(free_slots.end(), exported.free_slots.begin(), exported.free_slots.end());

    const int prior_last_block_idx = memory_blocks.size() - 1;
    // Append imported memory blocks from the exported allocator
    memory_blocks.insert(memory_blocks.end(), exported.memory_blocks.begin(),
                         exported.memory_blocks.end());

    if (prior_last_block_idx >= 0)
        std::swap(memory_blocks.back(), memory_blocks[prior_last_block_idx]);

    // note: ok to leave current_block_slot as is
    // if it's currently null, we need to allocate a new block in order to use it
    // and if it's not null, we've restored the last memory block to what it was before so that the
    // range [current_block_slot, this->cur_block_end()) is still what's waiting to be allocated.
}

template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::transfer_all(PoolAllocator<T, BlockSize>& from)
{
    assert(&from != this && "Cannot import directly from self");

    // Export and Import the free slots
    this->import(from.export_all());
}

template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::transfer_free(PoolAllocator<T, BlockSize>& from)
{
    assert(&from != this && "Cannot import directly from self");

    // Export and Import the free slots
    this->import(from.export_free());
}

template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::allocate_block()
{
    // Allocate a new block of memory
    block_pointer new_block =
        reinterpret_cast<block_pointer>(::operator new(BlockSize, std::align_val_t(alignof(T))));

    // Push the new block to the free blocks stack
    memory_blocks.push_back(new_block);

    // Reset block counter
    current_block_slot = new_block;
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
            pointer p = free_slots.back();
            free_slots.pop_back();
            return p;
        }
        if (!current_block_slot || current_block_slot >= this->cur_block_end())
            // If no free slots, and no memory blocks, or current block is full
            // Allocate a new block
            allocate_block();
    }
    return current_block_slot++;
}

// Deallocate a single object
template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::deallocate(pointer p, size_type n) noexcept
{
    // Do nothing if n is 0
    if (n == 0)
        return;
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
            free_slots.push_back(p);
        }
    }
}

// Construct an object in the allocated memory
template <typename T, size_t BlockSize>
template <class U, class... Args>
void
PoolAllocator<T, BlockSize>::construct(U* p, Args&&... args)
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
PoolAllocator<T, BlockSize>::delete_object(pointer p) noexcept
{
    // Call the destructor of the object
    destroy(p);
    // Deallocate the object
    deallocate(p, 1);
}
