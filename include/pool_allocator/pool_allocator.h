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
 * 1. Separated PoolAllocator logic cleanly into BlockStore and FreeSlotStore
 * 2. Added [[nodiscard]] to export and raw pointer allocation functions
 */

#include <atomic>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

//! Transfer mode policy for transfer_all/transfer_free. `Fast` uses vector-backed
//! stores (transfer may throw on allocation). `Noexcept` uses list-backed stores
//! plus a non-throwing spinlock, so the transfer allocates nothing and takes no
//! throwing lock — the API is declared `noexcept` in that mode. Requires
//! sizeof(T) >= sizeof(T*).
enum class TransferMode
{
    Fast,
    Noexcept
};

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
    // Return the un-carved range [next, end) and reset the bump. noexcept.
    std::pair<Pointer, Pointer> take_range() noexcept;

  private:
    Pointer next = nullptr; // next un-split slot
    Pointer end = nullptr;  // one-past-the-last slot in the current block
};

//! Singly-linked list of T-sized slots whose link (a T*) is stored inline in
//! each slot's own bytes (no separate nodes), so push/pop/splice allocate
//! nothing and are noexcept. Requires sizeof(T) >= sizeof(T*). Backs a store
//! under the Noexcept transfer policy.
template <typename T>
struct SlotList
{
    static_assert(sizeof(T) >= sizeof(T*),
                  "SlotList stores a T* link in each slot; needs sizeof(T) >= sizeof(T*)");

    // Backend traits (read by the stores; see backend_for).
    static constexpr size_t slots_per_block_overhead = 1; // link lives in the block's slot 0
    static constexpr bool nothrow_transfer = true;        // push/pop/splice allocate nothing

    SlotList() noexcept = default;
    SlotList(const SlotList&) = delete;
    SlotList& operator=(const SlotList&) = delete;
    SlotList(SlotList&& o) noexcept
        : head(o.head)
        , tail(o.tail)
        , count(o.count)
    {
        o.head = o.tail = nullptr;
        o.count = 0;
    }
    SlotList& operator=(SlotList&& o) noexcept
    {
        if (this != &o)
        {
            head = o.head;
            tail = o.tail;
            count = o.count;
            o.head = o.tail = nullptr;
            o.count = 0;
        }
        return *this;
    }

    void push(T* slot) noexcept;
    void reserve(size_t) noexcept
    {
    } // a list never reallocates: nothing to reserve
    T* pop() noexcept;
    bool empty() const noexcept
    {
        return head == nullptr;
    }
    size_t size() const noexcept
    {
        return count;
    }
    // Move all of `from`'s slots onto this list; `from` left empty. O(1), noexcept.
    void splice(SlotList& from) noexcept;

  private:
    T* head = nullptr;
    T* tail = nullptr;
    size_t count = 0;
};

//! Vector-backed twin of SlotList with the same interface. Backs a store under
//! the Fast transfer policy; push/splice may throw (allocation), matching the
//! library's historical behavior.
template <typename T>
struct SlotVector
{
    // Backend traits (read by the stores; see backend_for).
    static constexpr size_t slots_per_block_overhead = 0; // block pointers held out-of-line
    static constexpr bool nothrow_transfer = false;       // push/splice may reallocate

    void reserve(size_t additional)
    {
        slots.reserve(slots.size() + additional);
    }
    void push(T* slot)
    {
        slots.push_back(slot);
    }
    T* pop() noexcept
    {
        T* p = slots.back();
        slots.pop_back();
        return p;
    }
    bool empty() const noexcept
    {
        return slots.empty();
    }
    size_t size() const noexcept
    {
        return slots.size();
    }
    void splice(SlotVector& from)
    {
        slots.insert(slots.end(), from.slots.begin(), from.slots.end());
        from.slots.clear();
    }

  private:
    std::vector<T*> slots;
};

//! The single point that decodes the public TransferMode enum into a concrete
//! store backend type. The stores below are parameterized on that backend type,
//! never on the mode; adding a mode is a new specialization here.
template <TransferMode Mode, typename T>
struct backend_for
{
    using type = SlotVector<T>;
};
template <typename T>
struct backend_for<TransferMode::Noexcept, T>
{
    using type = SlotList<T>;
};

//! A basic bump allocator; uses an underlying allocator to allocate blocks and
//! bumps a pointer within that block to provide allocations.  Doesn't handle deallocations at all,
//! so is best wrapped in FreeSlotStore.
template <typename T, typename Alloc = std::allocator<T>, size_t BlockSize = 4096,
          typename Blocks = SlotVector<T>>
class BlockStore
{
  public:
    /* Member types */
    using value_type = T;
    using pointer = T*;
    using block_pointer = T*;
    // True when the block-chain backend's transfer ops allocate nothing.
    static constexpr bool nothrow_transfer = Blocks::nothrow_transfer;

    BlockStore() noexcept = default;
    BlockStore(const BlockStore&) = delete;
    BlockStore(BlockStore&&) noexcept = default;
    BlockStore& operator=(const BlockStore&) = delete;
    BlockStore& operator=(BlockStore&&) noexcept = default;
    explicit BlockStore(Alloc&& alloc) noexcept;

    [[nodiscard]] pointer allocate(size_t n = 1);
    void deallocate(pointer p, size_t n = 1) noexcept;

    ~BlockStore() noexcept;
    // Metrics
    size_t allocated_bytes() const noexcept;
    size_t bump_remaining() const noexcept;
    // Return the current block's un-carved slots and reset the bump. noexcept.
    std::pair<pointer, pointer> take_bump_range() noexcept;
    // Splice `from`'s blocks into this store. noexcept when the backend's ops don't allocate.
    void transfer_blocks(BlockStore& from) noexcept(Blocks::nothrow_transfer);

    Alloc parent;

  private:
    BumpBlock<pointer> bump;
    Blocks blocks;
};

//! Wraps another allocator with a stack so that single deallocations
//! can be easily returned as single allocations.
template <typename T, typename Alloc = std::allocator<T>, typename FreeStore = SlotVector<T>>
class FreeSlotStore
{
  public:
    using value_type = T;
    using pointer = value_type*;

    FreeSlotStore() noexcept = default;
    explicit FreeSlotStore(Alloc&& alloc) noexcept;
    FreeSlotStore(const FreeSlotStore&) = delete;
    FreeSlotStore(FreeSlotStore&&) noexcept = default;
    FreeSlotStore& operator=(const FreeSlotStore&) = delete;
    FreeSlotStore& operator=(FreeSlotStore&&) noexcept = default;

    [[nodiscard]] pointer allocate(size_t n = 1);

    void deallocate(pointer p, size_t n = 1) noexcept;

    // Metrics
    size_t free_size() const noexcept;

    // Transfer APIs. noexcept when the backends' transfer ops allocate nothing.
    void transfer_free(FreeSlotStore& from) noexcept(FreeStore::nothrow_transfer);
    void transfer_all(FreeSlotStore& from) noexcept(FreeStore::nothrow_transfer &&
                                                    Alloc::nothrow_transfer);

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

// PoolAllocator combines a block store with a free-slot store
template <typename T, size_t BlockSize = 4096, TransferMode Mode = TransferMode::Fast>
class PoolAllocator
    : public pool_allocator_detail::ObjectOpsMixin<PoolAllocator<T, BlockSize, Mode>, T>
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
    PoolAllocator(const PoolAllocator<U, BlockSize, Mode>& other) = delete;
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

    // Get total number of free slots in FreeSlotStore
    inline size_type num_slots_available() const noexcept
    {
        return allocator.free_size();
    }

    // Get number of slots in BumpBlock of BlockStore
    inline size_type num_bump_available() const noexcept
    {
        return allocator.parent.bump_remaining();
    }

    // Transfer free slots from another allocator.
    // Locks this allocator's spinlock (destination). The caller must be the owning
    // thread of `from` (source) — no lock is taken on the source.
    void transfer_free(PoolAllocator<T, BlockSize, Mode>& from) noexcept(Mode ==
                                                                         TransferMode::Noexcept);

    // Transfer all memory blocks and free slots from another allocator.
    // Locks this allocator's spinlock (destination). The caller must be the owning
    // thread of `from` (source) — no lock is taken on the source.
    void transfer_all(PoolAllocator<T, BlockSize, Mode>& from) noexcept(Mode ==
                                                                        TransferMode::Noexcept);

  private:
    // Decode the public Mode enum into a backend type once, then compose a
    // free-slot store over a block store, both parameterized on that backend.
    using Backend = typename pool_allocator_detail::backend_for<Mode, T>::type;
    using BumpAlloc = pool_allocator_detail::BlockStore<T, std::allocator<T>, BlockSize, Backend>;
    using ComboAlloc = pool_allocator_detail::FreeSlotStore<T, BumpAlloc, Backend>;

    // No explicit export/import API; transfer functions call underlying allocator ops directly
    ComboAlloc allocator; // owns BlockAlloc internally and free list on top

    // Spinlock protecting this allocator as a transfer destination, used for
    // BOTH transfer modes. A spinlock (not std::mutex) because its lock/unlock
    // are noexcept: that is what lets transfer_all/transfer_free be noexcept
    // under TransferMode::Noexcept (std::mutex::lock may throw). Fast mode uses
    // the same spinlock; its transfer can still throw, but from the vector
    // splice, never from the lock. The critical section is only a few pointer
    // splices. Only taken on the destination side; the source is assumed to be
    // accessed only by its owning thread.
    std::atomic<bool> transfer_lock{false};

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
template <typename T, size_t BlockSize, TransferMode Mode>
inline bool
operator==(const PoolAllocator<T, BlockSize, Mode>& a,
           const PoolAllocator<T, BlockSize, Mode>& b) noexcept
{
    return &a == &b;
}

template <typename T, size_t BlockSize, TransferMode Mode>
inline bool
operator!=(const PoolAllocator<T, BlockSize, Mode>& a,
           const PoolAllocator<T, BlockSize, Mode>& b) noexcept
{
    return !(a == b);
}

// include the implementation file
#include "pool_allocator.tcc" // IWYU pragma: export
