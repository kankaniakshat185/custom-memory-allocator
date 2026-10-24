// Randomised stress: heap.check() after every operation, content integrity
// verified on free, small arena so OOM paths are exercised constantly.
#include <random>
#include <vector>

#include "heap_test_util.h"

namespace {
struct Live {
    unsigned char* p;
    std::size_t n;
    std::uint8_t tag;
};

std::size_t run(unsigned seed, std::size_t arena, std::size_t max_size, int ops) {
    myalloc::Heap h;
    if (!h.init(arena)) { ADD_FAILURE() << "arena init"; return 0; }
    std::mt19937 rng(seed);
    std::vector<Live> live;
    std::size_t oom = 0;
    for (int i = 0; i < ops; ++i) {
        const bool do_alloc = live.empty() || (rng() % 100) < 55;
        if (do_alloc) {
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
