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

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stack>
#include <vector>

template <typename T, size_t BlockSize = 4096>
class PoolAllocator
{
  public:
    /* Member types */
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using void_pointer = void*;
    using const_void_pointer = const void*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_copy_assignment = std::false_type;
    using propagate_on_container_move_assignment = std::false_type;
    using propagate_on_container_swap = std::true_type;
    using is_always_equal = std::false_type;

    /* Member functions */
    // Default constructor
    PoolAllocator() noexcept;
    // No Copy constructor
    PoolAllocator(const PoolAllocator& other) = delete;
    // No Move constructor
    PoolAllocator(PoolAllocator&& other) = delete;
    // No Templated copy
    template <class U>
    PoolAllocator(const PoolAllocator<U, BlockSize>& other) = delete;
    // Destructor
    ~PoolAllocator() noexcept;

    // Assignment operator
    // We do not allow copy assignment for allocators
    PoolAllocator& operator=(const PoolAllocator& other) = delete;
    // We do not allow move assignment for allocators
    PoolAllocator& operator=(PoolAllocator&& other) = delete;

    // Allocation and deallocation
    pointer allocate(size_type n = 1);
    void deallocate(pointer p, size_type n = 1) noexcept;

    // Construct and destory functions
    template <class U, class... Args>
    void construct(U* p, Args&&... args);
    template <class U>
    void destroy(U* p) noexcept;

    // Maximum size of the pool
    size_type max_size() const noexcept;

    // Unique pointer support
    // Deleter
    struct Deleter
    {
        // Pool address so we know which pool to use for deletion
        // If a global memory pool is used, user can implement a deleter that doesn't increase size
        // of unique_ptr
        PoolAllocator* allocator = nullptr;
        template <typename U>
        void operator()(U* ptr) const noexcept;
    };

    // make unique
    template <class... Args>
    std::unique_ptr<T, Deleter> make_unique(Args&&... args);

    // Create new object with empty constructor
    [[nodiscard]] pointer new_object();

    // Create new object with arguments
    template <class... Args>
    [[nodiscard]] pointer new_object(Args&&... args);

    // Delete an object
    void delete_object(pointer p) noexcept;

    // Debug helper functions
    // Get total allocated size
    inline size_type total_allocated_size() const noexcept
    {
        return memory_blocks.size() * BlockSize;
    }

    // Get total number of free slots
    // Does not account for partial blocks
    inline size_type total_free_slots() const noexcept
    {
        return free_slots.size();
    }

    // Transfer free slots from another allocator
    void transfer_free(PoolAllocator<T, BlockSize>& from);
    // Transfer all memory blocks and free slots from another allocator
    void transfer_all(PoolAllocator<T, BlockSize>& from);

  private:
    //! A pointer to the beginning (or end) of a block
    using block_pointer = T*;
    //! Given a block pointer, return a "end" pointer for that block
    block_pointer cur_block_end() const
    {
        return memory_blocks.back() + BlockSize / sizeof(T);
    }

    // Allocate a memory block
    void allocate_block();

    struct ExportedAlloc
    {
        // Free slots in the block
        std::vector<pointer> free_slots;

        // Memory blocks - Optional, only used in export_all and import
        std::vector<block_pointer> memory_blocks;
    };

    // Allocator import/export functions
    // Export
    //! Export only the available slots as a vector of pointers.
    //! Warning: This does NOT transfer ownership of the underlying memory blocks.
    //! Do NOT use this function in threads with shorter lifetimes than other threads
    //! accessing objects backed by this allocator. Doing so may lead to use-after-free.
    ExportedAlloc export_free();

    //! Export all the memory blocks + available slots
    ExportedAlloc export_all();

    // Import
    //! Import all memory blocks and free slots from an ExportedAlloc
    void import(ExportedAlloc exported);

    // Pointer to blocks of memory
    std::vector<block_pointer> memory_blocks;
    pointer current_block_slot = 0; // Current slot in the current block

    // Free list
    std::vector<pointer> free_slots;
};

// Operators
// Only two references to the same allocator are equal
template <typename T, size_t BlockSize>
inline bool
operator==(const PoolAllocator<T, BlockSize>& a, const PoolAllocator<T, BlockSize>& b) noexcept
{
    return &a == &b;
}

template <typename T, size_t BlockSize>
inline bool
operator!=(const PoolAllocator<T, BlockSize>& a, const PoolAllocator<T, BlockSize>& b) noexcept
{
    return !(a == b);
}

// include the implementation file
#include "pool_allocator.tcc"
