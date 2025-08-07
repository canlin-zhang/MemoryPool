#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <pool_allocator/pool_allocator.h>
#include <tests/test_structs.h>
#include <vector>

constexpr size_t kBlockSize = 4096;
using Alloc = PoolAllocator<ComplexStruct, kBlockSize>;

inline size_t
expected_total_size(size_t allocated_count)
{
    const size_t object_size = sizeof(ComplexStruct);
    const size_t slots_per_block = kBlockSize / object_size;
    const size_t required_blocks = static_cast<size_t>(
        std::ceil(static_cast<double>(allocated_count) / slots_per_block));
    return required_blocks * kBlockSize;
}

TEST(PoolAllocatorTest, initial_state)
{
    Alloc allocator;
    EXPECT_TRUE(allocator.is_valid());
    EXPECT_FALSE(allocator.has_blocks());
    EXPECT_FALSE(allocator.has_free_slots());
    EXPECT_EQ(allocator.total_size(), 0);
}

TEST(PoolAllocatorTest, allocate_one_state)
{
    Alloc allocator;
    auto ptr = allocator.make_unique();

    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->x, 'X');
    EXPECT_EQ(ptr->vec, std::vector<int>({1, 2, 3, 4, 5}));
    EXPECT_EQ(ptr->inner.a, 42);
    EXPECT_DOUBLE_EQ(ptr->inner.b, 3.14);

    EXPECT_TRUE(allocator.is_valid());
    EXPECT_TRUE(allocator.has_blocks());
    EXPECT_TRUE(allocator.has_free_slots());
    EXPECT_EQ(allocator.total_size(), expected_total_size(1));

    // RAII cleanup by allocator destructor
}

TEST(PoolAllocatorTest, allocate_delete_multiple_state)
{
    Alloc allocator;
    constexpr size_t N = 100;
    std::vector<std::unique_ptr<ComplexStruct, Alloc::Deleter>> objects;

    for (size_t i = 0; i < N; ++i)
    {
        auto uptr = allocator.make_unique();
        ASSERT_NE(uptr.get(), nullptr);

        EXPECT_EQ(uptr->x, 'X');
        EXPECT_EQ(uptr->vec, std::vector<int>({1, 2, 3, 4, 5}));
        EXPECT_EQ(uptr->inner.a, 42);
        EXPECT_DOUBLE_EQ(uptr->inner.b, 3.14);

        objects.push_back(std::move(uptr));
    }

    EXPECT_TRUE(allocator.has_blocks());
    EXPECT_TRUE(allocator.has_free_slots());
    EXPECT_EQ(allocator.total_size(), expected_total_size(N));

    // No manual destroy/deallocate needed — unique_ptr handles it
}

TEST(PoolAllocatorTest, export_invalidates)
{
    Alloc allocator;
    allocator.allocate(); // trigger allocation
    auto exported = allocator.export_pool();

    // Import into a new allocator
    Alloc new_allocator;
    new_allocator.import_pool(exported);

    EXPECT_FALSE(allocator.is_valid());
    EXPECT_TRUE(new_allocator.is_valid());
    EXPECT_TRUE(new_allocator.has_blocks());
}

TEST(PoolAllocatorTest, import_appends)
{
    Alloc a, b;
    constexpr size_t N = 5;

    for (size_t i = 0; i < N; ++i)
        b.allocate();

    auto blocks = b.export_pool();
    a.import_pool(blocks);

    std::vector<std::unique_ptr<ComplexStruct, Alloc::Deleter>> objects;
    for (size_t i = 0; i < N; ++i)
    {
        auto uptr = a.make_unique();
        ASSERT_NE(uptr.get(), nullptr);

        EXPECT_EQ(uptr->x, 'X');
        EXPECT_EQ(uptr->vec, std::vector<int>({1, 2, 3, 4, 5}));
        EXPECT_EQ(uptr->inner.a, 42);
        EXPECT_DOUBLE_EQ(uptr->inner.b, 3.14);

        objects.push_back(std::move(uptr));
    }

    EXPECT_TRUE(a.is_valid());
    EXPECT_TRUE(a.has_blocks());
    EXPECT_TRUE(a.has_free_slots());
    EXPECT_EQ(a.total_size(), expected_total_size(N));
}

TEST(PoolAllocatorTest, import_revives_invalid_allocator)
{
    constexpr size_t N = 10;
    Alloc a, b, c;

    // Setup: B exports to A, becomes invalid
    for (size_t i = 0; i < N; ++i)
        b.allocate();

    auto b_blocks = b.export_pool();
    a.import_pool(b_blocks);

    EXPECT_FALSE(b.is_valid());
    EXPECT_TRUE(a.is_valid());
    EXPECT_EQ(a.total_size(), expected_total_size(N));

    // Setup: C allocates blocks
    for (size_t i = 0; i < N; ++i)
        c.allocate();

    auto c_blocks = c.export_pool();
    b.import_pool(c_blocks); // revive B

    EXPECT_TRUE(b.is_valid());
    EXPECT_TRUE(b.has_blocks());
    EXPECT_TRUE(b.has_free_slots());
    EXPECT_EQ(b.total_size(), expected_total_size(N));

    // Construct in revived B
    auto obj = b.make_unique();
    ASSERT_NE(obj.get(), nullptr);

    EXPECT_EQ(obj->x, 'X');
    EXPECT_EQ(obj->vec, std::vector<int>({1, 2, 3, 4, 5}));
    EXPECT_EQ(obj->inner.a, 42);
    EXPECT_DOUBLE_EQ(obj->inner.b, 3.14);

    // No manual cleanup needed
}
