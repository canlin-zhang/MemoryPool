#include <pool_allocator/pool_allocator.h>

#include <gtest/gtest.h>

// Test fundamental types
// Test fixture: Memory pool of int, double and char
class PoolAllocatorTest : public ::testing::Test
{
  protected:
    // This can be used to set up any common state for the tests
    PoolAllocator<int> intPool;
    PoolAllocator<double> doublePool;
    PoolAllocator<char> charPool;
};

// Single element allocation and deallocation
TEST_F(PoolAllocatorTest, basic_type_allocation)
{
    auto intPtr = intPool.new_object(42);
    EXPECT_EQ(*intPtr, 42);

    auto doublePtr = doublePool.new_object(3.14);
    EXPECT_DOUBLE_EQ(*doublePtr, 3.14);

    auto charPtr = charPool.new_object('A');
    EXPECT_EQ(*charPtr, 'A');
}

// Deallocate single elements
TEST_F(PoolAllocatorTest, basic_type_deallocation)
{
    auto intPtr = intPool.new_object(42);
    intPool.delete_object(intPtr);

    auto doublePtr = doublePool.new_object(3.14);
    doublePool.delete_object(doublePtr);

    auto charPtr = charPool.new_object('A');
    charPool.delete_object(charPtr);
}

// Allocate multiple elements
TEST_F(PoolAllocatorTest, basic_type_multiple_allocation)
{
    auto intPtr = intPool.allocate(100000);
    for (size_t i = 0; i < 100000; ++i)
    {
        intPool.construct(intPtr + i, static_cast<int>(i));
    }
    for (size_t i = 0; i < 100000; ++i)
    {
        EXPECT_EQ(intPtr[i], static_cast<int>(i));
    }
    intPool.deallocate(intPtr, 100000);

    auto doublePtr = doublePool.allocate(100000);
    for (size_t i = 0; i < 100000; ++i)
    {
        doublePool.construct(doublePtr + i, static_cast<double>(i) + 0.5);
    }
    for (size_t i = 0; i < 100000; ++i)
    {
        EXPECT_DOUBLE_EQ(doublePtr[i], static_cast<double>(i) + 0.5);
    }
    doublePool.deallocate(doublePtr, 100000);

    auto charPtr = charPool.allocate(256);
    for (size_t i = 0; i < 256; ++i)
    {
        charPool.construct(charPtr + i, static_cast<char>(i));
    }
    for (size_t i = 0; i < 256; ++i)
    {
        EXPECT_EQ(charPtr[i], static_cast<char>(i));
    }
    charPool.deallocate(charPtr, 256);
}
