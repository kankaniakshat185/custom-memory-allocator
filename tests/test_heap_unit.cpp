// Unit tests: a fresh small Heap per test, exercising one behaviour at a time.
#include <set>
#include <vector>

#include "heap_test_util.h"

using myalloc::FreeStatus;
using myalloc::Heap;

class HeapUnit : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(h.init(1u << 20)); }
    Heap h;
};

TEST_F(HeapUnit, FreshHeapIsConsistent) { EXPECT_HEAP_OK(h); }

TEST_F(HeapUnit, AllocReturnsUsableMemory) {
    void* p = h.malloc(100);
    ASSERT_NE(p, nullptr);
    fill(p, 100, 0x5A);
    EXPECT_TRUE(all_equal(p, 100, 0x5A));
    EXPECT_HEAP_OK(h);
    EXPECT_EQ(h.free(p), FreeStatus::Ok);
    EXPECT_HEAP_OK(h);
}

TEST_F(HeapUnit, ZeroSizeGivesUniqueFreeablePointers) {
    void* a = h.malloc(0);
    void* b = h.malloc(0);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b);
    EXPECT_EQ(h.free(a), FreeStatus::Ok);
    EXPECT_EQ(h.free(b), FreeStatus::Ok);
    EXPECT_HEAP_OK(h);
}

TEST_F(HeapUnit, FreeNullIsNoop) {
    EXPECT_EQ(h.free(nullptr), FreeStatus::Ok);
    EXPECT_HEAP_OK(h);
}

TEST_F(HeapUnit, BlockSizeRoundingAndMinimum) {
    EXPECT_EQ(myalloc::block_size_for(0), 32u);
    EXPECT_EQ(myalloc::block_size_for(1), 32u);
    EXPECT_EQ(myalloc::block_size_for(24), 32u);   // 24 + 8 header = 32
    EXPECT_EQ(myalloc::block_size_for(25), 48u);
    EXPECT_EQ(myalloc::block_size_for(40), 48u);
    EXPECT_EQ(myalloc::block_size_for(41), 64u);
}

TEST_F(HeapUnit, AllocationsDoNotOverlap) {
    struct Blk { unsigned char* p; std::size_t n; };
    std::vector<Blk> blocks;
    for (std::size_t n : {1u, 7u, 16u, 24u, 25u, 100u, 1000u, 33u, 4096u}) {
        auto* p = static_cast<unsigned char*>(h.malloc(n));
        ASSERT_NE(p, nullptr);
        fill(p, n, static_cast<std::uint8_t>(n));
        blocks.push_back({p, n});
    }
    for (const auto& b : blocks)
        EXPECT_TRUE(all_equal(b.p, b.n, static_cast<std::uint8_t>(b.n))) << "size " << b.n;
    EXPECT_HEAP_OK(h);
}

TEST_F(HeapUnit, FreedBlockIsReusedLifo) {
    void* a = h.malloc(64);
    void* b = h.malloc(64);
    ASSERT_TRUE(a && b);
    h.free(a);
    h.free(b);
    EXPECT_EQ(h.malloc(64), b);  // most recently freed first
    EXPECT_EQ(h.malloc(64), a);
    EXPECT_HEAP_OK(h);
}

TEST_F(HeapUnit, FirstFitSkipsTooSmallBlocks) {
    void* small = h.malloc(16);
    void* big = h.malloc(200);
    ASSERT_TRUE(small && big);
    h.free(big);
    h.free(small);  // list head: small, then big
    void* p = h.malloc(150);  // small can't serve it; big can
    EXPECT_EQ(p, big);
    EXPECT_HEAP_OK(h);
}

TEST_F(HeapUnit, OutOfMemoryReturnsNullAndHeapSurvives) {
    std::vector<void*> ptrs;
    while (void* p = h.malloc(4096)) ptrs.push_back(p);
    EXPECT_GT(ptrs.size(), 100u);
    EXPECT_EQ(h.malloc(4096), nullptr);
    EXPECT_HEAP_OK(h);
    for (void* p : ptrs) EXPECT_EQ(h.free(p), FreeStatus::Ok);
    EXPECT_HEAP_OK(h);
    EXPECT_NE(h.malloc(4096), nullptr);  // recovers after frees
}

TEST_F(HeapUnit, HugeRequestsFailCleanly) {
    EXPECT_EQ(h.malloc(SIZE_MAX), nullptr);
    EXPECT_EQ(h.malloc(SIZE_MAX - 8), nullptr);
    EXPECT_EQ(h.malloc(h.arena().size()), nullptr);
    EXPECT_HEAP_OK(h);
}

TEST_F(HeapUnit, DoubleFreeIsDetected) {
    void* p = h.malloc(48);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(h.free(p), FreeStatus::Ok);
    EXPECT_EQ(h.free(p), FreeStatus::DoubleFree);
    EXPECT_HEAP_OK(h);  // bookkeeping untouched by the rejected free
}

TEST_F(HeapUnit, InvalidPointersAreRejected) {
    int local = 0;
    EXPECT_EQ(h.free(&local), FreeStatus::InvalidPointer);
    void* p = h.malloc(64);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(h.free(static_cast<char*>(p) + 8), FreeStatus::InvalidPointer);  // misaligned
    void* past_end = static_cast<char*>(h.arena().base()) + h.arena().size();
    EXPECT_EQ(h.free(past_end), FreeStatus::InvalidPointer);  // outside the arena
    EXPECT_HEAP_OK(h);
}

TEST_F(HeapUnit, UseAfterFreeWriteDoesNotBreakOtherBlocks) {
    // Writing into the payload of a freed block clobbers its free-list links
    // (documented limitation), but must not touch neighbouring live blocks.
    void* a = h.malloc(64);
    void* b = h.malloc(64);
    fill(b, 64, 0x77);
    h.free(a);
    EXPECT_TRUE(all_equal(b, 64, 0x77));
}

TEST_F(HeapUnit, StatsTrackUsage) {
    void* a = h.malloc(100);
    void* b = h.malloc(200);
    EXPECT_EQ(h.stats().live_blocks, 2u);
    const std::size_t both = myalloc::block_size_for(100) + myalloc::block_size_for(200);
    EXPECT_EQ(h.stats().bytes_in_use, both);
    h.free(a);
    h.free(b);
    EXPECT_EQ(h.stats().live_blocks, 0u);
    EXPECT_EQ(h.stats().bytes_in_use, 0u);
    EXPECT_EQ(h.stats().peak_in_use, both);
    EXPECT_EQ(h.stats().free_blocks, 3u);  // a, b, and the never-allocated remainder
}
