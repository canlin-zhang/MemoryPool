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
 * 2. Added unique_ptr its necessary helper functions
 */

#include "pool_allocator.h"

#ifndef POOL_ALLOCATOR_TCC
#define POOL_ALLOCATOR_TCC

// Default constructor
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize>::PoolAllocator() noexcept
{
    // Initialize all pointers to nullptr
    currentBlock_ = nullptr;
    currentSlot_ = nullptr;
    lastSlot_ = nullptr;
    freeSlots_ = nullptr;
}

// Copy constructor - do nothing
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize>::PoolAllocator(const PoolAllocator &other) noexcept
{
}

// Move constructor
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize>::PoolAllocator(PoolAllocator &&other) noexcept
{
    currentBlock_ = other.currentBlock_;
    currentSlot_ = other.currentSlot_;
    lastSlot_ = other.lastSlot_;
    freeSlots_ = other.freeSlots_;
    other.currentBlock_ = nullptr;
    other.currentSlot_ = nullptr;
    other.lastSlot_ = nullptr;
    other.freeSlots_ = nullptr;
}

// Templated copy - do nothing
template <typename T, size_t BlockSize>
template <class U>
PoolAllocator<T, BlockSize>::PoolAllocator(const PoolAllocator<U, BlockSize> &other) noexcept
{
}

// Destructor
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize>::~PoolAllocator() noexcept
{
    // Free all allocated blocks
    while (currentBlock_ != nullptr)
    {
        Slot_ *nextBlock = currentBlock_->next;
        ::operator delete[](currentBlock_);
        currentBlock_ = nextBlock;
    }
}

// Move assignment operator
template <typename T, size_t BlockSize>
PoolAllocator<T, BlockSize> &
PoolAllocator<T, BlockSize>::operator=(PoolAllocator &&other) noexcept
{
    if (this != &other)
    {
        // Swap the contents of the allocators
        std::swap(currentBlock_, other.currentBlock_);
        std::swap(currentSlot_, other.currentSlot_);
        std::swap(lastSlot_, other.lastSlot_);
        std::swap(freeSlots_, other.freeSlots_);
        // Reset the other allocator's pointers
        other.currentBlock_ = nullptr;
        other.currentSlot_ = nullptr;
        other.lastSlot_ = nullptr;
        other.freeSlots_ = nullptr;
    }

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

// Allocation functions
// Allocate a memory block
template <typename T, size_t BlockSize>
void PoolAllocator<T, BlockSize>::allocateBlock()
{
    // If we don't have a current block, allocate a new one
    if (currentBlock_ == nullptr)
    {
        // A new block of memory
        char *raw = reinterpret_cast<char *>(::operator new[](BlockSize));
        // Define the block header
        Slot_ *blockHead = reinterpret_cast<Slot_ *>(raw);
        blockHead->next = nullptr;
        currentBlock_ = blockHead;

        // Find aligned region for <T>
        // Non-aligned pointer (first usable byte)
        char *beginPtr = raw + sizeof(Slot_);
        size_t usableSpace = BlockSize - sizeof(Slot_);
        void *beginPtrLval = static_cast<void *>(beginPtr);

        // Align pointer
        // std::align will adjust usable space and first
        void *alignedPtr = std::align(alignof(T), sizeof(T), beginPtrLval, usableSpace);
        if (alignedPtr == nullptr)
        {
            throw std::bad_alloc(); // Allocation failed
        }
        else
        {
            // Calculate usable slots
            size_t num_slots = (usableSpace / sizeof(T));
            if (num_slots == 0)
            {
                throw std::bad_alloc(); // Not enough space for even one object
            }
            // Set current slot to the aligned pointer
            currentSlot_ = reinterpret_cast<Slot_ *>(alignedPtr);
            // Last slot is 1 next to the end of aligned block
            lastSlot_ = reinterpret_cast<Slot_ *>(alignedPtr) + num_slots;
            // Also initialize free slots to nullptr
            freeSlots_ = nullptr;
        }
    }
    else
    {
        // If we have a current block, we need to allocate a new one
        char *raw = reinterpret_cast<char *>(::operator new[](BlockSize));
        Slot_ *blockHead = reinterpret_cast<Slot_ *>(raw);
        blockHead->next = currentBlock_;
        currentBlock_ = blockHead;

        // Find aligned region for <T>
        char *beginPtr = raw + sizeof(Slot_);
        size_t usableSpace = BlockSize - sizeof(Slot_);
        void *beginPtrLval = static_cast<void *>(beginPtr);

        void *alignedPtr = std::align(alignof(T), sizeof(T), beginPtrLval, usableSpace);
        if (alignedPtr == nullptr)
        {
            throw std::bad_alloc(); // Allocation failed
        }
        else
        {
            size_t num_slots = (usableSpace / sizeof(T));
            if (num_slots == 0)
            {
                throw std::bad_alloc(); // Not enough space for even one object
            }
            currentSlot_ = reinterpret_cast<Slot_ *>(alignedPtr);
            lastSlot_ = reinterpret_cast<Slot_ *>(alignedPtr) + num_slots;
            // Don't touch free slots, they are handled by deallocation.
        }
    }
}

// Allocate a single object
template <typename T, size_t BlockSize>
typename PoolAllocator<T, BlockSize>::pointer
PoolAllocator<T, BlockSize>::allocate(size_type n)
{
    // For single object allocation, use our implementation
    if (n == 1)
    {
        if (freeSlots_ != nullptr)
        {
            pointer result = reinterpret_cast<pointer>(freeSlots_);
            freeSlots_ = freeSlots_->next;
            return result;
        }
        else
        {
            if (currentSlot_ >= lastSlot_)
                allocateBlock();
            return reinterpret_cast<pointer>(currentSlot_++);
        }
    }
    // For multiple object, wrap around std::allocator
    else if (n > 1)
    {
        return std::allocator<T>().allocate(n);
    }
    // n can't be zero.
    else
    {
        throw std::bad_alloc(); // Handle invalid allocation request
    }
}

// Deallocate a single object
template <typename T, size_t BlockSize>
void PoolAllocator<T, BlockSize>::deallocate(pointer p, size_type n)
{
    if (p == nullptr)
    {
        return; // No need to deallocate null pointers
    }
    else if (n == 1)
    {
        reinterpret_cast<Slot_ *>(p)->next = freeSlots_;
        freeSlots_ = reinterpret_cast<Slot_ *>(p);
    }
    else if (n > 1)
    {
        // For multiple objects, use std::allocator to deallocate
        // Current implementation always assume p is a std::allocator<T> pointer
        // and not a PoolAllocator pointer.
        std::allocator<T>().deallocate(p, n);
    }
    else
    {
        throw std::bad_alloc(); // Handle invalid deallocation request
    }
}

// Construct an object in the allocated memory
template <typename T, size_t BlockSize>
template <class U, class... Args>
void PoolAllocator<T, BlockSize>::construct(U *p, Args &&...args)
{
    // Use placement new to construct the object in the allocated memory
    new (p) U(std::forward<Args>(args)...);
}
// Destroy an object in the allocated memory
template <typename T, size_t BlockSize>
template <class U>
void PoolAllocator<T, BlockSize>::destroy(U *p)
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
    if (allocator && ptr)
    {
        allocator->destroy(ptr);
        allocator->deallocate(ptr, 1); // Deallocate a single object
    }
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
    if (p == nullptr)
    {
        return; // No need to delete null pointers
    }
    // Call the destructor of the object
    destroy(p);
    // Deallocate the memory
    deallocate(p, 1); // Deallocate a single object
}

#endif // POOL_ALLOCATOR_TCC