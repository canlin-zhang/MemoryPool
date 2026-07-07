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

#include <cstring> // for std::memcpy (SlotList inline links)
#include <limits>
#include <new> // for std::align_val_t

// ==== pool_allocator_detail helpers: BumpBlock, SlotList, BlockStore, FreeSlotStore ====
namespace pool_allocator_detail
{

template <typename T>
void
SlotList<T>::push(T* slot) noexcept
{
    // Store the current head (a T*) into the freed slot's own bytes.
    std::memcpy(static_cast<void*>(slot), &head, sizeof(T*));
    head = slot;
    if (tail == nullptr)
        tail = slot;
    ++count;
}

template <typename T>
T*
SlotList<T>::pop() noexcept
{
    if (head == nullptr)
        return nullptr;
    T* p = head;
    T* next;
    std::memcpy(&next, static_cast<const void*>(p), sizeof(T*));
    head = next;
    if (head == nullptr)
        tail = nullptr;
    --count;
    return p;
}

template <typename T>
void
SlotList<T>::splice(SlotList& from) noexcept
{
    if (from.head == nullptr)
        return;
    if (tail == nullptr)
    {
        head = from.head;
        tail = from.tail;
    }
    else
    {
        std::memcpy(static_cast<void*>(tail), &from.head, sizeof(T*));
        tail = from.tail;
    }
    count += from.count;
    from.head = from.tail = nullptr;
    from.count = 0;
}

template <typename Pointer>
void
BumpBlock<Pointer>::init(Pointer start, size_t count)
{
    this->next = start;
    this->end = start + count;
}

template <typename Pointer>
void
BumpBlock<Pointer>::reset() noexcept
{
    next = nullptr;
    end = nullptr;
}

template <typename Pointer>
bool
BumpBlock<Pointer>::empty() const noexcept
{
    return next == end;
}

template <typename Pointer>
size_t
BumpBlock<Pointer>::remaining() const noexcept
{
    return static_cast<size_t>(end - next);
}

template <typename Pointer>
Pointer
BumpBlock<Pointer>::allocate_one() noexcept
{
    if (empty())
        return nullptr;
    Pointer p = next;
    ++next;
    return p;
}

template <typename Pointer>
std::pair<Pointer, Pointer>
BumpBlock<Pointer>::take_range() noexcept
{
    std::pair<Pointer, Pointer> r{next, end};
    reset();
    return r;
}

template <typename T, typename Alloc, size_t BlockSize, typename Blocks>
BlockStore<T, Alloc, BlockSize, Blocks>::BlockStore(Alloc&& alloc) noexcept
    : parent(std::forward<Alloc>(alloc))
{
}

template <typename T, typename Alloc, size_t BlockSize, typename Blocks>
typename BlockStore<T, Alloc, BlockSize, Blocks>::pointer
BlockStore<T, Alloc, BlockSize, Blocks>::allocate(size_t n)
{
    if (n != 1)
        return parent.allocate(n);
    if (bump.empty())
    {
        // The backend may claim some slots of each block for its own linkage
        // (SlotList stores its link in slot 0; SlotVector claims none). Carve the
        // bump range from the first slot the backend leaves usable.
        constexpr size_t overhead = Blocks::slots_per_block_overhead;
        static_assert(BlockSize / sizeof(value_type) > overhead,
                      "block must hold at least one usable slot after backend link overhead");
        const size_t count = BlockSize / sizeof(value_type);
        blocks.reserve(1); // grow the block chain before we own the raw block (strong guarantee)
        block_pointer p = parent.allocate(count);
        blocks.push(p); // capacity reserved above, so this cannot throw
        bump.init(p + overhead, count - overhead);
    }
    return bump.allocate_one();
}

template <typename T, typename Alloc, size_t BlockSize, typename Blocks>
void
BlockStore<T, Alloc, BlockSize, Blocks>::deallocate(pointer p, size_t n) noexcept
{
    if (n != 1)
        parent.deallocate(p, n);
}

template <typename T, typename Alloc, size_t BlockSize, typename Blocks>
BlockStore<T, Alloc, BlockSize, Blocks>::~BlockStore() noexcept
{
    const size_t count = BlockSize / sizeof(value_type);
    while (!blocks.empty())
        parent.deallocate(blocks.pop(), count);
    bump.reset();
}

template <typename T, typename Alloc, size_t BlockSize, typename Blocks>
size_t
BlockStore<T, Alloc, BlockSize, Blocks>::allocated_bytes() const noexcept
{
    return blocks.size() * BlockSize;
}

template <typename T, typename Alloc, size_t BlockSize, typename Blocks>
size_t
BlockStore<T, Alloc, BlockSize, Blocks>::bump_remaining() const noexcept
{
    return bump.remaining();
}

template <typename T, typename Alloc, size_t BlockSize, typename Blocks>
std::pair<typename BlockStore<T, Alloc, BlockSize, Blocks>::pointer,
          typename BlockStore<T, Alloc, BlockSize, Blocks>::pointer>
BlockStore<T, Alloc, BlockSize, Blocks>::take_bump_range() noexcept
{
    return bump.take_range();
}

template <typename T, typename Alloc, size_t BlockSize, typename Blocks>
void
BlockStore<T, Alloc, BlockSize, Blocks>::transfer_blocks(BlockStore& from) noexcept(
    Blocks::nothrow_transfer)
{
    if (&from == this)
        return;
    blocks.splice(from.blocks);
}

template <typename T, typename Alloc, typename FreeStore>
FreeSlotStore<T, Alloc, FreeStore>::FreeSlotStore(Alloc&& alloc) noexcept
    : parent(std::forward<Alloc>(alloc))
{
}

template <typename T, typename Alloc, typename FreeStore>
typename FreeSlotStore<T, Alloc, FreeStore>::pointer
FreeSlotStore<T, Alloc, FreeStore>::allocate(size_t n)
{
    if (n != 1)
        return parent.allocate(n);
    if (free_store.empty())
        return parent.allocate(n);
    return free_store.pop();
}

template <typename T, typename Alloc, typename FreeStore>
void
FreeSlotStore<T, Alloc, FreeStore>::deallocate(pointer p, size_t n) noexcept
{
    if (n != 1)
    {
        parent.deallocate(p, n);
        return;
    }
    free_store.push(p);
}

template <typename T, typename Alloc, typename FreeStore>
size_t
FreeSlotStore<T, Alloc, FreeStore>::free_size() const noexcept
{
    return free_store.size();
}

template <typename T, typename Alloc, typename FreeStore>
void
FreeSlotStore<T, Alloc, FreeStore>::transfer_free(FreeSlotStore& from) noexcept(
    FreeStore::nothrow_transfer)
{
    if (&from == this)
        return;
    free_store.splice(from.free_store);
}

template <typename T, typename Alloc, typename FreeStore>
void
FreeSlotStore<T, Alloc, FreeStore>::transfer_all(FreeSlotStore& from) noexcept(
    FreeStore::nothrow_transfer && Alloc::nothrow_transfer)
{
    if (&from == this)
        return;
    // Drain the source's half-carved block into this free store, then splice the
    // source's free slots (axis 2) and its blocks (axis 1). No allocation on the
    // list backend, so this is noexcept under Noexcept mode.
    auto [b, e] = from.parent.take_bump_range();
    for (pointer p = b; p != e; ++p)
        free_store.push(p);
    transfer_free(from);
    parent.transfer_blocks(from.parent);
}

// ---- ObjectOpsMixin implementations ----
template <typename Derived, typename T>
template <class U, class... Args>
void
ObjectOpsMixin<Derived, T>::construct(U* p, Args&&... args)
{
    new (p) U(std::forward<Args>(args)...);
}

template <typename Derived, typename T>
template <class U>
void
ObjectOpsMixin<Derived, T>::destroy(U* p) noexcept
{
    p->~U();
}

template <typename Derived, typename T>
typename ObjectOpsMixin<Derived, T>::pointer
ObjectOpsMixin<Derived, T>::new_object()
{
    auto* self = static_cast<Derived*>(this);
    pointer p = self->allocate(1);
    try
    {
        construct(p);
    }
    catch (...)
    {
        self->deallocate(p, 1);
        throw;
    }
    return p;
}

template <typename Derived, typename T>
template <class... Args>
typename ObjectOpsMixin<Derived, T>::pointer
ObjectOpsMixin<Derived, T>::new_object(Args&&... args)
{
    auto* self = static_cast<Derived*>(this);
    pointer p = self->allocate(1);
    try
    {
        construct(p, std::forward<Args>(args)...);
    }
    catch (...)
    {
        self->deallocate(p, 1);
        throw;
    }
    return p;
}

template <typename Derived, typename T>
void
ObjectOpsMixin<Derived, T>::delete_object(pointer p) noexcept
{
    auto* self = static_cast<Derived*>(this);
    destroy(p);
    self->deallocate(p, 1);
}

template <typename Derived, typename T>
template <class Deleter, class... Args>
std::unique_ptr<T, Deleter>
ObjectOpsMixin<Derived, T>::make_unique_with(Deleter del, Args&&... args)
{
    auto* self = static_cast<Derived*>(this);
    pointer raw = self->allocate(1);
    try
    {
        construct(raw, std::forward<Args>(args)...);
    }
    catch (...)
    {
        self->deallocate(raw, 1);
        throw;
    }
    return std::unique_ptr<T, Deleter>(raw, std::move(del));
}

// Debug-only reentrancy tripwire for a transfer participant. In a debug build
// the constructor marks the instance (atomic exchange) and asserts it was not
// already marked, and the destructor clears it, so two overlapping transfers on
// a shared instance trip the assert. The exchange lives inside assert(), so
// under NDEBUG the constructor does nothing and only the destructor's
// store(false) remains -- a no-op on an always-false flag. Transfers are rare
// phase-boundary events, so even that residual store costs nothing.
struct TransferAccessMark
{
    std::atomic<bool>& flag;
    explicit TransferAccessMark(std::atomic<bool>& f)
        : flag(f)
    {
        assert(!flag.exchange(true, std::memory_order_acq_rel) &&
               "PoolAllocator: concurrent transfer on a shared instance -- "
               "transfer_all/transfer_free read the source unlocked, so the "
               "same pool must not be a transfer source/destination on two "
               "threads at once");
    }
    ~TransferAccessMark()
    {
        flag.store(false, std::memory_order_release);
    }
    TransferAccessMark(const TransferAccessMark&) = delete;
    TransferAccessMark& operator=(const TransferAccessMark&) = delete;
};

// noexcept scoped lock over a std::atomic<bool> spinlock. Used instead of
// std::mutex/std::lock_guard so transfer_all/transfer_free stay noexcept under
// TransferMode::Noexcept (std::mutex::lock may throw system_error; atomic ops
// cannot). The critical section is only a few pointer splices, so spinning is
// cheap and transfers are rare phase-boundary events.
struct SpinLockGuard
{
    std::atomic<bool>& lock;
    explicit SpinLockGuard(std::atomic<bool>& l) noexcept
        : lock(l)
    {
        bool expected = false;
        while (!lock.compare_exchange_weak(expected, true, std::memory_order_acquire,
                                           std::memory_order_relaxed))
            expected = false;
    }
    ~SpinLockGuard()
    {
        lock.store(false, std::memory_order_release);
    }
    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;
};

} // namespace pool_allocator_detail

// Default constructor
template <typename T, size_t BlockSize, TransferMode Mode>
PoolAllocator<T, BlockSize, Mode>::PoolAllocator() noexcept
{
    // Check block size vs T size; blocks must hold at least one T
    static_assert(BlockSize / sizeof(T) > 0, "Block size is too small for the type T");
}

// Destructor
template <typename T, size_t BlockSize, TransferMode Mode>
PoolAllocator<T, BlockSize, Mode>::~PoolAllocator() noexcept = default;

template <typename T, size_t BlockSize, TransferMode Mode>
void
PoolAllocator<T, BlockSize, Mode>::transfer_all(PoolAllocator<T, BlockSize, Mode>& from) noexcept(
    Mode == TransferMode::Noexcept)
{
    assert(&from != this && "Cannot import directly from self");
    pool_allocator_detail::SpinLockGuard lock(transfer_lock);
    pool_allocator_detail::TransferAccessMark dst_mark(in_transfer_);
    pool_allocator_detail::TransferAccessMark src_mark(from.in_transfer_);
    allocator.transfer_all(from.allocator);
}

template <typename T, size_t BlockSize, TransferMode Mode>
void
PoolAllocator<T, BlockSize, Mode>::transfer_free(PoolAllocator<T, BlockSize, Mode>& from) noexcept(
    Mode == TransferMode::Noexcept)
{
    assert(&from != this && "Cannot import directly from self");
    pool_allocator_detail::SpinLockGuard lock(transfer_lock);
    pool_allocator_detail::TransferAccessMark dst_mark(in_transfer_);
    pool_allocator_detail::TransferAccessMark src_mark(from.in_transfer_);
    allocator.transfer_free(from.allocator);
}

// Maximum size of the pool
template <typename T, size_t BlockSize, TransferMode Mode>
typename PoolAllocator<T, BlockSize, Mode>::size_type
PoolAllocator<T, BlockSize, Mode>::max_size() const noexcept
{
    // Calculate the maximum number of objects that can be allocated in a block
    return std::numeric_limits<PoolAllocator<T, BlockSize, Mode>::size_type>::max() / sizeof(T);
}

// Unique pointer support
// Deleter for unique_ptr
template <typename T, size_t BlockSize, TransferMode Mode>
template <typename U>
void
PoolAllocator<T, BlockSize, Mode>::Deleter::operator()(U* ptr) const noexcept
{
    static_assert(sizeof(U) > 0, "Deleter cannot be used with incomplete types");
    // Call delete_object on the allocator
    allocator->delete_object(ptr);
}

// Create a unique pointer with a custom deleter
template <typename T, size_t BlockSize, TransferMode Mode>
template <class... Args>
inline std::unique_ptr<T, typename PoolAllocator<T, BlockSize, Mode>::Deleter>
PoolAllocator<T, BlockSize, Mode>::make_unique(Args&&... args)
{
    return this->template make_unique_with<Deleter>(Deleter{this}, std::forward<Args>(args)...);
}
