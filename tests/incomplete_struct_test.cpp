#include <gtest/gtest.h>
#include <pool_allocator/pool_allocator.h>

struct IncompleteStruct;

TEST(PoolAllocatorTest, IncompleteStructAllocation)
{
    // Can construct a unique_ptr with an incomplete type
    using IncompletePtr = std::unique_ptr<IncompleteStruct, PoolAllocator<IncompleteStruct>::Deleter>;

    // Can create a memory pool for an incomplete type
    PoolAllocator<IncompleteStruct> pool;

    // Test make unique with incomplete type
    IncompletePtr ptr = pool.make_unique();
    ASSERT_NE(ptr, nullptr); // Ensure the pointer is not null

    // The code should compile to here.
    SUCCEED();
}

struct IncompleteStruct
{
    int data = 42;                    // This is just a placeholder to make the struct complete
    IncompleteStruct *next = nullptr; // Pointer to the next incomplete struct
    IncompleteStruct() = default;     // Default constructor
};