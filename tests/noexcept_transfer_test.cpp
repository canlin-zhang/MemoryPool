#include <pool_allocator/pool_allocator.h>

#include <gtest/gtest.h>
#include <utility>
#include <vector>

// Task 2: the policy param exists, defaults to Fast, and existing 2-arg
// usage is unchanged.
TEST(TransferMode, FastIsDefaultAndCompiles)
{
    PoolAllocator<int> a; // default = Fast, unchanged
    PoolAllocator<int, 4096, TransferMode::Fast> b;
    int* p = a.allocate();
    a.deallocate(p);
    (void)b;
    SUCCEED();
}

namespace
{
struct Big
{
    alignas(void*) unsigned char b[32]; // >= sizeof(T*)
};
} // namespace

// Task 4: Noexcept free-slot store recycles slots via the SlotList backend.
TEST(NoexceptFreeSlots, DeallocateThenReuseRoundTrips)
{
    PoolAllocator<Big, 4096, TransferMode::Noexcept> pool;
    Big* p = pool.allocate();
    pool.deallocate(p);
    EXPECT_EQ(pool.num_slots_available(), 1u);
    Big* q = pool.allocate(); // reuses the freed slot
    EXPECT_EQ(q, p);
    EXPECT_EQ(pool.num_slots_available(), 0u);
    pool.deallocate(q);
}

// Task 5: Noexcept block store reserves slot 0 per block (127 usable of 128).
TEST(NoexceptBlocks, CarvesAndAccountsAcrossBlocks)
{
    PoolAllocator<Big, 4096, TransferMode::Noexcept> pool;
    std::vector<Big*> ptrs;
    for (int i = 0; i < 130; ++i) // 127 usable/block -> forces a 2nd block
        ptrs.push_back(pool.allocate());
    EXPECT_EQ(pool.allocated_bytes(), 2u * 4096u);
    for (Big* p : ptrs)
        pool.deallocate(p);
}

// Task 6: transfer_all is noexcept under Noexcept mode and moves everything.
TEST(NoexceptTransfer, TransferAllIsNoexceptAndMovesEverything)
{
    using Pool = PoolAllocator<Big, 4096, TransferMode::Noexcept>;
    static_assert(noexcept(std::declval<Pool&>().transfer_all(std::declval<Pool&>())),
                  "transfer_all must be noexcept under Noexcept mode");
    Pool src, dst;
    std::vector<Big*> ptrs;
    for (int i = 0; i < 200; ++i)
        ptrs.push_back(src.allocate());
    for (Big* p : ptrs)
        src.deallocate(p);
    const auto bytes = src.allocated_bytes();
    EXPECT_GT(bytes, 0u);
    dst.transfer_all(src);
    EXPECT_EQ(src.allocated_bytes(), 0u);
    EXPECT_EQ(dst.allocated_bytes(), bytes);
}
