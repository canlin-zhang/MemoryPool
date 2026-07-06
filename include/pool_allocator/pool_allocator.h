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
 * 4. Added transfer_all and transfer_free functions
 *
 * Modifed by Eric Norige
 * Changes:
 * 1. Separated PoolAllocator logic cleanly into BumpAllocator and StackAllocator
 * 2. Added [[nodiscard]] to export and raw pointer allocation functions
 */

#include <atomic>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <mutex>
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
    using BlocksContainer = std::vector<block_pointer>;
    using FreeSlotsContainer = std::vector<pointer>;

    BumpAllocator() noexcept = default;
    BumpAllocator(const BumpAllocator&) = delete;
    BumpAllocator(BumpAllocator&&) noexcept = default;
    BumpAllocator& operator=(const BumpAllocator&) = delete;
    BumpAllocator& operator=(BumpAllocator&&) noexcept = default;
    explicit BumpAllocator(Alloc&& alloc) noexcept;

    [[nodiscard]] pointer allocate(size_t n = 1);
    void deallocate(pointer p, size_t n = 1) noexcept;

    ~BumpAllocator() noexcept;
    // Metrics
    size_t allocated_bytes() const noexcept;
    size_t bump_remaining() const noexcept;
    // Export: move remaining bump slots to free list and move blocks out.
    // New overload that returns the exported value.
    [[nodiscard]] std::pair<FreeSlotsContainer, BlocksContainer> export_all();
    // Import: take ownership of blocks (for accounting and destruction). Bump remains empty.
    void import_blocks(BlocksContainer&& in_blocks);

    Alloc parent;

  private:
    BumpBlock<pointer> bump;
    BlocksContainer blocks;
};

//! SFINAE-safe trait: is sizeof(T) available AND T is smaller than a pointer?
//! Incomplete types → false (default to intrusive — safe for any class).
template <typename T, typename = void>
struct is_small_type : std::false_type
{
};

template <typename T>
struct is_small_type<T, std::void_t<decltype(sizeof(T))>>
    : std::bool_constant<(sizeof(T) < sizeof(void*))>
{
};

//! Fallback free-list container for types smaller than a pointer
//! (int, char, etc.) — uses std::vector because the slot can't hold a next pointer.
template <typename T>
struct SmallTypeFreeList
{
    std::vector<T*> free_slots;
};

//! Intrusive free-list for types that are at least pointer-sized.
//! Each freed slot stores a next pointer in its own payload bytes — no side allocation.
template <typename T>
struct IntrusiveFreeList
{
    T* free_head = nullptr;
    size_t free_count = 0;
};

//! Wraps another allocator with a stack so that single deallocations
//! can be easily returned as single allocations.
//!
//! For sizeof(T) >= sizeof(void*): uses an intrusive singly-linked list threaded
//! through the unused slot storage.  Deallocate, transfer_free, and transfer_all
//! are genuinely noexcept — no side container, no allocation.
//!
//! For smaller types: falls back to std::vector.
template <typename T, typename Alloc = std::allocator<T>>
class StackAllocator
{
    static constexpr bool use_intrusive = !is_small_type<T>::value;
    using FreeStore =
        typename std::conditional<use_intrusive, IntrusiveFreeList<T>, SmallTypeFreeList<T>>::type;

  public:
    using value_type = T;
    using pointer = value_type*;

    StackAllocator() noexcept = default;
    explicit StackAllocator(Alloc&& alloc) noexcept;
    StackAllocator(const StackAllocator&) = delete;
    StackAllocator(StackAllocator&&) noexcept = default;
    StackAllocator& operator=(const StackAllocator&) = delete;
    StackAllocator& operator=(StackAllocator&&) noexcept = default;

    [[nodiscard]] pointer allocate(size_t n = 1);

    void deallocate(pointer p, size_t n = 1) noexcept;

    // Metrics
    size_t free_size() const noexcept;

    // Transfer APIs
    void transfer_free(StackAllocator& from);
    void transfer_all(StackAllocator& from);

    Alloc parent;

  private:
    FreeStore free_store;
};

// CRTP mixin that provides object helpers (construct/destroy, new/delete, make_unique)
// to any Derived that implements allocate(n) and deallocate(ptr, n).
template <typename Derived, typename T>
class ObjectOpsMixin
{
  public:
    using value_type = T;
    using pointer = T*;

    template <class U, class... Args>
    static void construct(U* p, Args&&... args);
    template <class U>
    static void destroy(U* p) noexcept;

    [[nodiscard]] pointer new_object();
    template <class... Args>
    [[nodiscard]] pointer new_object(Args&&... args);
    void delete_object(pointer p) noexcept;

    template <class Deleter, class... Args>
    [[nodiscard]] std::unique_ptr<value_type, Deleter> make_unique_with(Deleter del,
                                                                        Args&&... args);
};

} // namespace pool_allocator_detail

// PoolAllocator combines a bump allocator with a stack allocator
template <typename T, size_t BlockSize = 4096>
class PoolAllocator : public pool_allocator_detail::ObjectOpsMixin<PoolAllocator<T, BlockSize>, T>
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
    PoolAllocator() noexcept;
    ~PoolAllocator() noexcept;
    // No Copy/move
    PoolAllocator(const PoolAllocator& other) = delete;
    PoolAllocator(PoolAllocator&& other) = delete;
    template <class U>
    PoolAllocator(const PoolAllocator<U, BlockSize>& other) = delete;
    PoolAllocator& operator=(const PoolAllocator& other) = delete;
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

    // functions provided by ObjectOpsMixin
    // template <class U, class... Args>
    // void construct(U* p, Args&&... args);
    // template <class U>
    // void destroy(U* p) noexcept;
    // [[nodiscard]] pointer new_object();
    // template <class... Args>
    // [[nodiscard]] pointer new_object(Args&&... args);
    // void delete_object(pointer p) noexcept;

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

    // Transfer free slots from another allocator.
    // Locks this allocator's mutex (destination). The caller must be the owning
    // thread of `from` (source) — no lock is taken on the source.
    void transfer_free(PoolAllocator<T, BlockSize>& from);

    // Transfer all memory blocks and free slots from another allocator.
    // Locks this allocator's mutex (destination). The caller must be the owning
    // thread of `from` (source) — no lock is taken on the source.
    void transfer_all(PoolAllocator<T, BlockSize>& from);

  private:
    // Compose a free-list stack allocator over a bump allocator for backing blocks.
    using BumpAlloc = pool_allocator_detail::BumpAllocator<T, std::allocator<T>, BlockSize>;
    using ComboAlloc = pool_allocator_detail::StackAllocator<T, BumpAlloc>;

    // No explicit export/import API; transfer functions call underlying allocator ops directly
    ComboAlloc allocator; // owns BlockAlloc internally and free list on top

    // Mutex protecting this allocator as a transfer destination.
    // Only locked by transfer_all/transfer_free on the destination side;
    // the source is assumed to be accessed only by its owning thread.
    std::mutex transfer_mutex;

    // Set while this instance participates in a transfer (as source or
    // destination). transfer_all/transfer_free read the source unlocked, so two
    // threads transferring with a shared source (or a source that is another's
    // destination) is a data race. The debug assert in transfer_all/_free trips
    // on that overlap; the flag itself is always present so debug and release
    // share one layout.
    std::atomic<bool> in_transfer_{false};
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
