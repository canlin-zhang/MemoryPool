#include <pool_allocator/pool_allocator.h>
#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <chrono>
#include <random>
#include <algorithm>
#include <iostream>
#include <iomanip> // make sure this is at the top of your file
#include <cassert>

template <typename Alloc>
int64_t run_allocator_benchmark(const std::string &label, Alloc &allocator, std::vector<typename Alloc::pointer> &ptr_vec)
{
    using Traits = std::allocator_traits<Alloc>;
    using T = typename Traits::value_type;

    std::mt19937 rng{42};
    std::bernoulli_distribution coin_flip(0.5); // 50% chance for allocation or deallocation

    constexpr int NUM_ELEMS = 1'000'000;
    constexpr int BLOCK_SIZE = 100'000;
    constexpr int MAX_STEP = 500;
    int steps_used = 0;

    int64_t total_time = 0;
    auto start_init = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_ELEMS; ++i)
    {
        T *ptr = allocator.allocate(1);
        Traits::construct(allocator, ptr, i);
        ptr_vec.push_back(ptr);
    }

    auto end_init = std::chrono::steady_clock::now();
    total_time += std::chrono::duration_cast<std::chrono::microseconds>(end_init - start_init).count();

    while (steps_used < MAX_STEP)
    {
        const bool must_alloc = ptr_vec.empty();
        const bool must_free = ptr_vec.size() >= NUM_ELEMS * 2;
        assert(!must_alloc || !must_free);
        const bool do_free = must_alloc ? false : must_free ? true
                                                            : coin_flip(rng);
        if (do_free)
        {
            // randomly select some old pointers to deallocate
            if (ptr_vec.size() > BLOCK_SIZE)
            {
                for (auto dest = ptr_vec.end() - BLOCK_SIZE; dest != ptr_vec.end() - BLOCK_SIZE / 2; ++dest)
                {
                    auto rand_it = ptr_vec.begin() + rng() % (ptr_vec.size() - BLOCK_SIZE);
                    std::swap(*dest, *rand_it);
                }
            }
            // Deallocate
            auto start_deallocate = std::chrono::steady_clock::now();
            for (auto it = ptr_vec.end() - BLOCK_SIZE; it != ptr_vec.end(); ++it)
            {
                T *ptr = *it;
                Traits::destroy(allocator, ptr);
                allocator.deallocate(ptr, 1);
            }
            ptr_vec.erase(ptr_vec.end() - BLOCK_SIZE, ptr_vec.end());

            auto end_deallocate = std::chrono::steady_clock::now();
            total_time += std::chrono::duration_cast<std::chrono::microseconds>(end_deallocate - start_deallocate).count();
        }
        else
        {
            // Reallocate
            auto start_reallocate = std::chrono::steady_clock::now();
            for (int j = 0; j < BLOCK_SIZE; ++j)
            {
                T *ptr = allocator.allocate(1);
                Traits::construct(allocator, ptr);
                ptr_vec.push_back(ptr);
            }
            auto end_reallocate = std::chrono::steady_clock::now();
            total_time += std::chrono::duration_cast<std::chrono::microseconds>(end_reallocate - start_reallocate).count();
        }
        steps_used++;
    }

    // Cleanup (if needed)
    auto start_cleanup = std::chrono::steady_clock::now();
    for (T *ptr : ptr_vec)
    {
        Traits::destroy(allocator, ptr);
        allocator.deallocate(ptr, 1);
    }
    ptr_vec.clear();
    auto end_cleanup = std::chrono::steady_clock::now();
    total_time += std::chrono::duration_cast<std::chrono::microseconds>(end_cleanup - start_cleanup).count();

    // std::cout << label << ": " << total_time << " us\n";
    return total_time;
}

template <typename T, size_t BlockSize = 1024>
class StackAllocator
{
public:
    /* Member types */
    using value_type = T;
    using pointer = T *;
    using const_pointer = const T *;
    using void_pointer = void *;
    using const_void_pointer = const void *;
    using reference = T &;
    using const_reference = const T &;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_copy_assignment = std::false_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    using is_always_equal = std::true_type;

    /* Legacy Rebind struct */
    template <typename U>
    struct rebind
    {
        typedef StackAllocator<U, BlockSize> other;
    };

    StackAllocator() noexcept {};
    StackAllocator(const StackAllocator &) noexcept = default;
    StackAllocator(StackAllocator &&) noexcept = default;
    template <class U>
    StackAllocator(const StackAllocator<U, BlockSize> &) noexcept {};
    ~StackAllocator() noexcept = default;

    StackAllocator &operator=(const StackAllocator &) = delete;
    StackAllocator &operator=(StackAllocator &&) noexcept = default;

    pointer addressof(reference x) const noexcept
    {
        return std::addressof(x);
    }
    const_pointer addressof(const_reference x) const noexcept
    {
        return std::addressof(x);
    }
    pointer allocate(size_type n = 1)
    {
        if (n == 0)
            return nullptr;
        if (n > 1)
            return std::allocator<T>().allocate(n);

        if (stack.empty())
            allocateBlock();
        pointer ret = stack.top();
        stack.pop();
        return ret;
    }

    void deallocate(pointer p, size_type n = 1)
    {
        if (n == 0 || p == nullptr)
            return;
        if (n > 1)
            return std::allocator<T>().deallocate(p, n);

        stack.push(p);
    }

    // Construct and destroy functions
    template <class U, class... Args>
    void construct(U *p, Args &&...args) noexcept
    {
        new (p) U(std::forward<Args>(args)...);
    }
    template <class U>
    void destroy(U *p) noexcept
    {
        p->~U();
    }

    size_type max_size() const noexcept
    {
        return BlockSize / sizeof(T);
    }

private:
    void allocateBlock()
    {
        // Allocate a block of memory and push it onto the stack
        T *block = std::allocator<T>().allocate(BlockSize);
        for (size_type i = 0; i < BlockSize / sizeof(T); ++i)
        {
            stack.push(block + i);
        }
    }
    std::stack<pointer, std::vector<pointer>> stack; // Stack to hold allocated pointers
};

// Text fixture - pool allocator and default allocator
class PoolAllocatorTest : public ::testing::Test
{
protected:
    int64_t pool_time;    // Time taken by pool allocator
    int64_t default_time; // Time taken by default allocator
    int64_t stack_time;   // Time taken by stack allocator

    // Pointer storage
    std::vector<int *> pool_ptr_vector;
    std::vector<int *> default_ptr_vector;
    std::vector<int *> stack_ptr_vector;

    // Test allocator
    std::allocator<int> default_allocator;
    PoolAllocator<int> pool_allocator;
    StackAllocator<int> stack_allocator;
};

// Test for pool allocator performance
TEST_F(PoolAllocatorTest, allocator_perf)
{
    pool_time = run_allocator_benchmark("Pool Allocator", pool_allocator, pool_ptr_vector);
    default_time = run_allocator_benchmark("Default Allocator", default_allocator, default_ptr_vector);
    stack_time = run_allocator_benchmark("Stack Allocator", stack_allocator, stack_ptr_vector);

    // Sanity check for test run time
    ASSERT_GT(pool_time, 0) << "Pool allocator time should be greater than 0";
    ASSERT_GT(default_time, 0) << "Default allocator time should be greater than 0";
    ASSERT_GT(stack_time, 0) << "Stack allocator time should be greater than 0";

    // Assert that vectors are empty after the test
    ASSERT_TRUE(pool_ptr_vector.empty()) << "Pool allocator vector should be empty after test";
    ASSERT_TRUE(default_ptr_vector.empty()) << "Default allocator vector should be empty after test";
    ASSERT_TRUE(stack_ptr_vector.empty()) << "Stack allocator vector should be empty after test";

    // Assert that pool allocator is faster than default allocator
    ASSERT_LT(pool_time, default_time) << "Pool allocator should be faster than default allocator";

    // print a relative performance comparison table with the default allocator as the baseline
    std::cout << "Performance Comparison:\n";

    std::cout << std::left << std::setw(12) << "Allocator"
              << std::right << std::setw(12) << "Time (us)"
              << std::setw(16) << "Relative (%)" << "\n"
              << std::fixed << std::setprecision(1);

    std::cout << std::left  << std::setw(12) << "Default"
              << std::right << std::setw(12) << default_time
              << std::setw(16) << "100.0%" << "\n";
    std::cout << std::left  << std::setw(12) << "Pool"
              << std::right << std::setw(12) << pool_time
              << std::setw(15) << std::fixed << std::setprecision(1)
              << (static_cast<double>(pool_time) / default_time) * 100 << "%" << "\n";
    std::cout << std::left  << std::setw(12) << "Stack"
              << std::right << std::setw(12) << stack_time
              << std::setw(15) << std::fixed << std::setprecision(1)
              << (static_cast<double>(stack_time) / default_time) * 100 << "%" << "\n";
}
