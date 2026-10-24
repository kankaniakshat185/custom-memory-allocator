// Unit tests for realloc and calloc (ADR-007).
#include <cstdint>

#include "heap_test_util.h"

using myalloc::block_size;
using myalloc::block_size_for;
using myalloc::FreeStatus;
using myalloc::header_of;
using myalloc::Heap;

class Realloc : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(h.init(1u << 20)); }
    std::size_t usable() const { return h.arena().size() - 16; }
    Heap h;
};

TEST_F(Realloc, NullBehavesLikeMalloc) {
    FreeStatus st = FreeStatus::DoubleFree;
    void* p = h.realloc(nullptr, 100, &st);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(st, FreeStatus::Ok);
    EXPECT_EQ(h.stats().live_blocks, 1u);
    EXPECT_HEAP_OK(h);
}

TEST_F(Realloc, SameSizeIsANoOp) {
    void* p = h.malloc(100);
    fill(p, 100, 0x11);
    EXPECT_EQ(h.realloc(p, 100), p);
    EXPECT_EQ(h.realloc(p, 104), p);  // still fits the 112 B block
    EXPECT_TRUE(all_equal(p, 100, 0x11));
    EXPECT_EQ(h.stats().realloc_in_place, 2u);
    EXPECT_HEAP_OK(h);
}

TEST_F(Realloc, ShrinkInPlaceSplitsAndFreesTail) {
    void* p = h.malloc(1000);
    void* guard = h.malloc(16);
    fill(p, 1000, 0x22);
    const std::size_t free_before = h.free_summary().count;

    EXPECT_EQ(h.realloc(p, 100), p);
    EXPECT_TRUE(all_equal(p, 100, 0x22));
    EXPECT_EQ(block_size(header_of(p)), block_size_for(100));
    EXPECT_EQ(h.free_summary().count, free_before + 1);  // the tail, isolated by guard
    EXPECT_EQ(h.stats().bytes_in_use, block_size_for(100) + block_size_for(16));
    EXPECT_HEAP_OK(h);
    (void)guard;
}

TEST_F(Realloc, ShrinkTailCoalescesWithFreeSuccessor) {
    void* p = h.malloc(1000);
    void* q = h.malloc(1000);
    void* guard = h.malloc(16);
    h.free(q);
    const std::size_t free_before = h.free_summary().count;
    EXPECT_EQ(h.realloc(p, 100), p);
    EXPECT_EQ(h.free_summary().count, free_before);  // tail merged into q's block
    EXPECT_HEAP_OK(h);
    (void)guard;
}

TEST_F(Realloc, SmallShrinkKeepsWholeBlock) {
    void* p = h.malloc(100);  // 112 B
    EXPECT_EQ(h.realloc(p, 90), p);  // 112 - 112 = 0, nothing to split
    EXPECT_EQ(h.realloc(p, 80), p);  // needs 96 B; 16 B remainder < kMinBlock
    EXPECT_EQ(block_size(header_of(p)), block_size_for(100));
    EXPECT_HEAP_OK(h);
}

TEST_F(Realloc, ZeroShrinksToMinimumAndStaysValid) {
    void* p = h.malloc(1000);
    void* q = h.realloc(p, 0);
    EXPECT_EQ(q, p);
    EXPECT_EQ(block_size(header_of(q)), myalloc::kMinBlock);
    EXPECT_EQ(h.free(q), FreeStatus::Ok);
    EXPECT_HEAP_OK(h);
}

TEST_F(Realloc, GrowIntoFreeSuccessorInPlace) {
    void* p = h.malloc(100);
    void* q = h.malloc(1000);
    void* guard = h.malloc(16);
    fill(p, 100, 0x33);
    h.free(q);

    EXPECT_EQ(h.realloc(p, 600), p);  // 1008 B neighbour, remainder split off
    EXPECT_TRUE(all_equal(p, 100, 0x33));
    EXPECT_EQ(block_size(header_of(p)), block_size_for(600));
    EXPECT_EQ(h.stats().realloc_in_place, 1u);
    EXPECT_EQ(h.stats().realloc_moved, 0u);
    EXPECT_HEAP_OK(h);
    (void)guard;
}

TEST_F(Realloc, GrowAbsorbsWholeSuccessorWhenRemainderTooSmall) {
    void* p = h.malloc(100);  // 112 B
    void* q = h.malloc(40);   // 48 B
    void* guard = h.malloc(16);
    h.free(q);
    // Need 144 B of 160 available: 16 B remainder is absorbed.
    EXPECT_EQ(h.realloc(p, 136), p);
    EXPECT_EQ(block_size(header_of(p)), 160u);
    EXPECT_HEAP_OK(h);
    (void)guard;
}

TEST_F(Realloc, GrowIntoTailInPlace) {
    void* p = h.malloc(100);  // last block before the big tail
    fill(p, 100, 0x44);
    EXPECT_EQ(h.realloc(p, 50000), p);
    EXPECT_TRUE(all_equal(p, 100, 0x44));
    EXPECT_HEAP_OK(h);
}

TEST_F(Realloc, GrowMovesWhenSuccessorIsAllocated) {
    void* p = h.malloc(100);
    void* blocker = h.malloc(16);
    fill(p, 100, 0x55);

    void* q = h.realloc(p, 500);
    ASSERT_NE(q, nullptr);
    EXPECT_NE(q, p);
    EXPECT_TRUE(all_equal(q, 100, 0x55));
    EXPECT_EQ(h.stats().realloc_moved, 1u);
    EXPECT_EQ(h.stats().realloc_bytes_copied, block_size_for(100) - 8);
    EXPECT_EQ(h.stats().live_blocks, 2u);  // q and blocker; p was freed
    EXPECT_EQ(h.free(p), FreeStatus::DoubleFree);
    EXPECT_HEAP_OK(h);
    (void)blocker;
}

TEST_F(Realloc, GrowMovesWhenSuccessorFreeButTooSmall) {
    void* p = h.malloc(100);
    void* q = h.malloc(16);
    void* blocker = h.malloc(16);
    h.free(q);
    void* r = h.realloc(p, 500);
    EXPECT_NE(r, p);
    EXPECT_EQ(h.stats().realloc_moved, 1u);
    EXPECT_HEAP_OK(h);
    (void)blocker;
}

TEST_F(Realloc, FailureLeavesOriginalIntact) {
    void* p = h.malloc(100);
    void* blocker = h.malloc(16);
    fill(p, 100, 0x66);
    const auto before = h.stats();

    EXPECT_EQ(h.realloc(p, usable()), nullptr);  // can neither grow nor move
    EXPECT_TRUE(all_equal(p, 100, 0x66));
    EXPECT_EQ(h.stats().live_blocks, before.live_blocks);
    EXPECT_EQ(h.stats().bytes_in_use, before.bytes_in_use);
    EXPECT_EQ(h.free(p), FreeStatus::Ok);  // still a valid allocation
    EXPECT_HEAP_OK(h);
    (void)blocker;
}

TEST_F(Realloc, HugeSizeFailsWithoutOverflow) {
    void* p = h.malloc(100);
    EXPECT_EQ(h.realloc(p, SIZE_MAX), nullptr);
    EXPECT_EQ(h.realloc(p, SIZE_MAX - 8), nullptr);
    EXPECT_EQ(h.free(p), FreeStatus::Ok);
    EXPECT_HEAP_OK(h);
}

TEST_F(Realloc, InvalidAndFreedPointersAreReported) {
    int local = 0;
    FreeStatus st = FreeStatus::Ok;
    EXPECT_EQ(h.realloc(&local, 10, &st), nullptr);
    EXPECT_EQ(st, FreeStatus::InvalidPointer);

    void* p = h.malloc(64);
    h.free(p);
    EXPECT_EQ(h.realloc(p, 10, &st), nullptr);
    EXPECT_EQ(st, FreeStatus::DoubleFree);
    EXPECT_HEAP_OK(h);
}

TEST_F(Realloc, RepeatedGrowthLikeAVector) {
    // Doubling growth of one buffer with nothing else allocated stays in place.
    std::size_t cap = 16;
    auto* buf = static_cast<std::uint8_t*>(h.malloc(cap));
    for (std::size_t i = 0; i < cap; ++i) buf[i] = static_cast<std::uint8_t>(i);
    void* const original = buf;
    while (cap < (256u << 10)) {
        const std::size_t old = cap;
        cap *= 2;
        buf = static_cast<std::uint8_t*>(h.realloc(buf, cap));
        ASSERT_NE(buf, nullptr);
        for (std::size_t i = 0; i < old; ++i) ASSERT_EQ(buf[i], static_cast<std::uint8_t>(i));
        for (std::size_t i = old; i < cap; ++i) buf[i] = static_cast<std::uint8_t>(i);
    }
    EXPECT_EQ(static_cast<void*>(buf), original);
    EXPECT_EQ(h.stats().realloc_moved, 0u);
    EXPECT_HEAP_OK(h);
}

class Calloc : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(h.init(1u << 20)); }
    Heap h;
};

TEST_F(Calloc, ReturnsZeroedMemoryEvenWhenReusingDirtyBlock) {
    void* dirty = h.malloc(500);
    void* guard = h.malloc(16);
    fill(dirty, 500, 0xFF);
    h.free(dirty);
    void* p = h.calloc(50, 10);
    EXPECT_EQ(p, dirty);  // same block reused...
    EXPECT_TRUE(all_equal(p, 500, 0));  // ...but zeroed
    EXPECT_HEAP_OK(h);
    (void)guard;
}

TEST_F(Calloc, MultiplicationOverflowFails) {
    EXPECT_EQ(h.calloc(SIZE_MAX / 2, 3), nullptr);
    EXPECT_EQ(h.calloc(SIZE_MAX, SIZE_MAX), nullptr);
    EXPECT_EQ(h.stats().live_blocks, 0u);
    EXPECT_HEAP_OK(h);
}

TEST_F(Calloc, ZeroCountOrSizeGivesValidPointer) {
    void* a = h.calloc(0, 8);
    void* b = h.calloc(8, 0);
    ASSERT_TRUE(a && b);
    EXPECT_NE(a, b);
    EXPECT_EQ(h.free(a), FreeStatus::Ok);
    EXPECT_EQ(h.free(b), FreeStatus::Ok);
}

TEST_F(Calloc, ExhaustionReturnsNull) {
    EXPECT_EQ(h.calloc(1, h.arena().size()), nullptr);
    EXPECT_HEAP_OK(h);
}
