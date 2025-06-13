#pragma once

template <typename T, size_t BlockSize>
class PoolAllocator;

// Custom deleter for unique_ptr use
template <typename T, size_t BlockSize>
struct PoolDeleter
{
    PoolAllocator<T, BlockSize>* allocator = nullptr;

    void operator()(T* ptr) const noexcept
    {
        if (allocator)
        {
            ptr->~T();
            allocator->deallocate(ptr, 1);
        }
    }
};
