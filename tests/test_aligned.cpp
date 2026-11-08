// Aligned allocation in the heap (ADR-014).
#include <cstdint>
#include <random>
#include <vector>

#include "heap_test_util.h"

using myalloc::FreeStatus;
using myalloc::Heap;

TEST(Aligned, PayloadIsAlignedForEveryPowerOfTwo) {
    Heap h;
    ASSERT_TRUE(h.init(4u << 20));
    std::vector<void*> ptrs;
    for (std::size_t a = 16; a <= (64u << 10); a *= 2) {
        for (std::size_t n : {1u, 100u, 5000u}) {
            void* p = h.aligned_malloc(a, n);
            ASSERT_NE(p, nullptr) << a << " " << n;
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % a, 0u) << a;
            fill(p, n, static_cast<std::uint8_t>(a));
            ptrs.push_back(p);
            EXPECT_HEAP_OK(h);
        }
    }
    for (void* p : ptrs) EXPECT_EQ(h.free(p), FreeStatus::Ok);
    EXPECT_HEAP_OK(h);
    EXPECT_EQ(h.free_summary().count, 1u);  // front gaps and tails all merged back
}

TEST(Aligned, InvalidAlignmentIsRejected) {
    Heap h;
    ASSERT_TRUE(h.init(1u << 20));
    EXPECT_EQ(h.aligned_malloc(48, 10), nullptr);        // not a power of two
    EXPECT_EQ(h.aligned_malloc(1u << 21, 10), nullptr);  // larger than the arena
    EXPECT_NE(h.aligned_malloc(8, 10), nullptr);         // <= 16: plain malloc
    EXPECT_HEAP_OK(h);
}

TEST(Aligned, ExactSlabSizedBlocksTileWithoutGaps) {
    // 16 KiB-aligned payloads in blocks of exactly 16 KiB: consecutive ones
    // must be 16 KiB apart (the slab layout of ADR-014).
    Heap h;
    ASSERT_TRUE(h.init(1u << 20));
    void* a = h.aligned_malloc(16384, 16384 - 8);
    void* b = h.aligned_malloc(16384, 16384 - 8);
    ASSERT_TRUE(a && b);
    EXPECT_EQ(myalloc::block_size(myalloc::header_of(a)), 16384u);
    EXPECT_EQ(static_cast<char*>(b) - static_cast<char*>(a), 16384);
    EXPECT_HEAP_OK(h);
}

TEST(Aligned, MixedWithOrdinaryAllocationsUnderStress) {
    Heap h;
    ASSERT_TRUE(h.init(8u << 20));
    std::mt19937 rng(5);
    struct Blk {
        unsigned char* p;
        std::size_t n;
        std::uint8_t tag;
    };
    std::vector<Blk> live;
    for (int i = 0; i < 5000; ++i) {
        if (live.empty() || rng() % 2) {
            const std::size_t n = 1 + rng() % 3000;
            void* p =
                rng() % 3 ? h.malloc(n) : h.aligned_malloc(std::size_t{16} << (rng() % 10), n);
            if (!p) continue;
            const auto tag = static_cast<std::uint8_t>(rng());
            fill(p, n, tag);
            live.push_back({static_cast<unsigned char*>(p), n, tag});
        } else {
            const std::size_t k = rng() % live.size();
            ASSERT_TRUE(all_equal(live[k].p, live[k].n, live[k].tag));
            ASSERT_EQ(h.free(live[k].p), FreeStatus::Ok);
            live[k] = live.back();
            live.pop_back();
        }
        const char* why = nullptr;
        ASSERT_TRUE(h.check(&why)) << i << ": " << why;
    }
}
