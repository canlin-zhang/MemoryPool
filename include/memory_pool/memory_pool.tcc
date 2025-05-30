#include "memory_pool.h"
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

#ifndef MEMORY_POOL_TCC
#define MEMORY_POOL_TCC

// Define Slot_
template <typename T, size_t BlockSize>
union MemoryPool<T, BlockSize>::Slot_
{
  // Pointer to the next free slot
  Slot_ *next;
  // The actual data
  typename std::aligned_storage<sizeof(T), alignof(T)>::type data;
};

template <typename T, size_t BlockSize>
inline typename MemoryPool<T, BlockSize>::size_type
MemoryPool<T, BlockSize>::padPointer(data_pointer_ p, size_type align)
    const noexcept
{
  uintptr_t result = reinterpret_cast<uintptr_t>(p);
  return ((align - result) % align);
}

template <typename T, size_t BlockSize>
MemoryPool<T, BlockSize>::MemoryPool() noexcept
{
  currentBlock_ = nullptr;
  currentSlot_ = nullptr;
  lastSlot_ = nullptr;
  freeSlots_ = nullptr;
}

template <typename T, size_t BlockSize>
MemoryPool<T, BlockSize>::MemoryPool(const MemoryPool &memoryPool) noexcept : MemoryPool()
{
}

template <typename T, size_t BlockSize>
MemoryPool<T, BlockSize>::MemoryPool(MemoryPool &&memoryPool) noexcept
{
  currentBlock_ = memoryPool.currentBlock_;
  memoryPool.currentBlock_ = nullptr;
  currentSlot_ = memoryPool.currentSlot_;
  lastSlot_ = memoryPool.lastSlot_;
  freeSlots_ = memoryPool.freeSlots;
}

template <typename T, size_t BlockSize>
template <class U>
MemoryPool<T, BlockSize>::MemoryPool(const MemoryPool<U> &memoryPool) noexcept : MemoryPool()
{
}

template <typename T, size_t BlockSize>
MemoryPool<T, BlockSize> &
MemoryPool<T, BlockSize>::operator=(MemoryPool &&memoryPool) noexcept
{
  if (this != &memoryPool)
  {
    std::swap(currentBlock_, memoryPool.currentBlock_);
    currentSlot_ = memoryPool.currentSlot_;
    lastSlot_ = memoryPool.lastSlot_;
    freeSlots_ = memoryPool.freeSlots;
  }
  return *this;
}

template <typename T, size_t BlockSize>
MemoryPool<T, BlockSize>::~MemoryPool() noexcept
{
  slot_pointer_ curr = currentBlock_;
  while (curr != nullptr)
  {
    slot_pointer_ prev = curr->next;
    operator delete(reinterpret_cast<void *>(curr));
    curr = prev;
  }
}

template <typename T, size_t BlockSize>
inline typename MemoryPool<T, BlockSize>::pointer
MemoryPool<T, BlockSize>::address(reference x)
    const noexcept
{
  return &x;
}

template <typename T, size_t BlockSize>
inline typename MemoryPool<T, BlockSize>::const_pointer
MemoryPool<T, BlockSize>::address(const_reference x)
    const noexcept
{
  return &x;
}

template <typename T, size_t BlockSize>
void MemoryPool<T, BlockSize>::allocateBlock()
{
  // Allocate space for the new block and store a pointer to the previous one
  data_pointer_ newBlock = reinterpret_cast<data_pointer_>(operator new(BlockSize));
  reinterpret_cast<slot_pointer_>(newBlock)->next = currentBlock_;
  currentBlock_ = reinterpret_cast<slot_pointer_>(newBlock);
  // Pad block body to staisfy the alignment requirements for elements
  data_pointer_ body = newBlock + sizeof(slot_pointer_);
  size_type bodyPadding = padPointer(body, alignof(slot_type_));
  currentSlot_ = reinterpret_cast<slot_pointer_>(body + bodyPadding);
  lastSlot_ = reinterpret_cast<slot_pointer_>(newBlock + BlockSize - sizeof(slot_type_) + 1);
}

template <typename T, size_t BlockSize>
inline typename MemoryPool<T, BlockSize>::pointer
MemoryPool<T, BlockSize>::allocate(size_type n)
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

template <typename T, size_t BlockSize>
inline void
MemoryPool<T, BlockSize>::deallocate(pointer p, size_type n)
{
  if (p == nullptr)
  {
    return; // No need to deallocate null pointers
  }
  else if (n == 1)
  {
    reinterpret_cast<slot_pointer_>(p)->next = freeSlots_;
    freeSlots_ = reinterpret_cast<slot_pointer_>(p);
  }
  else if (n > 1)
  {
    // For multiple objects, use std::allocator to deallocate
    // Current implementation always assume p is a std::allocator<T> pointer
    // and not a MemoryPool pointer.
    std::allocator<T>().deallocate(p, n);
  }
  else
  {
    throw std::bad_alloc(); // Handle invalid deallocation request
  }
}

template <typename T, size_t BlockSize>
inline typename MemoryPool<T, BlockSize>::size_type
MemoryPool<T, BlockSize>::max_size()
    const noexcept
{
  size_type maxBlocks = -1 / BlockSize;
  return (BlockSize - sizeof(data_pointer_)) / sizeof(slot_type_) * maxBlocks;
}

template <typename T, size_t BlockSize>
template <class U, class... Args>
inline void
MemoryPool<T, BlockSize>::construct(U *p, Args &&...args)
{
  new (p) U(std::forward<Args>(args)...);
}

template <typename T, size_t BlockSize>
template <class U>
inline void
MemoryPool<T, BlockSize>::destroy(U *p)
{
  p->~U();
}

template <typename T, size_t BlockSize>
template <class... Args>
inline typename MemoryPool<T, BlockSize>::pointer
MemoryPool<T, BlockSize>::newElement(Args &&...args)
{
  pointer result = allocate();
  construct<value_type>(result, std::forward<Args>(args)...);
  return result;
}

template <typename T, size_t BlockSize>
template <class... Args>
inline typename MemoryPool<T, BlockSize>::unique_ptr
MemoryPool<T, BlockSize>::make_unique(Args &&...args)
{
  // Allocate a new element using the memory pool
  pointer raw = allocate();
  // Basically a reimplementation of std::new
  // we need handling of failed constructors
  try
  {
    construct(raw, std::forward<Args>(args)...);
    return std::unique_ptr<T, Deleter>{
        raw,
        Deleter{this}}; // Return a unique pointer with a custom deleter
  }
  catch (...)
  {
    deallocate(raw); // Clean up if construction fails
    throw;           // Re-throw the exception
  }
}

template <typename T, size_t BlockSize>
inline typename MemoryPool<T, BlockSize>::unique_ptr MemoryPool<T, BlockSize>::move(unique_ptr src)
{
  // Allocate first
  pointer raw = allocate();
  // Construct the object from source object
  construct(raw, std::move(*src));
  // Deallocate the source object
  destroy(src.get());
  deallocate(src.release());

  return unique_ptr(
      raw, Deleter{this});
}

template <typename T, size_t BlockSize>
inline void
MemoryPool<T, BlockSize>::deleteElement(pointer p)
{
  if (p != nullptr)
  {
    p->~value_type();
    deallocate(p);
  }
}

template <typename T, size_t BlockSize>
void MemoryPool<T, BlockSize>::Deleter::operator()(pointer ptr) const
{
  pool->deleteElement(ptr);
}

#endif // MEMORY_POOL_TCC
