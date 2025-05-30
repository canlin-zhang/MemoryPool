#include <gtest/gtest.h>
#include <memory_pool/memory_pool.h>

struct IncompleteStruct;

TEST(MemoryPoolTest, IncompleteStructAllocation)
{
    // Can construct a unique_ptr with an incomplete type
    using IncompletePtr = typename MemoryPool<IncompleteStruct>::unique_ptr;

    // Can create a memory pool for an incomplete type
    MemoryPool<IncompleteStruct> pool;

    // The code should compile to here.
    SUCCEED();
}