// Unit tests for coalescing with boundary tags (ADR-006).
#include <algorithm>
#include <random>
#include <vector>

#include "heap_test_util.h"

using myalloc::block_size_for;
using myalloc::FreeStatus;
using myalloc::Header;
using myalloc::header_of;
using myalloc::Heap;

class Coalesce : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(h.init(1u << 20)); }

    std::size_t usable() const { return h.arena().size() - 16; }
    static Header* hdr(void* p) { return header_of(p); }

    // Allocates [a][b][c][guard] so the tail never takes part in a merge.
    void abc(std::size_t n = 100) {
        a = h.malloc(n);
        b = h.malloc(n);
        c = h.malloc(n);
        guard = h.malloc(16);
        ASSERT_TRUE(a && b && c && guard);
        blk = block_size_for(n);
    }

    Heap h;
    void *a = nullptr, *b = nullptr, *c = nullptr, *guard = nullptr;
    std::size_t blk = 0;
};

TEST_F(Coalesce, IsolatedFreeDoesNotMerge) {
    abc();
    const std::size_t before = h.free_summary().count;
    ASSERT_EQ(h.free(b), FreeStatus::Ok);
    EXPECT_EQ(h.free_summary().count, before + 1);
    EXPECT_HEAP_OK(h);
}

TEST_F(Coalesce, MergesWithNextBlock) {
    abc();
    h.free(c);
    h.free(b);  // b absorbs c
    const auto s = h.free_summary();
    EXPECT_EQ(s.count, 2u);  // b+c, and the tail behind guard
    EXPECT_EQ(myalloc::block_size(hdr(b)), 2 * blk);
    EXPECT_HEAP_OK(h);
}

TEST_F(Coalesce, MergesWithPreviousBlock) {
    abc();
    h.free(a);
    h.free(b);  // a absorbs b
    EXPECT_EQ(h.free_summary().count, 2u);
    EXPECT_EQ(myalloc::block_size(hdr(a)), 2 * blk);
    EXPECT_HEAP_OK(h);
}

TEST_F(Coalesce, MergesBothDirectionsAtOnce) {
    abc();
    h.free(a);
    h.free(c);
    EXPECT_EQ(h.free_summary().count, 3u);
    h.free(b);  // a absorbs b and c
    EXPECT_EQ(h.free_summary().count, 2u);
    EXPECT_EQ(myalloc::block_size(hdr(a)), 3 * blk);
    EXPECT_HEAP_OK(h);
}

TEST_F(Coalesce, LastBlockMergesWithTail) {
    void* p = h.malloc(100);
    ASSERT_NE(p, nullptr);
    h.free(p);
    const auto s = h.free_summary();
    EXPECT_EQ(s.count, 1u);
    EXPECT_EQ(s.largest, usable());
    EXPECT_HEAP_OK(h);
}

TEST_F(Coalesce, FirstBlockNeverLooksBeforeArena) {
    // The first block's prev-alloc bit is set at init, so freeing it must not
    // read a footer before the arena start.
    void* first = h.malloc(100);
    void* second = h.malloc(100);
    ASSERT_TRUE(first && second);
    EXPECT_TRUE(myalloc::prev_alloc(hdr(first)));
    h.free(first);
    EXPECT_HEAP_OK(h);
}

TEST_F(Coalesce, PrevAllocBitTracksNeighbour) {
    abc();
    EXPECT_TRUE(myalloc::prev_alloc(hdr(b)));
    h.free(a);
    EXPECT_FALSE(myalloc::prev_alloc(hdr(b)));
    void* again = h.malloc(100);  // exact refit of a
    EXPECT_EQ(again, a);
    EXPECT_TRUE(myalloc::prev_alloc(hdr(b)));
    EXPECT_HEAP_OK(h);
}

TEST_F(Coalesce, MergedNeighboursServeALargerRequest) {
    // Flipped version of the Phase 2 baseline test: two adjacent freed blocks
    // now satisfy a request larger than either alone.
    Heap small;
    ASSERT_TRUE(small.init(16u << 10));  // one 16 KiB page
    void* p1 = small.malloc(4000);
    void* p2 = small.malloc(4000);
    void* p3 = small.malloc(4000);
    void* p4 = small.malloc(small.arena().size() - 16 - 3 * block_size_for(4000) - 8);
    ASSERT_TRUE(p1 && p2 && p3 && p4);
    small.free(p1);
    small.free(p2);
    EXPECT_EQ(small.free_summary().count, 1u);
    EXPECT_EQ(small.malloc(6000), p1);
    EXPECT_HEAP_OK(small);
}

TEST_F(Coalesce, FreeingEverythingInAnyOrderRestoresOneBlock) {
    std::mt19937 rng(7);
    for (int round = 0; round < 20; ++round) {
        std::vector<void*> ptrs;
        for (int i = 0; i < 300; ++i) {
            void* p = h.malloc(rng() % 2000);
            ASSERT_NE(p, nullptr);
            ptrs.push_back(p);
        }
        std::shuffle(ptrs.begin(), ptrs.end(), rng);
        for (void* p : ptrs) ASSERT_EQ(h.free(p), FreeStatus::Ok);
        const auto s = h.free_summary();
        ASSERT_EQ(s.count, 1u) << "round " << round;
        ASSERT_EQ(s.largest, usable());
        EXPECT_HEAP_OK(h);
    }
}

TEST_F(Coalesce, DoubleFreeOfAbsorbedBlockIsDetected) {
    abc();
    h.free(a);
    ASSERT_EQ(h.free(b), FreeStatus::Ok);  // b's header is now inside a's free block
    EXPECT_EQ(h.free(b), FreeStatus::DoubleFree);
    EXPECT_HEAP_OK(h);
}

TEST_F(Coalesce, DoubleFreeOfAbsorbingBlockIsDetected) {
    abc();
    h.free(c);
    ASSERT_EQ(h.free(b), FreeStatus::Ok);  // b absorbs c
    EXPECT_EQ(h.free(b), FreeStatus::DoubleFree);
    EXPECT_EQ(h.free(c), FreeStatus::DoubleFree);
    EXPECT_HEAP_OK(h);
}

TEST_F(Coalesce, FooterHoldsSizeOnly) {
    abc();
    h.free(b);
    Header* hb = hdr(b);
    EXPECT_EQ(myalloc::footer(hb)->word, myalloc::block_size(hb));
    EXPECT_TRUE(myalloc::prev_alloc(hb));  // flag lives in the header only
}
