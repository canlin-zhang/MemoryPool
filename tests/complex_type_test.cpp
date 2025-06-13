#include <gtest/gtest.h>
#include <map>
#include <pool_allocator/general_helpers.hpp>
#include <pool_allocator/pool_allocator.h>
#include <pool_allocator/shared_helpers.hpp>
#include <string>
#include <vector>

// Test complex types
// Test fixture: Memory pool of std::string, std::vector<int>, and std::map<std::string, int>
class PoolAllocatorTest : public ::testing::Test
{
  public:
    struct alignas(64) AlignedStruct
    {
        char x;
    };

  protected:
    PoolAllocator<std::string> stringPool;
    PoolAllocator<std::vector<int>> vectorPool;
    PoolAllocator<std::map<std::string, int>> mapPool;
    PoolAllocator<AlignedStruct> alignedPool;
};

// Single element allocation and deallocation for std::string
TEST_F(PoolAllocatorTest, string_allocation)
{
    auto strPtr = new_object<std::string>(stringPool, "THIS IS A TEST OF STRING ALLOCATION");
    EXPECT_EQ(*strPtr, "THIS IS A TEST OF STRING ALLOCATION");
    delete_object<std::string>(stringPool, strPtr);
}

// Single element allocation and deallocation for std::vector<int>
TEST_F(PoolAllocatorTest, vector_allocation)
{
    auto vecPtr = new_object<std::vector<int>>(vectorPool, std::vector<int>{1, 2, 3, 4, 5});
    EXPECT_EQ((*vecPtr)[0], 1);
    EXPECT_EQ((*vecPtr)[1], 2);
    EXPECT_EQ((*vecPtr)[2], 3);
    EXPECT_EQ((*vecPtr)[3], 4);
    EXPECT_EQ((*vecPtr)[4], 5);
    delete_object<std::vector<int>>(vectorPool, vecPtr);
}

// Single element allocation and deallocation for std::map<std::string, int>
TEST_F(PoolAllocatorTest, map_allocation)
{
    auto mapPtr = new_object<std::map<std::string, int>>(
        mapPool, std::map<std::string, int>{{"one", 1}, {"two", 2}});
    EXPECT_EQ((*mapPtr)["one"], 1);
    EXPECT_EQ((*mapPtr)["two"], 2);
    delete_object<std::map<std::string, int>>(mapPool, mapPtr);
}

// Test allocation of a struct with alignment requirements
TEST_F(PoolAllocatorTest, aligned_struct_allocation)
{
    EXPECT_LE(sizeof(AlignedStruct),
              alignof(AlignedStruct)); // Ensure size is less than or equal to alignment

    auto alignedPtr = new_object<AlignedStruct>(alignedPool);
    alignedPtr->x = 'A';

    EXPECT_EQ(alignedPtr->x, 'A');
    EXPECT_EQ(reinterpret_cast<uintptr_t>(alignedPtr) % alignof(AlignedStruct), 0);
    delete_object<AlignedStruct>(alignedPool, alignedPtr);
}

// Test shared pointer
TEST_F(PoolAllocatorTest, shared_pointer_allocation)
{
    auto sharedPtr = pool_make_shared(stringPool, "This is a test for shared string.");
    std::shared_ptr<std::string> anotherSharedPtr = sharedPtr;

    // Test value
    EXPECT_EQ(*sharedPtr, "This is a test for shared string.");

    // Test reference counting
    EXPECT_EQ(sharedPtr.use_count(), 2);
    EXPECT_EQ(anotherSharedPtr.use_count(), 2);

    // Test reset and reference counting
    sharedPtr.reset();
    EXPECT_EQ(sharedPtr.use_count(), 0);
    EXPECT_EQ(anotherSharedPtr.use_count(), 1);

    // Memory still valid after reset
    EXPECT_EQ(*anotherSharedPtr, "This is a test for shared string.");
}
