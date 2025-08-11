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

// ==== pool_allocator_detail helpers: BumpBlock, BumpAllocator, StackAllocator ====
namespace pool_allocator_detail {

template <typename Pointer>
void BumpBlock<Pointer>::init(Pointer start, size_t count)
{
    this->next = start;
    this->end = start + count;
}

template <typename Pointer>
void BumpBlock<Pointer>::reset() noexcept
{
    next = nullptr;
    end = nullptr;
}

template <typename Pointer>
bool BumpBlock<Pointer>::empty() const noexcept
{
    return next == end;
}

template <typename Pointer>
size_t BumpBlock<Pointer>::remaining() const noexcept
{
    return static_cast<size_t>(end - next);
}

template <typename Pointer>
Pointer BumpBlock<Pointer>::allocate_one() noexcept
{
    if (empty()) return nullptr;
    Pointer p = next;
    ++next;
    return p;
}

template <typename Pointer>
template <typename Vec>
void BumpBlock<Pointer>::export_remaining(Vec& out)
{
    if (empty()) return;
    out.reserve(out.size() + remaining());
    while (next != end) {
        out.push_back(next++);
    }
    reset();
}

template <typename T, typename Alloc, size_t BlockSize>
BumpAllocator<T, Alloc, BlockSize>::BumpAllocator(Alloc&& alloc) noexcept
    : parent(std::forward<Alloc>(alloc)) {}

template <typename T, typename Alloc, size_t BlockSize>
typename BumpAllocator<T, Alloc, BlockSize>::pointer
BumpAllocator<T, Alloc, BlockSize>::allocate(size_t n)
{
    if (n != 1) return parent.allocate(n);
    if (bump.empty()) {
        const size_t count = BlockSize / sizeof(value_type);
        block_pointer p = parent.allocate(count);
        blocks.push_back(p);
        bump.init(p, count);
    }
    return bump.allocate_one();
}

template <typename T, typename Alloc, size_t BlockSize>
void BumpAllocator<T, Alloc, BlockSize>::deallocate(pointer p, size_t n) noexcept
{
    if (n != 1) parent.deallocate(p, n);
}

template <typename T, typename Alloc, size_t BlockSize>
BumpAllocator<T, Alloc, BlockSize>::~BumpAllocator() noexcept
{
    for (auto& block : blocks) {
        const size_t count = BlockSize / sizeof(value_type);
        parent.deallocate(block, count);
    }
    blocks.clear();
    bump.reset();
}

template <typename T, typename Alloc, size_t BlockSize>
size_t BumpAllocator<T, Alloc, BlockSize>::allocated_bytes() const noexcept
{
    return blocks.size() * BlockSize;
}

template <typename T, typename Alloc, size_t BlockSize>
size_t BumpAllocator<T, Alloc, BlockSize>::bump_remaining() const noexcept
{
    return bump.remaining();
}

template <typename T, typename Alloc, size_t BlockSize>
template <class VecSlots, class VecBlocks>
void BumpAllocator<T, Alloc, BlockSize>::export_all(VecSlots& out_free_slots, VecBlocks& out_blocks)
{
    bump.export_remaining(out_free_slots);
    std::swap(out_blocks, blocks);
    bump.reset();
}

template <typename T, typename Alloc, size_t BlockSize>
template <class VecBlocks>
void BumpAllocator<T, Alloc, BlockSize>::import_blocks(VecBlocks&& in_blocks)
{
    blocks.insert(blocks.end(), std::make_move_iterator(in_blocks.begin()),
                  std::make_move_iterator(in_blocks.end()));
}

template <typename T, typename Alloc>
StackAllocator<T, Alloc>::StackAllocator(Alloc&& alloc) noexcept
    : parent(std::forward<Alloc>(alloc)) {}

template <typename T, typename Alloc>
typename StackAllocator<T, Alloc>::pointer
StackAllocator<T, Alloc>::allocate(size_t n)
{
    if (n != 1) return parent.allocate(n);
    if (free_slots.empty()) return parent.allocate(n);
    pointer p = free_slots.back();
    free_slots.pop_back();
    return p;
}

template <typename T, typename Alloc>
void StackAllocator<T, Alloc>::deallocate(pointer p, size_t n) noexcept
{
    if (n != 1) { parent.deallocate(p, n); return; }
    free_slots.push_back(p);
}

template <typename T, typename Alloc>
size_t StackAllocator<T, Alloc>::free_size() const noexcept { return free_slots.size(); }

template <typename T, typename Alloc>
template <class Vec>
void StackAllocator<T, Alloc>::export_free(Vec& out) noexcept
{
    std::swap(out, free_slots);
}

template <typename T, typename Alloc>
template <class Vec>
void StackAllocator<T, Alloc>::import_free(Vec&& in)
{
    free_slots.insert(free_slots.end(), std::make_move_iterator(in.begin()),
                      std::make_move_iterator(in.end()));
}

template <typename T, typename Alloc>
template <class VecSlots, class VecBlocks>
void StackAllocator<T, Alloc>::export_all(VecSlots& out_slots, VecBlocks& out_blocks)
{
    export_free(out_slots);
    parent.export_all(out_slots, out_blocks);
}

template <typename T, typename Alloc>
template <class VecBlocks>
void StackAllocator<T, Alloc>::import_blocks(VecBlocks&& in_blocks)
{
    parent.import_blocks(std::forward<VecBlocks>(in_blocks));
}

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
typename PoolAllocator<T, BlockSize>::ExportedAlloc
PoolAllocator<T, BlockSize>::export_free()
{
    ExportedAlloc exported;
    // Export only free slots from the top free-list layer
    allocator.export_free(exported.free_slots);
    return exported;
}

template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::ExportedAlloc
PoolAllocator<T, BlockSize>::export_all()
{
    ExportedAlloc exported;
    // Then export all blocks and the remaining bump slots from the bump layer
    allocator.export_all(exported.free_slots, exported.memory_blocks);
    return exported;
}

template <typename T, size_t BlockSize>
void
PoolAllocator<T, BlockSize>::import(ExportedAlloc exported)
{
    // Import free slots into free-list layer
    allocator.import_free(exported.free_slots);
    // Import blocks ownership into bump layer (no bump state)
    allocator.import_blocks(exported.memory_blocks);
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
