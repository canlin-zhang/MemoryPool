#include <gtest/gtest.h>
#include <pool_allocator/pool_allocator.h>

struct IncompleteStruct;

TEST(MemoryPoolTest, IncompleteStructAllocation)
{
    // Can construct a unique_ptr with an incomplete type
    using IncompletePtr = std::unique_ptr<IncompleteStruct, PoolAllocator<IncompleteStruct>::Deleter>;

    // Can create a memory pool for an incomplete type
    PoolAllocator<IncompleteStruct> pool;

    // The code should compile to here.
    SUCCEED();
}

struct IncompleteStruct
{
    int data = 42; // This is just a placeholder to make the struct complete
};