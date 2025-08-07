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
#include <optional>
#include <stack>
#include <vector>

template <typename T, size_t BlockSize>
struct ExportedAlloc
{
    using pointer = T*;
    using size_type = std::size_t;

    // Free slots in the block
    std::stack<pointer, std::vector<pointer>> free_slots;

    // Memory blocks - Optional, only used in export_all and import_all
    std::optional<std::vector<pointer>> memory_blocks;
    // Bump allocation counter - Optional, only used in export_all and import_all
    std::optional<size_type> current_block_slot;
};

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

    // /* Legacy Rebind struct */
    // template <typename U>
    // struct rebind
    // {
    //     typedef PoolAllocator<U, BlockSize> other;
    // };

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

    // Address functions
    pointer addressof(reference x) const noexcept;
    const_pointer addressof(const_reference x) const noexcept;

    // Allocation and deallocation
    pointer allocate(size_type n = 1);
    void deallocate(pointer p, size_type n = 1);

    // Construct and destory functions
    template <class U, class... Args>
    void construct(U* p, Args&&... args) noexcept;
    template <class U>
    void destroy(U* p) noexcept;

    // Maximum size of the pool
    size_type max_size() const noexcept;

    // Unique pointer support
    // Deleter
    struct Deleter
    {
        // Pool address so we know which pool to use for deletion
        PoolAllocator* allocator = nullptr;
        template <typename U>
        void operator()(U* ptr) const noexcept;
    };

    // make unique
    template <class... Args>
    std::unique_ptr<T, Deleter> make_unique(Args&&... args);

    // Create new object with empty constructor
    pointer new_object();

    // Create new object with arguments
    template <class... Args>
    pointer new_object(Args&&... args);

    // Delete an object
    void delete_object(pointer p);

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

    // Allocator import/export functions
    // Export
    //! Export only the available slots as a vector of pointers.
    //! Warning: This does NOT transfer ownership of the underlying memory blocks.
    //! Do NOT use this function in threads with shorter lifetimes than other threads
    //! accessing objects backed by this allocator. Doing so may lead to use-after-free.
    ExportedAlloc<T, BlockSize> export_free();

    //! Export all the memory blocks + available slots
    ExportedAlloc<T, BlockSize> export_all();

    // Import
    //! Import free slots from an ExportedAlloc
    void import_free(const ExportedAlloc<T, BlockSize>& exported);
    void import_free(PoolAllocator<T, BlockSize>& from);

    //! Import all memory blocks and free slots from an ExportedAlloc
    void import_all(const ExportedAlloc<T, BlockSize>& exported);
    void import_all(PoolAllocator<T, BlockSize>& from);

  private:
    // Allocate a memory block
    void allocateBlock();

    // Pointer to blocks of memory
    std::vector<pointer> memory_blocks;
    size_type current_block_slot = 0; // Current slot in the current block

    // Free list
    std::stack<pointer, std::vector<pointer>> free_slots;
};

// Operators
// Operator != and ==
template <typename T1, size_t B1, typename T2, size_t B2>
inline bool
operator==(const PoolAllocator<T1, B1>&, const PoolAllocator<T2, B2>&) noexcept
{
    return B1 == B2;
}

// Only two references to the same allocator are equal
template <typename T, size_t BlockSize>
inline bool
operator==(const PoolAllocator<T, BlockSize>& a, const PoolAllocator<T, BlockSize>& b) noexcept
{
    return &a == &b;
}

template <typename T1, size_t B1, typename T2, size_t B2>
inline bool
operator!=(const PoolAllocator<T1, B1>& a, const PoolAllocator<T2, B2>& b) noexcept
{
    return !(a == b);
}

template <typename T, size_t BlockSize>
inline bool
operator!=(const PoolAllocator<T, BlockSize>& a, const PoolAllocator<T, BlockSize>& b) noexcept
{
    return !(a == b);
}

// include the implementation file
#include "pool_allocator.tcc"
