#include "../memory_pool.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <map>

// Test complex types
// Test fixture: Memory pool of std::string, std::vector<int>, and std::map<std::string, int>
class MemoryPoolTest : public ::testing::Test
{
protected:
    MemoryPool<std::string> stringPool;
    MemoryPool<std::vector<int>> vectorPool;
    MemoryPool<std::map<std::string, int>> mapPool;
};

// Single element allocation and deallocation for std::string
TEST_F(MemoryPoolTest, string_allocation)
{
    auto strPtr = stringPool.newElement("THIS IS A TEST OF STRING ALLOCATION");
    EXPECT_EQ(*strPtr, "THIS IS A TEST OF STRING ALLOCATION");
    stringPool.deleteElement(strPtr);
}

// Single element allocation and deallocation for std::vector<int>
TEST_F(MemoryPoolTest, vector_allocation)
{
    auto vecPtr = vectorPool.newElement(std::vector<int>{1, 2, 3, 4, 5});
    EXPECT_EQ((*vecPtr)[0], 1);
    EXPECT_EQ((*vecPtr)[1], 2);
    EXPECT_EQ((*vecPtr)[2], 3);
    EXPECT_EQ((*vecPtr)[3], 4);
    EXPECT_EQ((*vecPtr)[4], 5);
    vectorPool.deleteElement(vecPtr);
}

// Single element allocation and deallocation for std::map<std::string, int>
TEST_F(MemoryPoolTest, map_allocation)
{
    auto mapPtr = mapPool.newElement(std::map<std::string, int>{{"one", 1}, {"two", 2}});
    EXPECT_EQ((*mapPtr)["one"], 1);
    EXPECT_EQ((*mapPtr)["two"], 2);
    mapPool.deleteElement(mapPtr);
}