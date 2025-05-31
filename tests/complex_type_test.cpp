#include <pool_allocator/pool_allocator.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <map>

// Test complex types
// Test fixture: Memory pool of std::string, std::vector<int>, and std::map<std::string, int>
class MemoryPoolTest : public ::testing::Test
{
protected:
    PoolAllocator<std::string> stringPool;
    PoolAllocator<std::vector<int>> vectorPool;
    PoolAllocator<std::map<std::string, int>> mapPool;
};

// Single element allocation and deallocation for std::string
TEST_F(MemoryPoolTest, string_allocation)
{
    auto strPtr = stringPool.new_object("THIS IS A TEST OF STRING ALLOCATION");
    EXPECT_EQ(*strPtr, "THIS IS A TEST OF STRING ALLOCATION");
    stringPool.delete_object(strPtr);
}

// Single element allocation and deallocation for std::vector<int>
TEST_F(MemoryPoolTest, vector_allocation)
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
TEST_F(MemoryPoolTest, map_allocation)
{
    auto mapPtr = mapPool.new_object(std::map<std::string, int>{{"one", 1}, {"two", 2}});
    EXPECT_EQ((*mapPtr)["one"], 1);
    EXPECT_EQ((*mapPtr)["two"], 2);
    mapPool.delete_object(mapPtr);
}