// Randomised stress: heap.check() after every operation, content integrity
// verified on free and on realloc, small arena so OOM paths are exercised
// constantly. realloc_pct = 0 keeps the original Phase 1-3 op sequence, so
// those configurations stay comparable across phases.
#include <random>
#include <vector>

#include "heap_test_util.h"

namespace {
struct Live {
    unsigned char* p;
    std::size_t n;
    std::uint8_t tag;
};

std::size_t run(unsigned seed, std::size_t arena, std::size_t max_size, int ops,
                unsigned realloc_pct = 0) {
    myalloc::Heap h;
    if (!h.init(arena)) { ADD_FAILURE() << "arena init"; return 0; }
    std::mt19937 rng(seed);
    std::vector<Live> live;
    std::size_t oom = 0;
    for (int i = 0; i < ops; ++i) {
        const bool do_alloc = live.empty() || (rng() % 100) < 55;
        if (!live.empty() && realloc_pct && rng() % 100 < realloc_pct) {
            Live& l = live[rng() % live.size()];
            const std::size_t n = rng() % max_size;
            EXPECT_TRUE(all_equal(l.p, l.n, l.tag)) << "corruption before realloc, op " << i;
            auto* q = static_cast<unsigned char*>(h.realloc(l.p, n));
            if (!q) {  // OOM: original must be untouched
                ++oom;
                EXPECT_TRUE(all_equal(l.p, l.n, l.tag)) << "failed realloc clobbered, op " << i;
            } else {
                const std::size_t kept = n < l.n ? n : l.n;
                EXPECT_TRUE(all_equal(q, kept, l.tag)) << "realloc lost data, op " << i;
                fill(q, n, l.tag);
                l.p = q;
                l.n = n;
            }
        } else if (do_alloc) {
            std::size_t n = rng() % max_size;
            auto* p = static_cast<unsigned char*>(h.malloc(n));
            if (!p) { ++oom; continue; }
            auto tag = static_cast<std::uint8_t>(rng());
            fill(p, n, tag);
            live.push_back({p, n, tag});
        } else {
            std::size_t k = rng() % live.size();
            EXPECT_TRUE(all_equal(live[k].p, live[k].n, live[k].tag)) << "corruption, op " << i;
            EXPECT_EQ(h.free(live[k].p), myalloc::FreeStatus::Ok);
            live[k] = live.back();
            live.pop_back();
        }
        const char* why = nullptr;
        if (!h.check(&why)) {
            ADD_FAILURE() << "op " << i << ": " << (why ? why : "?");
            return oom;
        }
    }
    for (auto& l : live) {
        EXPECT_TRUE(all_equal(l.p, l.n, l.tag));
        EXPECT_EQ(h.free(l.p), myalloc::FreeStatus::Ok);
    }
    EXPECT_HEAP_OK(h);
    EXPECT_EQ(h.stats().live_blocks, 0u);
    // With coalescing, freeing everything must restore the pristine arena.
    const auto f = h.free_summary();
    EXPECT_EQ(f.count, 1u);
    EXPECT_EQ(f.largest, h.arena().size() - 16);
    return oom;
}
}  // namespace

TEST(Stress, SmallSizes) { for (unsigned s = 1; s <= 5; ++s) run(s, 1u << 20, 128, 20000); }
TEST(Stress, MixedSizes) { for (unsigned s = 1; s <= 5; ++s) run(s, 4u << 20, 8192, 10000); }
TEST(Stress, TinyArenaConstantOom) {
    std::size_t oom = 0;
    for (unsigned s = 1; s <= 5; ++s) oom += run(s, 64u << 10, 4096, 10000);
    EXPECT_GT(oom, 0u) << "test is meant to exercise the out-of-memory path";
}
TEST(Stress, WithReallocMixedSizes) {
    for (unsigned s = 1; s <= 5; ++s) run(s, 4u << 20, 8192, 10000, 25);
}
TEST(Stress, WithReallocTinyArena) {
    std::size_t oom = 0;
    for (unsigned s = 1; s <= 5; ++s) oom += run(s, 64u << 10, 4096, 10000, 25);
    EXPECT_GT(oom, 0u) << "test is meant to exercise failed realloc";
}
