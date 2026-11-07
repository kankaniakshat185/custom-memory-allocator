// Allocation tracking and leak reporting (ADR-013).
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "heap_test_util.h"
#include "myalloc/myalloc.h"

using myalloc::block_size_for;
using myalloc::Heap;

namespace {

std::vector<std::pair<void*, std::size_t>> live(const Heap& h, bool* complete = nullptr) {
    std::vector<std::pair<void*, std::size_t>> out;
    const bool ok = h.for_each_live([&](void* p, std::size_t n) { out.emplace_back(p, n); });
    if (complete) *complete = ok;
    return out;
}

std::string read_all(std::FILE* f) {
    std::rewind(f);
    std::string s;
    char buf[512];
    while (std::size_t n = std::fread(buf, 1, sizeof buf, f)) s.append(buf, n);
    return s;
}

}  // namespace

TEST(Tracking, ForEachLiveVisitsAllocatedBlocksInAddressOrder) {
    Heap h;
    ASSERT_TRUE(h.init(1u << 20));
    void* a = h.malloc(100);
    void* gone = h.malloc(50);
    void* b = h.malloc(1000);
    void* c = h.malloc(1);
    h.free(gone);
    const auto blocks = live(h);
    ASSERT_EQ(blocks.size(), 3u);
    EXPECT_EQ(blocks[0].first, a);
    EXPECT_EQ(blocks[1].first, b);
    EXPECT_EQ(blocks[2].first, c);
    EXPECT_EQ(blocks[0].second, block_size_for(100) - 8);
    EXPECT_EQ(blocks[1].second, block_size_for(1000) - 8);
    EXPECT_EQ(blocks[2].second, block_size_for(1) - 8);
}

TEST(Tracking, UsableSizeIsAtLeastTheRequest) {
    Heap h;
    ASSERT_TRUE(h.init(1u << 20));
    for (std::size_t n : {0u, 1u, 24u, 25u, 100u, 4095u}) {
        void* p = h.malloc(n);
        bool found = false;
        h.for_each_live([&](void* q, std::size_t usable) {
            if (q == p) {
                found = true;
                EXPECT_GE(usable, n);
                EXPECT_LT(usable, n + 16 + 24);  // rounding to 16 plus the 32 B minimum block
            }
        });
        EXPECT_TRUE(found) << n;
        h.free(p);
    }
}

TEST(Tracking, NothingLiveAfterFreeingEverything) {
    Heap h;
    ASSERT_TRUE(h.init(1u << 20));
    std::vector<void*> ptrs;
    for (int i = 0; i < 200; ++i) ptrs.push_back(h.malloc(16 + i * 7));
    EXPECT_EQ(live(h).size(), 200u);
    for (void* p : ptrs) h.free(p);
    bool complete = false;
    EXPECT_TRUE(live(h, &complete).empty());
    EXPECT_TRUE(complete);
}

TEST(Tracking, WalkStopsSafelyAtDamagedHeader) {
    // A header overwritten (e.g. by a buffer overflow from the previous block)
    // must end the walk, not send it off the end of the arena.
    Heap h;
    ASSERT_TRUE(h.init(1u << 20));
    void* a = h.malloc(100);
    void* b = h.malloc(100);
    myalloc::header_of(b)->word = 0;  // size 0 would loop forever if trusted
    bool complete = true;
    const auto blocks = live(h, &complete);
    EXPECT_FALSE(complete);
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].first, a);
}

TEST(TrackingApi, StatsFollowAllocationsAndPeak) {
    my_alloc_stats_t s0, s1, s2;
    my_alloc_stats(&s0);
    void* p = my_malloc(100);
    void* q = my_malloc(5000);
    my_alloc_stats(&s1);
    const std::size_t blocks = block_size_for(100) + block_size_for(5000);
    EXPECT_EQ(s1.live_blocks, s0.live_blocks + 2);
    EXPECT_EQ(s1.bytes_in_use, s0.bytes_in_use + blocks);
    EXPECT_EQ(s1.payload_in_use, s0.payload_in_use + blocks - 2 * 8);
    EXPECT_GE(s1.peak_bytes_in_use, s1.bytes_in_use);
    EXPECT_EQ(s1.free_bytes + s1.bytes_in_use, s1.arena_bytes);
    EXPECT_EQ(s1.corrupted, 0);
    my_free(p);
    my_free(q);
    my_alloc_stats(&s2);
    EXPECT_EQ(s2.live_blocks, s0.live_blocks);
    EXPECT_EQ(s2.bytes_in_use, s0.bytes_in_use);
    EXPECT_GE(s2.peak_bytes_in_use, s1.bytes_in_use);  // peak is a high-water mark
}

TEST(TrackingApi, ArenaIsTheDocumentedSize) {
    my_alloc_stats_t s;
    my_alloc_stats(&s);
    EXPECT_EQ(s.arena_bytes, (std::size_t{256} << 20) - 16);
}

TEST(TrackingApi, ReportListsLiveBlocksAndCountsThem) {
    void* p = my_malloc(100);
    std::FILE* f = std::tmpfile();
    ASSERT_NE(f, nullptr);
    const std::size_t n = my_report_leaks(f);
    const std::string text = read_all(f);
    std::fclose(f);

    my_alloc_stats_t s;
    my_alloc_stats(&s);
    EXPECT_EQ(n, s.live_blocks);
    char line[64];
    std::snprintf(line, sizeof line, "  %p  %zu bytes\n", p, block_size_for(100) - 8);
    EXPECT_NE(text.find(line), std::string::npos) << text;
    EXPECT_NE(text.find("live block"), std::string::npos);
    my_free(p);
}

TEST(TrackingApi, LeakReportPrintedAtExitWhenEnabled) {
    EXPECT_EXIT(
        {
            setenv("MYALLOC_LEAK_REPORT", "1", 1);
            my_malloc(100);
            std::exit(0);
        },
        ::testing::ExitedWithCode(0),
        "myalloc leak report: [0-9]+ live blocks?, [0-9]+ usable bytes");
}

TEST(TrackingApi, NoLeakReportAtExitWhenDisabled) {
    EXPECT_EXIT(
        {
            unsetenv("MYALLOC_LEAK_REPORT");
            my_malloc(100);
            std::exit(0);
        },
        ::testing::ExitedWithCode(0), "^$");
}
