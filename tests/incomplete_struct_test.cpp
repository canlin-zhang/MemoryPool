#include <gtest/gtest.h>
#include <pool_allocator/pool_allocator.h>
#include <pool_allocator/unique_helpers.hpp>

struct IncompleteStruct;

TEST(PoolAllocatorTest, forward_declaration_test)
{
    // Can create a memory pool for an incomplete type
    PoolAllocator<IncompleteStruct> pool;

    // Test make unique with incomplete type
    auto ptr = pool_make_unique(pool);
    ASSERT_NE(ptr, nullptr); // Ensure the pointer is not null

    // The code should compile to here.
    SUCCEED();
}

struct IncompleteStruct
{
    int data = 42;                    // This is just a placeholder to make the struct complete
    IncompleteStruct* next = nullptr; // Pointer to the next incomplete struct
    IncompleteStruct() = default;     // Default constructor
};
