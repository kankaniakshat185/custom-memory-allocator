// Unit tests for block splitting (ADR-005).
#include "heap_test_util.h"

using myalloc::block_size_for;
using myalloc::FreeStatus;
using myalloc::Heap;
using myalloc::kMinBlock;

class Split : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(h.init(1u << 20)); }

    // The whole usable arena: everything between the first header and the epilogue.
    std::size_t usable() const { return h.arena().size() - 16; }

    Heap h;
};

TEST_F(Split, FreshArenaIsOneFreeBlock) {
    auto s = h.free_summary();
    EXPECT_EQ(s.count, 1u);
    EXPECT_EQ(s.bytes, usable());
    EXPECT_EQ(s.largest, usable());
    EXPECT_HEAP_OK(h);
}

TEST_F(Split, AllocationSplitsTheInitialBlock) {
    void* p = h.malloc(100);
    ASSERT_NE(p, nullptr);
    auto s = h.free_summary();
    EXPECT_EQ(s.count, 1u);
    EXPECT_EQ(s.bytes, usable() - block_size_for(100));
    EXPECT_EQ(h.stats().bytes_in_use, block_size_for(100));
    EXPECT_HEAP_OK(h);
}

TEST_F(Split, BytesAreConservedAcrossSplits) {
    for (std::size_t n : {1u, 24u, 25u, 100u, 1000u, 5000u}) ASSERT_NE(h.malloc(n), nullptr);
    EXPECT_EQ(h.stats().bytes_in_use + h.free_summary().bytes, usable());
    EXPECT_HEAP_OK(h);
}

TEST_F(Split, LargeFreedBlockServesSmallRequestAndKeepsRemainder) {
    void* big = h.malloc(1000);           // 1008 B block
    void* guard = h.malloc(16);           // keeps big from touching the tail remainder
    ASSERT_TRUE(big && guard);
    ASSERT_EQ(h.free(big), FreeStatus::Ok);

    void* small = h.malloc(16);           // 32 B block, carved from the front of big
    EXPECT_EQ(small, big);
    EXPECT_EQ(h.stats().bytes_in_use, 2 * block_size_for(16));
    void* next = h.malloc(16);            // served from big's remainder, directly after small
    EXPECT_EQ(static_cast<char*>(next), static_cast<char*>(small) + block_size_for(16));
    EXPECT_HEAP_OK(h);
}

TEST_F(Split, RemainderSmallerThanMinimumIsNotSplit) {
    // 48 B block reused for a 32 B request: remainder 16 < kMinBlock, hand out whole.
    void* a = h.malloc(40);               // 48 B block
    void* guard = h.malloc(16);
    ASSERT_TRUE(a && guard);
    h.free(a);
    void* b = h.malloc(24);               // needs 32 B
    EXPECT_EQ(b, a);
    EXPECT_EQ(h.stats().bytes_in_use, block_size_for(40) + block_size_for(16));
    EXPECT_HEAP_OK(h);
}

TEST_F(Split, RemainderExactlyMinimumIsSplit) {
    void* a = h.malloc(56);               // 64 B block
    void* guard = h.malloc(16);
    ASSERT_TRUE(a && guard);
    h.free(a);
    const std::size_t free_before = h.free_summary().count;
    void* b = h.malloc(24);               // 32 B, remainder 32 == kMinBlock
    EXPECT_EQ(b, a);
    EXPECT_EQ(h.stats().bytes_in_use, block_size_for(24) + block_size_for(16));
    EXPECT_EQ(h.free_summary().count, free_before);  // a left the list, its remainder joined
    EXPECT_HEAP_OK(h);
}

TEST_F(Split, ExactFitLeavesNoRemainder) {
    void* a = h.malloc(100);
    void* guard = h.malloc(16);
    h.free(a);
    const std::size_t free_before = h.free_summary().count;
    EXPECT_EQ(h.malloc(100), a);
    EXPECT_EQ(h.free_summary().count, free_before - 1);
    EXPECT_HEAP_OK(h);
    (void)guard;
}

TEST_F(Split, WholeArenaCanBeAllocatedInOneBlock) {
    void* p = h.malloc(usable() - 8);     // exactly the initial block
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(h.free_summary().count, 0u);
    EXPECT_EQ(h.malloc(0), nullptr);
    EXPECT_HEAP_OK(h);
    EXPECT_EQ(h.free(p), FreeStatus::Ok);
    EXPECT_HEAP_OK(h);
}

TEST_F(Split, OneByteTooManyFails) {
    EXPECT_EQ(h.malloc(usable() - 7), nullptr);
    EXPECT_HEAP_OK(h);
}

TEST_F(Split, NoCoalescingYetFreedNeighboursStaySeparate) {
    // Baseline for ADR-006: two adjacent freed blocks cannot serve a request
    // larger than either one alone, even though together they would fit.
    Heap small;
    ASSERT_TRUE(small.init(16u << 10));   // one 16 KiB page
    void* a = small.malloc(4000);
    void* b = small.malloc(4000);
    void* c = small.malloc(4000);
    // a..c are 4016 B blocks; d takes exactly what is left of the page.
    void* d = small.malloc(small.arena().size() - 16 - 3 * block_size_for(4000) - 8);
    ASSERT_TRUE(a && b && c && d);
    small.free(a);
    small.free(b);
    EXPECT_EQ(small.free_summary().count, 2u);
    EXPECT_EQ(small.malloc(6000), nullptr);
    EXPECT_HEAP_OK(small);
}
