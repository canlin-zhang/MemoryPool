#include <pool_allocator/pool_allocator.h>

#include <gtest/gtest.h>
#include <cstddef>

using pool_allocator_detail::SlotList;

namespace
{
struct Slot
{
    alignas(void*) unsigned char bytes[16]; // >= sizeof(T*)
};
} // namespace

TEST(SlotList, PushPopIsLifo)
{
    Slot a, b;
    SlotList<Slot> l;
    EXPECT_TRUE(l.empty());
    l.push(&a);
    l.push(&b);
    EXPECT_EQ(l.size(), 2u);
    EXPECT_EQ(l.pop(), &b); // LIFO
    EXPECT_EQ(l.pop(), &a);
    EXPECT_EQ(l.pop(), nullptr);
    EXPECT_TRUE(l.empty());
}

TEST(SlotList, SpliceMovesAllNodesNoexcept)
{
    Slot a, b, c;
    SlotList<Slot> dst, src;
    dst.push(&a);
    src.push(&b);
    src.push(&c);
    static_assert(noexcept(dst.splice(src)), "splice must be noexcept");
    dst.splice(src);
    EXPECT_EQ(dst.size(), 3u);
    EXPECT_TRUE(src.empty());
}

TEST(SlotList, MoveLeavesSourceEmpty)
{
    Slot a, b;
    SlotList<Slot> src;
    src.push(&a);
    src.push(&b);
    SlotList<Slot> dst(std::move(src));
    EXPECT_EQ(dst.size(), 2u);
    EXPECT_TRUE(src.empty()); // moved-from source is empty, no aliasing
}
