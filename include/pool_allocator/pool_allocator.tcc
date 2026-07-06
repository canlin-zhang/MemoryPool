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

#include <cstring> // for std::memcpy
#include <limits>
#include <new> // for std::align_val_t

// ==== pool_allocator_detail helpers: BumpBlock, BumpAllocator, StackAllocator ====
namespace pool_allocator_detail
{

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
template <typename Vec>
void
BumpBlock<Pointer>::export_remaining(Vec& out)
{
    if (empty())
        return;
    out.reserve(out.size() + remaining());
    while (next != end)
    {
        out.push_back(next++);
    }
    reset();
}

template <typename T, typename Alloc, size_t BlockSize>
BumpAllocator<T, Alloc, BlockSize>::BumpAllocator(Alloc&& alloc) noexcept
    : parent(std::forward<Alloc>(alloc))
{
}

template <typename T, typename Alloc, size_t BlockSize>
typename BumpAllocator<T, Alloc, BlockSize>::pointer
BumpAllocator<T, Alloc, BlockSize>::allocate(size_t n)
{
    if (n != 1)
        return parent.allocate(n);
    if (bump.empty())
    {
        const size_t count = BlockSize / sizeof(value_type);
        block_pointer p = parent.allocate(count);
        blocks.push_back(p);
        bump.init(p, count);
    }
    return bump.allocate_one();
}

template <typename T, typename Alloc, size_t BlockSize>
void
BumpAllocator<T, Alloc, BlockSize>::deallocate(pointer p, size_t n) noexcept
{
    if (n != 1)
        parent.deallocate(p, n);
}

template <typename T, typename Alloc, size_t BlockSize>
BumpAllocator<T, Alloc, BlockSize>::~BumpAllocator() noexcept
{
    for (auto& block : blocks)
    {
        const size_t count = BlockSize / sizeof(value_type);
        parent.deallocate(block, count);
    }
    blocks.clear();
    bump.reset();
}

template <typename T, typename Alloc, size_t BlockSize>
size_t
BumpAllocator<T, Alloc, BlockSize>::allocated_bytes() const noexcept
{
    return blocks.size() * BlockSize;
}

template <typename T, typename Alloc, size_t BlockSize>
size_t
BumpAllocator<T, Alloc, BlockSize>::bump_remaining() const noexcept
{
    return bump.remaining();
}

template <typename T, typename Alloc, size_t BlockSize>
auto
BumpAllocator<T, Alloc, BlockSize>::export_all() -> std::pair<FreeSlotsContainer, BlocksContainer>
{
    FreeSlotsContainer out_free_slots;
    out_free_slots.reserve(bump.remaining());
    bump.export_remaining(out_free_slots);

    BlocksContainer out_blocks;
    out_blocks.swap(blocks);

    return {std::move(out_free_slots), std::move(out_blocks)};
}

template <typename T, typename Alloc, size_t BlockSize>
void
BumpAllocator<T, Alloc, BlockSize>::import_blocks(BlocksContainer&& in_blocks)
{
    blocks.insert(blocks.end(), in_blocks.begin(), in_blocks.end());
}

template <typename T, typename Alloc>
StackAllocator<T, Alloc>::StackAllocator(Alloc&& alloc) noexcept
    : parent(std::forward<Alloc>(alloc))
{
}

template <typename T, typename Alloc>
typename StackAllocator<T, Alloc>::pointer
StackAllocator<T, Alloc>::allocate(size_t n)
{
    if (n != 1)
        return parent.allocate(n);
    if constexpr (use_intrusive)
    {
        if (free_store.free_head == nullptr)
            return parent.allocate(n);
        pointer p = free_store.free_head;
        void* next;
        std::memcpy(&next, static_cast<const void*>(p), sizeof(void*));
        free_store.free_head = static_cast<pointer>(next);
        --free_store.free_count;
        return p;
    }
    else
    {
        if (free_store.free_slots.empty())
            return parent.allocate(n);
        pointer p = free_store.free_slots.back();
        free_store.free_slots.pop_back();
        return p;
    }
}

template <typename T, typename Alloc>
void
StackAllocator<T, Alloc>::deallocate(pointer p, size_t n) noexcept
{
    if (n != 1)
    {
        parent.deallocate(p, n);
        return;
    }
    if constexpr (use_intrusive)
    {
        std::memcpy(static_cast<void*>(p), &free_store.free_head, sizeof(void*));
        free_store.free_head = p;
        ++free_store.free_count;
    }
    else
    {
        free_store.free_slots.push_back(p);
    }
}

template <typename T, typename Alloc>
size_t
StackAllocator<T, Alloc>::free_size() const noexcept
{
    if constexpr (use_intrusive)
        return free_store.free_count;
    else
        return free_store.free_slots.size();
}

template <typename T, typename Alloc>
void
StackAllocator<T, Alloc>::transfer_free(StackAllocator& from)
{
    if (&from == this)
        return;
    if constexpr (use_intrusive)
    {
        if (from.free_store.free_head == nullptr)
            return;
        pointer tail = from.free_store.free_head;
        pointer next;
        std::memcpy(&next, static_cast<const void*>(tail), sizeof(void*));
        while (next != nullptr)
        {
            tail = next;
            std::memcpy(&next, static_cast<const void*>(tail), sizeof(void*));
        }
        std::memcpy(static_cast<void*>(tail), &free_store.free_head, sizeof(void*));
        free_store.free_head = from.free_store.free_head;
        free_store.free_count += from.free_store.free_count;
        from.free_store.free_head = nullptr;
        from.free_store.free_count = 0;
    }
    else
    {
        auto& v = free_store.free_slots;
        auto& fv = from.free_store.free_slots;
        v.insert(v.end(), fv.begin(), fv.end());
        fv.clear();
    }
}

template <typename T, typename Alloc>
void
StackAllocator<T, Alloc>::transfer_all(StackAllocator& from)
{
    if (&from == this)
        return;
    transfer_free(from);
    auto [fs, blocks] = from.parent.export_all();
    if constexpr (use_intrusive)
    {
        for (pointer p : fs)
            deallocate(p);
    }
    else
    {
        free_store.free_slots.insert(free_store.free_slots.end(), fs.begin(), fs.end());
    }
    parent.import_blocks(std::move(blocks));
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

} // namespace pool_allocator_detail

// Default constructor
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize>::PoolAllocator() noexcept
{
    // Check block size vs T size; blocks must hold at least one T
    static_assert(BlockSize / sizeof(T) > 0, "Block size is too small for the type T");
}

// Destructor
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize>::~PoolAllocator() noexcept = default;

template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::transfer_all(PoolAllocator<T, BlockSize>& from)
{
    assert(&from != this && "Cannot import directly from self");
    std::lock_guard<std::mutex> lock(transfer_mutex);
    pool_allocator_detail::TransferAccessMark dst_mark(in_transfer_);
    pool_allocator_detail::TransferAccessMark src_mark(from.in_transfer_);
    allocator.transfer_all(from.allocator);
}

template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::transfer_free(PoolAllocator<T, BlockSize>& from)
{
    assert(&from != this && "Cannot import directly from self");
    std::lock_guard<std::mutex> lock(transfer_mutex);
    pool_allocator_detail::TransferAccessMark dst_mark(in_transfer_);
    pool_allocator_detail::TransferAccessMark src_mark(from.in_transfer_);
    allocator.transfer_free(from.allocator);
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
    return this->template make_unique_with<Deleter>(Deleter{this}, std::forward<Args>(args)...);
}
