#include <gtest/gtest.h>
#include <map>
#include <pool_allocator/pool_allocator.h>
#include <stdint.h>
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
    auto strPtr = stringPool.new_object("THIS IS A TEST OF STRING ALLOCATION");
    EXPECT_EQ(*strPtr, "THIS IS A TEST OF STRING ALLOCATION");
    stringPool.delete_object(strPtr);
}

// Single element allocation and deallocation for std::vector<int>
TEST_F(PoolAllocatorTest, vector_allocation)
{
    auto vecPtr = vectorPool.new_object(std::vector<int>{1, 2, 3, 4, 5});
    EXPECT_EQ((*vecPtr)[0], 1);
    EXPECT_EQ((*vecPtr)[1], 2);
    EXPECT_EQ((*vecPtr)[2], 3);
    EXPECT_EQ((*vecPtr)[3], 4);
    EXPECT_EQ((*vecPtr)[4], 5);
    vectorPool.delete_object(vecPtr);
}

// Single element allocation and deallocation for std::map<std::string, int>
TEST_F(PoolAllocatorTest, map_allocation)
{
    auto mapPtr = mapPool.new_object(std::map<std::string, int>{{"one", 1}, {"two", 2}});
    EXPECT_EQ((*mapPtr)["one"], 1);
    EXPECT_EQ((*mapPtr)["two"], 2);
    mapPool.delete_object(mapPtr);
}

// Test allocation of a struct with alignment requirements
TEST_F(PoolAllocatorTest, aligned_struct_allocation)
{
    EXPECT_LE(sizeof(AlignedStruct),
              alignof(AlignedStruct)); // Ensure size is less than or equal to alignment

    auto alignedPtr = alignedPool.new_object();
    alignedPtr->x = 'A';

    EXPECT_EQ(alignedPtr->x, 'A');
    EXPECT_EQ(reinterpret_cast<uintptr_t>(alignedPtr) % alignof(AlignedStruct), 0);
    alignedPool.delete_object(alignedPtr);
}
