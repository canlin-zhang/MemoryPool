#include "pool_allocator/pool_allocator.h"
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <tuple>
#include <vector>

// Test parameters derived once here
namespace
{
constexpr size_t kBlockSize = 64; // cacheline-sized blocks for tests
using value_type = int;
using TestAlloc = PoolAllocator<value_type, kBlockSize>;
constexpr size_t kSlotsPerBlock = kBlockSize / sizeof(value_type);

template <typename T>
constexpr T
ceil_div(T a, T b)
{
    return (a + b - 1) / b;
}

// Make predictions about the state of the allocator after various sequences of
// allocations/deallocations Rules: 1) Allocator starts empty; 0 blocks, 0 slots and 0 bump
// available. 2) Each allocation will first try to use a free slot, then a bump space, and if
// neither is available, will allocate new blocks if needed.
//    When allocating a new block, kSlotsPerBlock bump space will be made available
// 3) Each deallocation will free up a slot in the allocator
struct AllocatorPrediction
{
    size_t blocks_alloc = 0;
    size_t slots_avail = 0;
    size_t bump_avail = 0;
    size_t bytes() const
    {
        return blocks_alloc * kBlockSize;
    }
    size_t live_alloc() const
    {
        return blocks_alloc * kSlotsPerBlock - slots_avail - bump_avail;
    }
    static AllocatorPrediction of_ta(const TestAlloc& alloc)
    {
        return AllocatorPrediction{alloc.allocated_bytes() / kBlockSize,
                                   alloc.num_slots_available(), alloc.num_bump_available()};
    }
    constexpr AllocatorPrediction alloc(int n) const
    {
        const size_t use_from_slots = std::min<size_t>(n, slots_avail);
        const size_t remaining1 = n - use_from_slots;
        const size_t use_from_bump = std::min<size_t>(remaining1, bump_avail);
        const size_t remaining2 = remaining1 - use_from_bump;
        const size_t blocks_added = ceil_div(remaining2, kSlotsPerBlock);
        const size_t bump_added = blocks_added * kSlotsPerBlock - remaining2;
        return AllocatorPrediction{blocks_alloc + blocks_added, slots_avail - use_from_slots,
                                   bump_avail - use_from_bump + bump_added};
    }
    constexpr AllocatorPrediction dealloc(int n) const
    {
        return AllocatorPrediction{blocks_alloc, slots_avail + n, bump_avail};
    }
    auto cmp_tie() const
    {
        return std::tie(blocks_alloc, slots_avail, bump_avail);
    }
    bool operator==(const AllocatorPrediction& other) const
    {
        return this->cmp_tie() == other.cmp_tie();
    }
};
std::ostream&
operator<<(std::ostream& os, const AllocatorPrediction& pred)
{
    return os << "AllocatorPrediction{blocks_alloc=" << pred.blocks_alloc
              << ", slots_avail=" << pred.slots_avail << ", bump_avail=" << pred.bump_avail << "}";
}

struct ToFrom
{
    AllocatorPrediction to, from;
};
constexpr ToFrom
transfer_pool(ToFrom tf)
{
    // .to, .from
    return ToFrom{AllocatorPrediction{tf.to.blocks_alloc, tf.to.slots_avail + tf.from.slots_avail,
                                      tf.to.bump_avail},
                  AllocatorPrediction{tf.from.blocks_alloc, 0, tf.from.bump_avail}};
}
constexpr ToFrom
transfer_all(ToFrom tf)
{
    // .to, .from
    return ToFrom{AllocatorPrediction{tf.to.blocks_alloc + tf.from.blocks_alloc,
                                      tf.to.slots_avail + tf.from.slots_avail + tf.from.bump_avail,
                                      tf.to.bump_avail},
                  AllocatorPrediction{0, 0, 0}};
}

} // namespace

class TransferTest : public ::testing::Test
{
  protected:
    TestAlloc allocator;

    void SetUp() override
    {
        // Reset allocator before each test
        TestAlloc().transfer_all(allocator);
    }
};

TEST_F(TransferTest, TransferToOtherAllocator)
{
    // Allocate some memory blocks
    std::vector<int*> ptr_vec;
    constexpr int NUM_ALLOC = 100;
    for (int i = 0; i < NUM_ALLOC; i++)
        ptr_vec.push_back(allocator.allocate());

    constexpr auto pred = AllocatorPrediction().alloc(NUM_ALLOC);
    EXPECT_EQ(pred, AllocatorPrediction::of_ta(allocator));

    for (int* ptr : ptr_vec)
        allocator.deallocate(ptr);
    const auto pred2 = pred.dealloc(NUM_ALLOC);
    EXPECT_EQ(pred2, AllocatorPrediction::of_ta(allocator));

    // Create a new allocator to transfer to
    TestAlloc destAllocator;

    EXPECT_EQ(AllocatorPrediction(), AllocatorPrediction::of_ta(destAllocator));

    // Transfer memory from source to destination
    ASSERT_NO_THROW(destAllocator.transfer_all(allocator));

    // The original allocator should be empty
    auto [to, from] = transfer_all({AllocatorPrediction(), pred2});
    EXPECT_EQ(from, AllocatorPrediction::of_ta(allocator));
    EXPECT_EQ(to, AllocatorPrediction::of_ta(destAllocator));
}

TEST_F(TransferTest, TransferFreeMovesOnlyFreeSlots)
{
    std::vector<int*> ptrs;
    constexpr int NUM_ALLOC = 50;
    constexpr int NUM_FREE = 20;
    for (int i = 0; i < NUM_ALLOC; ++i)
        ptrs.push_back(allocator.allocate());

    // Free a subset
    for (int i = 0; i < NUM_FREE; ++i)
        allocator.deallocate(ptrs[i]);
    EXPECT_EQ(allocator.num_slots_available(), NUM_FREE);

    const auto pred = AllocatorPrediction().alloc(NUM_ALLOC);
    EXPECT_EQ(allocator.allocated_bytes(), pred.bytes());

    TestAlloc dest;
    EXPECT_EQ(dest.allocated_bytes(), 0);
    EXPECT_EQ(dest.num_slots_available(), 0);

    // Transfer only free slots
    ASSERT_NO_THROW(dest.transfer_free(allocator));

    // Source keeps its blocks; free list emptied
    EXPECT_EQ(allocator.allocated_bytes(), pred.bytes());
    EXPECT_EQ(allocator.num_slots_available(), 0);

    // Dest gets only the free slots; no blocks moved
    EXPECT_EQ(dest.allocated_bytes(), 0);
    EXPECT_EQ(dest.num_slots_available(), NUM_FREE);

    // Allocating from dest consumes transferred free slots
    std::vector<int*> got;
    for (int i = 0; i < NUM_FREE; ++i)
        got.push_back(dest.allocate());
    EXPECT_EQ(dest.num_slots_available(), 0);
    // Return them; free list restored
    for (auto* p : got)
        dest.deallocate(p);
    EXPECT_EQ(dest.num_slots_available(), NUM_FREE);
}

TEST_F(TransferTest, TransferFreeNoEffectWhenNoFreeSlots)
{
    // Allocate but don't free
    for (int i = 0; i < 10; ++i)
        ASSERT_NE(allocator.allocate(), nullptr);
    EXPECT_EQ(allocator.num_slots_available(), 0);

    TestAlloc dest;
    ASSERT_NO_THROW(dest.transfer_free(allocator));

    EXPECT_EQ(dest.num_slots_available(), 0);
    EXPECT_EQ(dest.allocated_bytes(), 0);
}

TEST_F(TransferTest, TransferAllThenAllocateFromDestUsesTransferredSlots)
{
    // Allocate across multiple blocks
    std::vector<int*> ptrs;
    constexpr int NUM_ALLOC2 = 100;
    for (int i = 0; i < NUM_ALLOC2; ++i)
        ptrs.push_back(allocator.allocate());
    // Free all allocated pointers to move them to free list
    for (auto* p : ptrs)
        allocator.deallocate(p);

    const auto pred = AllocatorPrediction().alloc(NUM_ALLOC2).dealloc(NUM_ALLOC2);
    EXPECT_EQ(pred, AllocatorPrediction::of_ta(allocator));

    TestAlloc dest;
    ASSERT_NO_THROW(dest.transfer_all(allocator));

    auto [to, from] = transfer_all({AllocatorPrediction(), pred});
    EXPECT_EQ(from, AllocatorPrediction::of_ta(allocator));
    EXPECT_EQ(to, AllocatorPrediction::of_ta(dest));

    // Consume all free slots first
    std::vector<int*> got;
    got.reserve(to.slots_avail);
    for (size_t i = 0; i < to.slots_avail; ++i)
        got.push_back(dest.allocate());
    EXPECT_EQ(dest.num_slots_available(), 0);

    // Next allocation forces a new block in dest (lazy bump), increasing allocated size
    const auto before_bytes = dest.allocated_bytes();
    int* extra = dest.allocate();
    ASSERT_NE(extra, nullptr);
    EXPECT_GT(dest.allocated_bytes(), before_bytes);

    // Clean up: return all
    dest.deallocate(extra);
    for (auto* p : got)
        dest.deallocate(p);
}

// Randomized sequence test verifying allocator state against a simple model
TEST(TransferRandomized, RandomSequenceMatchesPrediction)
{
    std::mt19937 rng(1337u);
    std::uniform_int_distribution<int> opDist(0, 9); // choose among 10 operations
    constexpr int kIters = 1000;

    struct Model
    {
        int blocks_alloc = 0;
        int slots_avail = 0;
        int bump_avail = 0;
        void alloc_one()
        {
            if (slots_avail > 0)
            {
                --slots_avail;
            }
            else if (bump_avail > 0)
            {
                --bump_avail;
            }
            else
            {
                ++blocks_alloc;
                bump_avail = kSlotsPerBlock - 1; // consume one slot from the new block
            }
        }
        void dealloc_one()
        {
            ++slots_avail;
        }
        void transfer_free_to(Model& to)
        {
            to.slots_avail += slots_avail;
            slots_avail = 0;
        }
        void transfer_all_to(Model& to)
        {
            to.blocks_alloc += blocks_alloc;
            to.slots_avail += slots_avail + bump_avail;
            blocks_alloc = 0;
            slots_avail = 0;
            bump_avail = 0;
        }
    } mA, mB;

    TestAlloc A, B;
    std::vector<int*> liveA;
    std::vector<int*> liveB;

    auto check = [&](const TestAlloc& real, const Model& m)
    {
        EXPECT_EQ(real.allocated_bytes(), static_cast<size_t>(m.blocks_alloc) * kBlockSize);
        EXPECT_EQ(real.num_slots_available(), static_cast<size_t>(m.slots_avail));
        EXPECT_EQ(real.num_bump_available(), static_cast<size_t>(m.bump_avail));
    };

    for (int it = 0; it < kIters; ++it)
    {
        int op = opDist(rng);
        switch (op)
        {
        case 0: // alloc A
        {
            int* p = A.allocate();
            ASSERT_NE(p, nullptr);
            liveA.push_back(p);
            mA.alloc_one();
            break;
        }
        case 1: // alloc B
        {
            int* p = B.allocate();
            ASSERT_NE(p, nullptr);
            liveB.push_back(p);
            mB.alloc_one();
            break;
        }
        case 2: // dealloc A
        {
            if (!liveA.empty())
            {
                std::uniform_int_distribution<size_t> idx(0, liveA.size() - 1);
                size_t i = idx(rng);
                int* p = liveA[i];
                A.deallocate(p);
                liveA[i] = liveA.back();
                liveA.pop_back();
                mA.dealloc_one();
            }
            break;
        }
        case 3: // dealloc B
        {
            if (!liveB.empty())
            {
                std::uniform_int_distribution<size_t> idx(0, liveB.size() - 1);
                size_t i = idx(rng);
                int* p = liveB[i];
                B.deallocate(p);
                liveB[i] = liveB.back();
                liveB.pop_back();
                mB.dealloc_one();
            }
            break;
        }
        case 4: // transfer_free A -> B
        {
            B.transfer_free(A);
            mA.transfer_free_to(mB);
            break;
        }
        case 5: // transfer_free B -> A
        {
            A.transfer_free(B);
            mB.transfer_free_to(mA);
            break;
        }
        case 6: // transfer_all A -> B (only if no live allocations in A)
        {
            if (liveA.empty())
            {
                B.transfer_all(A);
                mA.transfer_all_to(mB);
            }
            break;
        }
        case 7: // transfer_all B -> A (only if no live allocations in B)
        {
            if (liveB.empty())
            {
                A.transfer_all(B);
                mB.transfer_all_to(mA);
            }
            break;
        }
        case 8: // bulk alloc 10 in A
        {
            for (int i = 0; i < 10; ++i)
            {
                int* p = A.allocate();
                ASSERT_NE(p, nullptr);
                liveA.push_back(p);
                mA.alloc_one();
            }
            break;
        }
        case 9: // bulk alloc 10 in B
        {
            for (int i = 0; i < 10; ++i)
            {
                int* p = B.allocate();
                ASSERT_NE(p, nullptr);
                liveB.push_back(p);
                mB.alloc_one();
            }
            break;
        }
        }

        check(A, mA);
        check(B, mB);
    }

    // Cleanup
    for (int* p : liveA)
    {
        A.deallocate(p);
        mA.dealloc_one();
    }
    for (int* p : liveB)
    {
        B.deallocate(p);
        mB.dealloc_one();
    }
    check(A, mA);
    check(B, mB);
}
