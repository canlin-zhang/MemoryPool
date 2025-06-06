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
    // Move free blocks linked list
    free_blocks = other.free_blocks;
    // Move current block and slot index
    current_block = other.current_block;
    current_block_end = other.current_block_end;
    current_block_slot = other.current_block_slot;

    // Move free slots linked list
    free_slots = other.free_slots;

    // Reset the other allocator
    other.current_block = nullptr;
    other.current_block_end = nullptr;
    other.current_block_slot = 0;
    other.free_slots = nullptr;
    other.free_blocks = nullptr;
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
    // Simple case -> T is larger than Slot pointer
    if constexpr (alignof(T) >= alignof(Slot))
    {
        // Calculate whole block alignment
        constexpr size_type block_align = std::max(alignof(T), alignof(Block));

        // Deallocate all blocks
        while (free_blocks != nullptr)
        {
            Block *next = free_blocks->prev;
            ::operator delete(free_blocks, std::align_val_t(block_align));
            free_blocks = next;
        }
    }
    // incontiguous memory layout
    else
    {
        // Calculate whole block alignment
        constexpr size_type block_align = std::max(alignof(Slot), alignof(Block));

        // Delete by Slot alignment
        while (free_blocks != nullptr)
        {
            Block *next = free_blocks->prev;
            ::operator delete(free_blocks, std::align_val_t(block_align));
            free_blocks = next;
        }
    }
}

// Move assignment operator
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize> &
PoolAllocator<T, BlockSize>::operator=(PoolAllocator &&other) noexcept
{
    // Move free blocks linked list
    free_blocks = other.free_blocks;
    // Move current block and slot index
    current_block = other.current_block;
    current_block_end = other.current_block_end;
    current_block_slot = other.current_block_slot;

    // Move free slots linked list
    free_slots = other.free_slots;

    // Reset the other allocator
    other.current_block = nullptr;
    other.current_block_end = nullptr;
    other.current_block_slot = 0;
    other.free_slots = nullptr;
    other.free_blocks = nullptr;

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

template <typename T, size_t BlockSize>
void PoolAllocator<T, BlockSize>::allocateBlock()
{
    // Simple case -> T is larger than Slot pointer
    if constexpr (alignof(T) >= alignof(Slot))
    {
        // Maximum alignment required between Block and T
        constexpr size_type block_align = std::max(alignof(T), alignof(Block));
        constexpr size_type stride = std::max(sizeof(T), alignof(T));

        // We'll allocate extra padding in case alignment adjustment is needed
        constexpr size_type overhead = sizeof(Block) + alignof(T);
        constexpr size_type total_size = BlockSize + overhead;

        // Allocate raw memory
        void *raw = ::operator new(total_size, std::align_val_t(block_align));

        // Block metadata at beginning
        Block *new_block = reinterpret_cast<Block *>(raw);

        // Find aligned start of T objects after metadata
        void *data_start = static_cast<char *>(raw) + sizeof(Block);
        uintptr_t aligned = (reinterpret_cast<uintptr_t>(data_start) + alignof(T) - 1) & ~(alignof(T) - 1);
        pointer block = reinterpret_cast<pointer>(aligned);

        // Compute how many T objects fit from `block` to end of allocation
        size_type available_bytes = total_size - (aligned - reinterpret_cast<uintptr_t>(raw));
        size_type numObjects = available_bytes / stride;

        if (numObjects < 1)
        {
            ::operator delete(raw, std::align_val_t(block_align));
            throw std::bad_alloc();
        }

        // Store block metadata
        new_block->ptr = block;
        new_block->prev = free_blocks;
        free_blocks = new_block;

        // Update bump pointer state
        current_block = block;
        current_block_end = block + numObjects;
        current_block_slot = 0;
    }
    // Second case -> just allocate Slot structs, accept non-contiguous memory layout for T.
    else
    {
        // Maximum alignment required between Block and T
        constexpr size_type block_align = std::max(alignof(Slot), alignof(Block));
        constexpr size_type stride = std::max(sizeof(Slot), alignof(Slot));

        // We'll allocate extra padding in case alignment adjustment is needed
        constexpr size_type overhead = sizeof(Block) + alignof(Slot);
        constexpr size_type total_size = BlockSize + overhead;

        // Allocate raw memory
        void *raw = ::operator new(total_size, std::align_val_t(block_align));

        // Block metadata at beginning
        Block *new_block = reinterpret_cast<Block *>(raw);

        // Find aligned start of T objects after metadata
        void *data_start = static_cast<char *>(raw) + sizeof(Block);
        uintptr_t aligned = (reinterpret_cast<uintptr_t>(data_start) + alignof(Slot) - 1) & ~(alignof(Slot) - 1);
        pointer block = reinterpret_cast<pointer>(aligned);

        // Compute how many T objects fit from `block` to end of allocation
        size_type available_bytes = total_size - (aligned - reinterpret_cast<uintptr_t>(raw));
        size_type numObjects = available_bytes / stride;

        if (numObjects < 1)
        {
            ::operator delete(raw, std::align_val_t(block_align));
            throw std::bad_alloc();
        }

        // Store block metadata
        new_block->ptr = block;
        new_block->prev = free_blocks;
        free_blocks = new_block;

        // Update bump pointer state
        current_block = block;
        current_block_end = block + numObjects;
        current_block_slot = 0;
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
        // Simple case -> T is larger than Slot pointer, contiguous memory layout
        if constexpr (alignof(T) >= alignof(Slot))
        {
            // Check whether we have available slots from current block
            if (current_block != nullptr && current_block + current_block_slot < current_block_end)
            {
                // Then move up the current block slot index by 1
                pointer p = current_block + current_block_slot;
                current_block_slot++;
                // Return the pointer to the allocated object
                return p;
            }
            // If not, check free list first
            else if (free_slots != nullptr)
            {
                // Pop the first available slot from the linked list
                pointer p = reinterpret_cast<pointer>(free_slots);
                Slot *prev = free_slots->prev;
                free_slots = prev; // Move to the next slot in the list
                return p;
            }
            // If no available slots, allocate a new block
            else
            {
                // Allocate a new block
                allocateBlock();
                // Then allocate the object from the new block
                pointer p = current_block + current_block_slot;
                current_block_slot++;
                return p;
            }
        }
        // Non-contiguous memory layout
        else
        {
            // Check whether we have available slots from current block
            if (current_block != nullptr && current_block + current_block_slot < current_block_end)
            {
                // Then move up the current block slot index by 1
                Slot *p = reinterpret_cast<Slot *>(current_block);
                p += current_block_slot;
                current_block_slot++;
                // Return the pointer to the allocated object
                return reinterpret_cast<pointer>(p);
            }
            // If not, check free list first
            else if (free_slots != nullptr)
            {
                // Pop the first available slot from the linked list
                pointer p = reinterpret_cast<pointer>(free_slots);
                Slot *prev = free_slots->prev;
                free_slots = prev; // Move to the next slot in the list
                return p;
            }
            // If no available slots, allocate a new block
            else
            {
                // Allocate a new block
                allocateBlock();
                // Then allocate the object from the new block
                Slot *p = reinterpret_cast<Slot *>(current_block + current_block_slot);
                current_block_slot++;
                return reinterpret_cast<pointer>(p);
            }
        }
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
    // We push it back to the available slots
    else
    {
        // Simple case -> T is larger than Slot pointer, contiguous memory layout
        if constexpr (alignof(T) >= alignof(Slot))
        {
            // Fill up available slot linked list
            if (free_slots == nullptr)
            {
                // Create a new slot
                free_slots = reinterpret_cast<Slot *>(p);
                free_slots->prev = nullptr;
            }
            else
            {
                // Push the pointer to the front of the linked list
                Slot *new_slot = reinterpret_cast<Slot *>(p);
                new_slot->prev = free_slots; // Link to the previous head
                free_slots = new_slot;
            }
        }
        // incontiguous memory layout
        else
        {
            // Fill up available slot linked list
            if (free_slots == nullptr)
            {
                // Create a new slot
                free_slots = reinterpret_cast<Slot *>(p);
                free_slots->prev = nullptr;
            }
            else
            {
                // Push the pointer to the front of the linked list
                Slot *new_slot = reinterpret_cast<Slot *>(p);
                new_slot->prev = free_slots; // Link to the previous head
                free_slots = new_slot;
            }
        }
    }
}

// Construct an object in the allocated memory
template <typename T, size_t BlockSize>
template <class U, class... Args>
void PoolAllocator<T, BlockSize>::construct(U *p, Args &&...args) noexcept
{
    // Use placement new to construct the object in the allocated memory
    new (p) U(std::forward<Args>(args)...);
}
// Destroy an object in the allocated memory
template <typename T, size_t BlockSize>
template <class U>
void PoolAllocator<T, BlockSize>::destroy(U *p) noexcept
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
    allocator->delete_object(ptr);
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