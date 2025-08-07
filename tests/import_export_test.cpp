#include <gtest/gtest.h>
#include <pool_allocator/pool_allocator.h>
#include <tests/test_structs.h>

#include <atomic>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

TEST(PoolAllocatorTest, single_thread_sanity_check)
{
    using Alloc = PoolAllocator<ComplexStruct, 4096>;
    using Ptr = ComplexStruct*;

    Alloc allocator;
    Ptr p = allocator.new_object();

    // Sanity check
    EXPECT_EQ(p->x, 'X');
    EXPECT_EQ(p->vec, std::vector<int>({1, 2, 3, 4, 5}));
    EXPECT_EQ(p->inner.a, 42);
    EXPECT_DOUBLE_EQ(p->inner.b, 3.14);

    // Clean up
    allocator.delete_object(p);
}

TEST(PoolAllocatorTest, export_to_main_thread)
{
    using Ptr = ComplexStruct*;
    using ThreadExport = std::vector<Ptr>;

    constexpr size_t kBlockSize = 4096;
    using ThreadAlloc = PoolAllocator<ComplexStruct, kBlockSize>;

    std::mutex mtx;
    std::atomic<size_t> expected_block_count{0};
    std::atomic<size_t> expected_object_count{0};
    std::vector<ThreadExport> thread_exported_blocks;

    constexpr int max_threads = 8;
    int num_threads = 1 + rand() % max_threads;
    std::vector<std::thread> threads;
    thread_exported_blocks.resize(num_threads);

    for (int tid = 0; tid < num_threads; ++tid)
    {
        threads.emplace_back(
            [&, tid]()
            {
                ThreadAlloc allocator;
                int num_objects = rand() % 9; // 0 to 8

                for (int i = 0; i < num_objects; ++i)
                {
                    Ptr p = allocator.new_object();

                    // Sanity check
                    EXPECT_EQ(p->x, 'X');
                    EXPECT_EQ(p->vec, std::vector<int>({1, 2, 3, 4, 5}));
                    EXPECT_EQ(p->inner.a, 42);
                    EXPECT_DOUBLE_EQ(p->inner.b, 3.14);

                    // We can delete the object here even if we are exporting
                    // the pool later This is because the allocator does NOT
                    // free the memory when an object is deleted It only marks
                    // the slot as free or decrement the current block slot
                    allocator.delete_object(p);
                }

                size_t actual_total_size = allocator.total_size();

                auto blocks = allocator.export_pool();
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    expected_block_count += blocks.size();
                    // Check if all blocks are of the expected size
                    EXPECT_EQ(actual_total_size, blocks.size() * kBlockSize);
                    expected_object_count += num_objects;
                }

                thread_exported_blocks[tid] = std::move(blocks);
            });
    }

    for (auto& t : threads)
        t.join();

    // Main thread consumes all blocks
    PoolAllocator<ComplexStruct, kBlockSize> main_allocator;

    size_t actual_blocks = 0;
    for (const auto& blocks : thread_exported_blocks)
    {
        actual_blocks += blocks.size();
        main_allocator.import_pool(blocks);
    }

    EXPECT_EQ(actual_blocks, expected_block_count.load());

    // Record the current total memory size used
    size_t total_size = main_allocator.total_size();

    // Allocate the exact number of objects expected
    for (size_t i = 0; i < expected_object_count.load(); ++i)
    {
        auto p = main_allocator.make_unique();
    }

    // There should be no additional memory allocation
    EXPECT_EQ(main_allocator.total_size(), total_size);
}

TEST(PoolAllocatorTest, mutual_export)
{
    using Alloc = PoolAllocator<ComplexStruct, 4096>;
    using Ptr = ComplexStruct*;
    constexpr size_t kBlockSize = 4096;

    const int num_threads = 1 + rand() % 8;
    Alloc main_allocator;

    // Step 1: Force main to allocate some blocks
    size_t initial_allocs = 256 + rand() % 256;
    std::vector<Ptr> warmup;
    for (size_t i = 0; i < initial_allocs; ++i)
        warmup.push_back(main_allocator.new_object());
    for (auto& p : warmup)
        main_allocator.delete_object(p);

    // Step 2: Export and partition blocks
    auto all_blocks = main_allocator.export_pool();
    const size_t total_blocks = all_blocks.size();
    ASSERT_GE(total_blocks, num_threads);

    std::vector<std::vector<ComplexStruct*>> thread_blocks(num_threads);
    size_t offset = 0;
    for (int i = 0; i < num_threads; ++i)
    {
        size_t remaining = total_blocks - offset - (num_threads - i - 1);
        size_t count = (i == num_threads - 1) ? (total_blocks - offset)
                                              : (1 + rand() % (remaining + 1));
        thread_blocks[i].insert(thread_blocks[i].end(),
                                all_blocks.begin() + offset,
                                all_blocks.begin() + offset + count);
        offset += count;
    }

    // Step 3: Thread logic
    std::mutex mtx;
    std::atomic<size_t> total_blocks_used{0};
    std::atomic<size_t> total_objects{0};
    std::vector<std::vector<ComplexStruct*>> exported_blocks(num_threads);

    std::vector<std::thread> threads;
    for (int tid = 0; tid < num_threads; ++tid)
    {
        threads.emplace_back(
            [&, tid]()
            {
                Alloc allocator;
                allocator.import_pool(thread_blocks[tid]);

                // Use a thread-local, seeded random number generator for
                // reproducibility
                std::mt19937 rng(42 + tid); // Seed with fixed value + thread id
                std::uniform_int_distribution<int> dist(0, 15);
                int num_objects = dist(rng);
                for (int i = 0; i < num_objects; ++i)
                {
                    auto p = allocator.new_object();
                    allocator.delete_object(p);
                }

                size_t used_blocks = allocator.total_size() / kBlockSize;
                auto blocks = allocator.export_pool();

                {
                    std::lock_guard<std::mutex> lock(mtx);
                    total_blocks_used += used_blocks;
                    total_objects += num_objects;
                    exported_blocks[tid] = std::move(blocks);
                }
            });
    }

    for (auto& t : threads)
        t.join();

    // Step 4: Re-import all exported memory
    size_t reimported = 0;
    for (const auto& blocks : exported_blocks)
    {
        reimported += blocks.size();
        main_allocator.import_pool(blocks);
    }
    size_t before = main_allocator.total_size();
    EXPECT_EQ(reimported, total_blocks_used.load())
        << "Main thread should receive exactly all blocks used by threads";

    // Step 5: Allocate the same number of objects back
    for (size_t i = 0; i < total_objects.load(); ++i)
    {
        auto ptr = main_allocator.make_unique();
        EXPECT_EQ(ptr->x, 'X');
        EXPECT_EQ(ptr->vec, std::vector<int>({1, 2, 3, 4, 5}));
        EXPECT_EQ(ptr->inner.a, 42);
        EXPECT_DOUBLE_EQ(ptr->inner.b, 3.14);
    }

    EXPECT_EQ(main_allocator.total_size(), before)
        << "Memory usage must match reused block size exactly";
}
