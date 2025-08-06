#include <gtest/gtest.h>
#include <pool_allocator/pool_allocator.h>

#include <vector>

// Test complex types
// Test fixture: Memory pool of std::string, std::vector<int>, and
// std::map<std::string, int>
class PoolAllocatorTest : public ::testing::Test
{
  public:
    struct alignas(64) ComplexStruct
    {
        // Char
        char x;

        // Vector
        std::vector<int> vec;

        // Another struct with smaller alignment
        struct alignas(16) InnerStruct
        {
            int a;
            double b;
        } inner;
    };

  protected:
    PoolAllocator<ComplexStruct> complexPool;
};
