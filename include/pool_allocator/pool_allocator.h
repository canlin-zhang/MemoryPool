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

#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

// Internal helper for lazy bump allocation of a single contiguous block
namespace pool_allocator_detail
{
template <typename Pointer>
struct BumpBlock
{
    void init(Pointer start, size_t count);
    void reset() noexcept;
    bool empty() const noexcept;
    size_t remaining() const noexcept;
    Pointer allocate_one() noexcept;
    // Move any remaining slots into a free list vector and reset.
    template <typename Vec>
    void export_remaining(Vec& out);

  private:
    Pointer next = nullptr; // next un-split slot
    Pointer end = nullptr;  // one-past-the-last slot in the current block
};

//! A basic bump allocator; uses an underlying allocator to allocate blocks and
//! bumps a pointer within that block to provide allocations.  Doesn't handle deallocations at all,
//! so is best wrapped in StackAllocator.
template <typename T, typename Alloc = std::allocator<T>, size_t BlockSize = 4096>
class BumpAllocator
{
  public:
    /* Member types */
    using value_type = T;
    using pointer = T*;
    using block_pointer = T*;

    BumpAllocator() noexcept = default;
    BumpAllocator(const BumpAllocator&) = delete;
    BumpAllocator(BumpAllocator&&) noexcept = default;
    BumpAllocator& operator=(const BumpAllocator&) = delete;
    BumpAllocator& operator=(BumpAllocator&&) noexcept = default;
    explicit BumpAllocator(Alloc&& alloc) noexcept;

    pointer allocate(size_t n = 1);
    void deallocate(pointer p, size_t n = 1) noexcept;

    ~BumpAllocator() noexcept;
    // Metrics
    size_t allocated_bytes() const noexcept;
    size_t bump_remaining() const noexcept;
    // Export: move remaining bump slots to free list and move blocks out.
    template <class VecSlots, class VecBlocks>
    void export_all(VecSlots& out_free_slots, VecBlocks& out_blocks);
    // Import: take ownership of blocks (for accounting and destruction). Bump remains empty.
    template <class VecBlocks>
    void import_blocks(VecBlocks&& in_blocks);

    Alloc parent;

  private:
    BumpBlock<pointer> bump;
    std::vector<block_pointer> blocks;
};

//! Wraps another allocator with a stack so that single deallocations
//! can be easily returned as single allocations.
template <typename T, typename Alloc = std::allocator<T>>
class StackAllocator
{
  public:
    using value_type = T;
    using pointer = value_type*;

    StackAllocator() noexcept = default;
    explicit StackAllocator(Alloc&& alloc) noexcept;
    StackAllocator(const StackAllocator&) = delete;
    StackAllocator(StackAllocator&&) noexcept = default;
    StackAllocator& operator=(const StackAllocator&) = delete;
    StackAllocator& operator=(StackAllocator&&) noexcept = default;

    pointer allocate(size_t n = 1);

    void deallocate(pointer p, size_t n = 1) noexcept;

    // Metrics
    size_t free_size() const noexcept;
    // Export/import free slots
    template <class Vec>
    void export_free(Vec& out) noexcept;
    template <class Vec>
    void import_free(Vec&& in);
    // Export/import blocks via underlying allocator
    template <class VecSlots, class VecBlocks>
    void export_all(VecSlots& out_slots, VecBlocks& out_blocks);
    template <class VecBlocks>
    void import_blocks(VecBlocks&& in_blocks);

    Alloc parent;

  private:
    std::vector<pointer> free_slots;
};

} // namespace pool_allocator_detail

// PoolAllocator combines a bump allocator with a stack allocator
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

    // Allocation and deallocation are done by allocator
    pointer allocate(size_type n = 1)
    {
        return allocator.allocate(n);
    }
    void deallocate(pointer p, size_type n = 1) noexcept
    {
        allocator.deallocate(p, n);
    }

    // Maximum size of an allocation from this pool
    size_type max_size() const noexcept;

    // Construct and destory functions
    template <class U, class... Args>
    void construct(U* p, Args&&... args);
    template <class U>
    void destroy(U* p) noexcept;

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
    inline size_type allocated_bytes() const noexcept
    {
        return allocator.parent.allocated_bytes();
    }

    // Get total number of free slots in StackAllocator
    inline size_type num_slots_available() const noexcept
    {
        return allocator.free_size();
    }

    // Get number of slots in BumpBlock of BumpAllocator
    inline size_type num_bump_available() const noexcept
    {
        return allocator.parent.bump_remaining();
    }

    // Transfer free slots from another allocator
    void transfer_free(PoolAllocator<T, BlockSize>& from);
    // Transfer all memory blocks and free slots from another allocator
    void transfer_all(PoolAllocator<T, BlockSize>& from);

  private:
    // Compose a free-list stack allocator over a bump allocator for backing blocks.
    using BumpAlloc = pool_allocator_detail::BumpAllocator<T, std::allocator<T>, BlockSize>;
    using ComboAlloc = pool_allocator_detail::StackAllocator<T, BumpAlloc>;
    struct ExportedAlloc
    {
        // Free slots in the block
        std::vector<pointer> free_slots;

        // Memory blocks - Optional, only used in export_all and import
        std::vector<typename BumpAlloc::block_pointer> memory_blocks;
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

    ComboAlloc allocator; // owns BlockAlloc internally and free list on top
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
#include "pool_allocator.tcc" // IWYU pragma: export
