// Unit tests for the fit policies (ADR-009). These use the policy types
// directly, so they run identically in both test binaries.
#include <type_traits>
#include <vector>

#include "heap_test_util.h"

using myalloc::BestFitHeap;
using myalloc::block_size_for;
using myalloc::FirstFitHeap;

namespace {

// Lays out free blocks of the given payload sizes, each pinned by a live
// guard so they cannot coalesce, freed in order (so LIFO lists them reversed).
template <class H>
std::vector<void*> holes(H& h, std::initializer_list<std::size_t> sizes) {
    std::vector<void*> blocks;
    for (std::size_t n : sizes) {
        blocks.push_back(h.malloc(n));
        h.malloc(16);  // guard
    }
    for (void* p : blocks) h.free(p);
    return blocks;
}

}  // namespace

TEST(FitPolicy, FirstFitTakesFirstListedBlockThatFits) {
    FirstFitHeap h;
    ASSERT_TRUE(h.init(1u << 20));
    auto b = holes(h, {1000, 200, 500});  // list order: 500, 200, 1000, tail
    EXPECT_EQ(h.malloc(150), b[2]);        // 500 is first and fits
    EXPECT_HEAP_OK(h);
}

TEST(FitPolicy, BestFitTakesSmallestBlockThatFits) {
    BestFitHeap h;
    ASSERT_TRUE(h.init(1u << 20));
    auto b = holes(h, {1000, 200, 500});
    EXPECT_EQ(h.malloc(150), b[1]);        // 200 is the tightest fit
    EXPECT_EQ(h.malloc(400), b[2]);        // then 500
    EXPECT_EQ(h.malloc(900), b[0]);        // then 1000, not the large tail
    EXPECT_HEAP_OK(h);
}

TEST(FitPolicy, BestFitStopsAtExactMatch) {
    BestFitHeap h;
    ASSERT_TRUE(h.init(1u << 20));
    auto b = holes(h, {300, 100, 100, 100});  // list: 100, 100, 100, 300, tail
    const std::size_t before = h.stats().search_steps;
    EXPECT_EQ(h.malloc(100), b[3]);           // first exact match in list order
    EXPECT_EQ(h.stats().search_steps - before, 1u);
}

TEST(FitPolicy, BestFitScansWholeListWithoutExactMatch) {
    BestFitHeap h;
    ASSERT_TRUE(h.init(1u << 20));
    holes(h, {300, 200, 400});                // list: 400, 200, 300, tail
    const std::size_t before = h.stats().search_steps;
    ASSERT_NE(h.malloc(150), nullptr);        // best is 200, but must see all 4
    EXPECT_EQ(h.stats().search_steps - before, 4u);
}

TEST(FitPolicy, FirstFitStepsStopAtFirstFit) {
    FirstFitHeap h;
    ASSERT_TRUE(h.init(1u << 20));
    holes(h, {300, 200, 400});                // list: 400, 200, 300, tail
    const std::size_t before = h.stats().search_steps;
    ASSERT_NE(h.malloc(150), nullptr);        // 400 fits immediately
    EXPECT_EQ(h.stats().search_steps - before, 1u);
}

TEST(FitPolicy, BothPoliciesReturnNullWhenNothingFits) {
    FirstFitHeap f;
    BestFitHeap b;
    ASSERT_TRUE(f.init(16u << 10));
    ASSERT_TRUE(b.init(16u << 10));
    EXPECT_EQ(f.malloc(32u << 10), nullptr);
    EXPECT_EQ(b.malloc(32u << 10), nullptr);
    EXPECT_HEAP_OK(f);
    EXPECT_HEAP_OK(b);
}

TEST(FitPolicy, BestFitSplitsTheChosenBlock) {
    BestFitHeap h;
    ASSERT_TRUE(h.init(1u << 20));
    auto b = holes(h, {1000, 600});
    void* p = h.malloc(100);                  // best is 600; remainder split off
    EXPECT_EQ(p, b[1]);
    EXPECT_EQ(myalloc::block_size(myalloc::header_of(p)), block_size_for(100));
    EXPECT_HEAP_OK(h);
}

TEST(FitPolicy, BuildUsesExpectedDefaultPolicy) {
    // Guards against the MYALLOC_BEST_FIT define not reaching this binary.
    EXPECT_EQ((std::is_same_v<myalloc::Heap, BestFitHeap>), kBestFit);
    EXPECT_EQ((std::is_same_v<decltype(myalloc::default_heap()), BestFitHeap&>), kBestFit);
}
