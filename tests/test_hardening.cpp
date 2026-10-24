// Use-after-free hardening (ADR-012): writes through dangling pointers must
// never make the allocator follow a corrupted link (crash or hang). They are
// either harmless or detected, after which the heap refuses further work.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "heap_test_util.h"
#include "myalloc/myalloc.h"

using myalloc::FreeStatus;
using myalloc::Heap;

namespace {

// Simulates a program writing through a dangling pointer.
void uaf_write(void* freed, std::size_t offset, std::uint64_t value) {
    std::memcpy(static_cast<char*>(freed) + offset, &value, sizeof value);
}
std::uint64_t addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

class Hardening : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(h.init(1u << 20)); }

    // [a 300 B][guard][b 100 B][guard], both freed: list is b, a, tail.
    void two_holes() {
        a = h.malloc(300);
        h.malloc(16);
        b = h.malloc(100);
        h.malloc(16);
        ASSERT_TRUE(a && b);
        h.free(a);
        h.free(b);
    }
    void expect_corrupted_and_inert() {
        EXPECT_TRUE(h.corrupted());
        EXPECT_EQ(h.malloc(16), nullptr);
        void* dummy = reinterpret_cast<void*>(addr(a));
        EXPECT_EQ(h.free(dummy), FreeStatus::HeapCorrupted);
        FreeStatus st = FreeStatus::Ok;
        EXPECT_EQ(h.realloc(nullptr, 16, &st), nullptr);
        EXPECT_EQ(st, FreeStatus::HeapCorrupted);
        const char* why = nullptr;
        EXPECT_FALSE(h.check(&why));
        (void)h.free_summary();  // bounded, must not crash or hang
    }

    Heap h;
    void* a = nullptr;
    void* b = nullptr;
};

}  // namespace

TEST_F(Hardening, GarbageNextLinkIsDetectedNotFollowed) {
    // The exact sequence that segfaulted before ADR-012.
    two_holes();
    uaf_write(b, 0, 0x4141414141414141ull);  // b.next
    EXPECT_EQ(h.malloc(5000), nullptr);       // walk reaches b, b does not fit, b.next is garbage
    expect_corrupted_and_inert();
}

TEST_F(Hardening, NextLinkToAllocatedBlockIsDetected) {
    void* live = h.malloc(40);                // allocated before the holes, so not carved from b
    two_holes();
    uaf_write(b, 0, addr(myalloc::header_of(live)));
    EXPECT_EQ(h.malloc(5000), nullptr);
    expect_corrupted_and_inert();
}

TEST_F(Hardening, NextLinkOutsideArenaIsDetected) {
    two_holes();
    int on_stack = 0;
    uaf_write(b, 0, addr(&on_stack));
    EXPECT_EQ(h.malloc(5000), nullptr);
    expect_corrupted_and_inert();
}

TEST_F(Hardening, MisalignedNextLinkInsideHeapIsDetected) {
    two_holes();
    uaf_write(b, 0, addr(myalloc::header_of(a)) + 8);
    EXPECT_EQ(h.malloc(5000), nullptr);
    expect_corrupted_and_inert();
}

TEST_F(Hardening, BrokenBackLinkOfChosenBlockIsDetectedOnUnlink) {
    two_holes();                              // list: b(112), a(304), tail
    uaf_write(a, 8, 0x1234);                  // a.prev should be b
    EXPECT_EQ(h.malloc(200), nullptr);        // search picks a; unlinking a checks a.prev first
    expect_corrupted_and_inert();
}

TEST_F(Hardening, BrokenNeighbourLinkIsDetectedOnUnlink) {
    two_holes();                              // list: b, a, tail
    uaf_write(a, 8, 0x4242);                  // a.prev should be b
    EXPECT_EQ(h.malloc(100), nullptr);        // b fits at once; unlinking b rewrites a.prev,
    expect_corrupted_and_inert();             // so safe unlinking checks it first
}

TEST_F(Hardening, SelfLoopIsDetected) {
    two_holes();
    uaf_write(b, 0, addr(myalloc::header_of(b)));  // b.next = b
    EXPECT_EQ(h.malloc(5000), nullptr);             // must terminate
    expect_corrupted_and_inert();
}

TEST_F(Hardening, TwoNodeCycleIsDetected) {
    two_holes();                                    // list: b, a, tail
    uaf_write(a, 0, addr(myalloc::header_of(b)));   // a.next = b
    EXPECT_EQ(h.malloc(5000), nullptr);             // must terminate
    expect_corrupted_and_inert();
}

TEST_F(Hardening, OverwrittenFooterIsDetectedOnBackwardMerge) {
    for (std::uint64_t bad : {std::uint64_t{0}, std::uint64_t{8}, std::uint64_t{1} << 40, std::uint64_t{64}}) {
        Heap hh;
        ASSERT_TRUE(hh.init(1u << 20));
        hh.malloc(16);
        void* x = hh.malloc(200);                   // 208 B block
        void* y = hh.malloc(200);
        hh.malloc(16);
        hh.free(x);
        // x's footer is the last 8 bytes of its block: payload offset 208 - 16.
        uaf_write(x, 208 - 16, bad);
        EXPECT_EQ(hh.free(y), FreeStatus::HeapCorrupted) << "footer value " << bad;
        EXPECT_TRUE(hh.corrupted());
        EXPECT_EQ(hh.malloc(16), nullptr);
    }
}

TEST_F(Hardening, HarmlessUafWritesAreNotFlagged) {
    // Writes into a freed block's payload that miss links and footer change
    // no metadata, so they are not (and need not be) reported.
    void* x = h.malloc(500);
    h.malloc(16);
    h.free(x);
    std::memset(static_cast<char*>(x) + 16, 0x5A, 400);
    EXPECT_NE(h.malloc(1000), nullptr);
    EXPECT_FALSE(h.corrupted());
    EXPECT_HEAP_OK(h);
}

TEST_F(Hardening, CorruptionDuringReallocIsReported) {
    void* p = h.malloc(100);
    void* q = h.malloc(300);
    h.malloc(16);
    h.free(q);                                // p's successor is free
    uaf_write(q, 0, 0x4343);                  // q.next garbage
    FreeStatus st = FreeStatus::Ok;
    EXPECT_EQ(h.realloc(p, 200, &st), nullptr);   // grow in place would unlink q
    EXPECT_EQ(st, FreeStatus::HeapCorrupted);
    EXPECT_TRUE(h.corrupted());
}

// The requirement itself: random writes through dangling pointers, mixed with
// random alloc/free/realloc, must never crash or hang the allocator. Each seed
// either runs to completion or stops at a detected corruption.
TEST(HardeningFuzz, RandomUafWritesNeverCrashOrHang) {
    std::size_t detected = 0, rejected = 0, completed = 0;
    for (unsigned seed = 1; seed <= 300; ++seed) {
        Heap h;
        ASSERT_TRUE(h.init(256u << 10));
        std::mt19937 rng(seed);
        struct Blk { void* p; std::size_t n; };
        std::vector<Blk> live, dead;
        bool stopped = false;
        for (int op = 0; op < 3000 && !stopped; ++op) {
            const unsigned r = rng() % 100;
            if (r < 5 && !dead.empty()) {
                // UAF: 1-4 random 8-byte writes anywhere in a dangling block's old range.
                const Blk& d = dead[rng() % dead.size()];
                for (unsigned k = 1 + rng() % 4; k-- > 0;) {
                    const std::size_t off = (rng() % (d.n / 8 + 1)) * 8;
                    if (off + 8 <= d.n) uaf_write(d.p, off, (std::uint64_t(rng()) << 32) | rng());
                }
            } else if (r < 50 || live.empty()) {
                const std::size_t n = 8 + rng() % 2000;
                if (void* p = h.malloc(n)) live.push_back({p, n});
            } else if (r < 90) {
                const std::size_t k = rng() % live.size();
                if (h.free(live[k].p) != FreeStatus::Ok) stopped = true;
                dead.push_back(live[k]);
                live[k] = live.back();
                live.pop_back();
            } else {
                Blk& l = live[rng() % live.size()];
                const std::size_t n = 8 + rng() % 4000;
                FreeStatus st = FreeStatus::Ok;
                if (void* q = h.realloc(l.p, n, &st)) {
                    if (q != l.p) dead.push_back(l);
                    l = {q, n};
                } else if (st != FreeStatus::Ok) {
                    stopped = true;
                }
            }
            if (h.corrupted()) stopped = true;
        }
        if (h.corrupted()) ++detected;        // metadata overwrite caught
        else if (stopped) ++rejected;         // a free/realloc of a pointer whose header was hit
        else ++completed;                     // writes were harmless
        (void)h.free_summary();  // must also be safe in whatever state we ended in
    }
    // Reaching this line is the real assertion (no crash, no hang). The counts
    // show both outcomes occur, so the test exercises detection, not luck.
    EXPECT_GT(detected, 0u);
    EXPECT_GT(completed, 0u);
    std::printf("[ fuzz ] seeds=300 detected=%zu rejected=%zu completed=%zu\n", detected, rejected,
                completed);
}

TEST(HardeningApi, CApiAbortsWithDiagnosticInsteadOfCrashing) {
    // The whole sequence runs in the death-test child, so the shared
    // process-wide heap of this test binary is never corrupted.
    EXPECT_DEATH(
        {
            void* a = my_malloc(100);
            void* guard = my_malloc(16);
            (void)guard;
            my_free(a);
            uaf_write(a, 0, 0x4141414141414141ull);
            my_malloc(5000);
        },
        "my_malloc: heap corruption detected");
    EXPECT_FALSE(myalloc::default_heap().corrupted());
}
