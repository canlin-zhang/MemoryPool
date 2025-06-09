#include <pool_allocator/pool_allocator.h>
#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <chrono>
#include <random>
#include <algorithm>
#include <iostream>

template <typename Alloc>
int64_t run_allocator_benchmark(const std::string &label, Alloc &allocator, std::vector<typename std::allocator_traits<Alloc>::pointer> &ptr_vec)
{
    using Traits = std::allocator_traits<Alloc>;
    using T = typename Traits::value_type;

    std::mt19937 rng{42};
    std::bernoulli_distribution coin_flip(0.5); // 50% chance for allocation or deallocation

    constexpr int NUM_ELEMS = 1'000'000;
    constexpr int BLOCK_SIZE = 100'000;

    int64_t total_time = 0;
    auto start_init = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_ELEMS; ++i)
    {
        T *ptr = allocator.allocate(1);
        Traits::construct(allocator, ptr, i);
        ptr_vec.push_back(ptr);
    }

    auto end_init = std::chrono::steady_clock::now();
    total_time += std::chrono::duration_cast<std::chrono::milliseconds>(end_init - start_init).count();

    while (!ptr_vec.empty())
    {
        std::shuffle(ptr_vec.begin(), ptr_vec.end(), rng);

        if (coin_flip(rng))
        {
            // Deallocate
            auto start_deallocate = std::chrono::steady_clock::now();
            for (int j = 0; j < BLOCK_SIZE; ++j)
            {
                T *ptr = ptr_vec.back();
                Traits::destroy(allocator, ptr);
                allocator.deallocate(ptr, 1);
                ptr_vec.pop_back();
            }
            auto end_deallocate = std::chrono::steady_clock::now();
            total_time += std::chrono::duration_cast<std::chrono::milliseconds>(end_deallocate - start_deallocate).count();
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
            total_time += std::chrono::duration_cast<std::chrono::milliseconds>(end_reallocate - start_reallocate).count();
        }
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
    total_time += std::chrono::duration_cast<std::chrono::milliseconds>(end_cleanup - start_cleanup).count();

    std::cout << label << ": " << total_time << " ms\n";
    return total_time;
}

// Text fixture - pool allocator and default allocator
class PoolAllocatorTest : public ::testing::Test
{
protected:
    int64_t pool_time;    // Time taken by pool allocator
    int64_t default_time; // Time taken by default allocator

    // Pointer storage
    std::vector<int *> pool_ptr_vector;
    std::vector<int *> default_ptr_vector;

    // Test allocator
    std::allocator<int> default_allocator;
    PoolAllocator<int> pool_allocator;
};

// Test for pool allocator performance
TEST_F(PoolAllocatorTest, allocator_perf)
{
    pool_time = run_allocator_benchmark("Pool Allocator", pool_allocator, pool_ptr_vector);
    default_time = run_allocator_benchmark("Default Allocator", default_allocator, default_ptr_vector);

    // Assert that vectors are empty after the test
    ASSERT_TRUE(pool_ptr_vector.empty()) << "Pool allocator vector should be empty after test";
    ASSERT_TRUE(default_ptr_vector.empty()) << "Default allocator vector should be empty after test";

    // Assert that pool allocator is faster than default allocator
    ASSERT_LT(pool_time, default_time) << "Pool allocator should be faster than default allocator";
}
