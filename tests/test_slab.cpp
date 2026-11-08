// Small-object slab cache, slabs carved from the heap (ADR-014).
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <set>
#include <vector>

#include "allocator.h"
#include "heap_test_util.h"
#include "slab.h"

using Slabs = myalloc::SlabCache<myalloc::Heap>;
using FR = Slabs::FreeResult;

namespace {

#define EXPECT_SLAB_OK(heap, cache)                                                 \
    do {                                                                            \
        const char* why_ = nullptr;                                                 \
        EXPECT_TRUE((heap).check(&why_)) << "heap check: " << (why_ ? why_ : "?");  \
        EXPECT_TRUE((cache).check(&why_)) << "slab check: " << (why_ ? why_ : "?"); \
    } while (0)

class Slab : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(h.init(1u << 20));
        ASSERT_TRUE(c.init(h));
    }
    myalloc::Heap h;
    Slabs c;
};

}  // namespace

TEST_F(Slab, ServesEverySizeUpTo256InItsClass) {
    for (std::size_t n = 0; n <= Slabs::kMaxSize; ++n) {
        void* p = c.malloc(n);
        ASSERT_NE(p, nullptr) << n;
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % 16, 0u) << n;
        EXPECT_EQ(c.usable_size(p), Slabs::slot_size(Slabs::class_of(n)));
        EXPECT_GE(c.usable_size(p), n);
    }
    EXPECT_SLAB_OK(h, c);
}

TEST_F(Slab, LargerRequestsAreNotServed) {
    EXPECT_EQ(c.malloc(Slabs::kMaxSize + 1), nullptr);
    EXPECT_EQ(c.malloc(SIZE_MAX), nullptr);
    EXPECT_EQ(c.stats().live_objects, 0u);
}

TEST_F(Slab, SlabsAreExactSixteenKiBHeapBlocks) {
    void* p = c.malloc(16);
    const auto slab = reinterpret_cast<std::uintptr_t>(p) & ~std::uintptr_t{Slabs::kSlabBytes - 1};
    EXPECT_EQ(slab % Slabs::kSlabBytes, 0u);
    EXPECT_EQ(myalloc::block_size(myalloc::header_of(reinterpret_cast<void*>(slab))),
              Slabs::kSlabBytes);
    EXPECT_EQ(h.stats().live_blocks, 1u);  // the slab is one live heap block
    EXPECT_SLAB_OK(h, c);
}

TEST_F(Slab, ObjectsDoNotOverlap) {
    std::vector<std::pair<unsigned char*, std::size_t>> v;
    std::mt19937 rng(3);
    for (int i = 0; i < 3000; ++i) {
        const std::size_t n = 1 + rng() % 256;
        auto* p = static_cast<unsigned char*>(c.malloc(n));
        ASSERT_NE(p, nullptr);
        fill(p, n, static_cast<std::uint8_t>(i));
        v.emplace_back(p, n);
    }
    for (std::size_t i = 0; i < v.size(); ++i)
        ASSERT_TRUE(all_equal(v[i].first, v[i].second, static_cast<std::uint8_t>(i)));
    EXPECT_SLAB_OK(h, c);
}

TEST_F(Slab, FreedSlotIsReusedLowestFirst) {
    void* a = c.malloc(32);
    void* b = c.malloc(32);
    ASSERT_EQ(static_cast<char*>(b) - static_cast<char*>(a), 32);
    EXPECT_EQ(c.free(a), FR::Ok);
    EXPECT_EQ(c.malloc(32), a);
    EXPECT_SLAB_OK(h, c);
}

TEST_F(Slab, DoubleFreeIsDetectedExactly) {
    void* a = c.malloc(64);
    void* keep = c.malloc(64);  // keeps the slab in use
    EXPECT_EQ(c.free(a), FR::Ok);
    EXPECT_EQ(c.free(a), FR::DoubleFree);
    EXPECT_SLAB_OK(h, c);
    (void)keep;
}

TEST_F(Slab, InvalidAndForeignPointers) {
    void* a = c.malloc(48);
    int local = 0;
    EXPECT_EQ(c.free(&local), FR::NotMine);
    EXPECT_EQ(c.free(static_cast<char*>(a) + 16), FR::InvalidPointer);  // inside a 48 B slot
    auto* slab = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(a) &
                                         ~std::uintptr_t{Slabs::kSlabBytes - 1});
    EXPECT_EQ(c.free(slab + 8), FR::InvalidPointer);  // the slab header area
    void* plain = h.malloc(100);                      // an ordinary heap block
    EXPECT_EQ(c.free(plain), FR::NotMine);            // left for the heap to free
    EXPECT_FALSE(c.corrupted());
    EXPECT_SLAB_OK(h, c);
}

TEST_F(Slab, EmptiedSlabGoesBackToTheHeap) {
    const std::size_t per_slab = Slabs::capacity_of(Slabs::class_of(256));  // 62
    std::vector<void*> v;
    for (std::size_t i = 0; i < per_slab + 1; ++i) v.push_back(c.malloc(256));
    EXPECT_EQ(c.stats().slabs_in_use, 2u);
    const std::size_t heap_in_use = h.stats().bytes_in_use;
    for (std::size_t i = 0; i < per_slab; ++i) c.free(v[i]);  // the first slab empties
    EXPECT_EQ(c.stats().slabs_in_use, 1u);
    EXPECT_EQ(h.stats().bytes_in_use, heap_in_use - Slabs::kSlabBytes);  // returned to the heap
    EXPECT_SLAB_OK(h, c);
}

TEST_F(Slab, LastPartialSlabIsKeptToAvoidChurn) {
    void* p = c.malloc(16);
    c.free(p);
    EXPECT_EQ(c.stats().slabs_in_use, 1u);  // kept: the class's only partial slab
    EXPECT_EQ(c.malloc(16), p);
    EXPECT_SLAB_OK(h, c);
}

TEST_F(Slab, MemoryFreedBySmallObjectsServesLargeOnes) {
    // The point of carving slabs from the heap: after the small objects are
    // gone, (almost) the whole arena is one free heap block again.
    std::vector<void*> v;
    while (void* p = c.malloc(256)) v.push_back(p);  // until the heap is full of slabs
    EXPECT_GT(v.size(), 3000u);
    EXPECT_EQ(h.malloc(100000), nullptr);
    for (void* p : v) ASSERT_EQ(c.free(p), FR::Ok);
    EXPECT_EQ(c.stats().slabs_in_use, 1u);  // only the kept partial slab remains
    EXPECT_NE(h.malloc(h.arena().size() / 2), nullptr);
    EXPECT_SLAB_OK(h, c);
}

TEST_F(Slab, ForEachLiveListsExactlyTheLiveObjects) {
    std::set<void*> live;
    for (int i = 0; i < 200; ++i) live.insert(c.malloc(1 + i % 256));
    int k = 0;
    for (auto it = live.begin(); it != live.end();) {
        if (k++ % 3 == 0) {
            c.free(*it);
            it = live.erase(it);
        } else {
            ++it;
        }
    }
    std::set<void*> seen;
    EXPECT_TRUE(c.for_each_live([&](void* p, std::size_t size) {
        seen.insert(p);
        EXPECT_EQ(size, c.usable_size(p));
    }));
    EXPECT_EQ(seen, live);
}

TEST_F(Slab, DamagedHeaderStopsTheCacheSafely) {
    // Simulates an overflow from the memory before the slab into its header.
    void* a = c.malloc(32);
    void* b = c.malloc(32);
    auto* header = reinterpret_cast<std::uint64_t*>(reinterpret_cast<std::uintptr_t>(a) &
                                                    ~std::uintptr_t{Slabs::kSlabBytes - 1});
    header[0] = 0x4141414141414141ull;  // magic
    EXPECT_EQ(c.free(b), FR::Corrupted);
    EXPECT_TRUE(c.corrupted());
    EXPECT_EQ(c.malloc(32), nullptr);
    EXPECT_EQ(c.free(a), FR::Corrupted);
}

TEST(SlabAllocator, RoutesBySizeAndFreesEitherKind) {
    myalloc::Allocator a;
    ASSERT_TRUE(a.init(1u << 20));
    void* small = a.malloc(100);
    void* big = a.malloc(1000);
    EXPECT_TRUE(a.slab().owns(small));
    EXPECT_FALSE(a.slab().owns(big));
    EXPECT_EQ(a.free(small), myalloc::FreeStatus::Ok);
    EXPECT_EQ(a.free(small), myalloc::FreeStatus::DoubleFree);
    EXPECT_EQ(a.free(big), myalloc::FreeStatus::Ok);
    const char* why = nullptr;
    EXPECT_TRUE(a.check(&why)) << why;
}

TEST(SlabAllocator, ReallocMovesBetweenSlabAndHeap) {
    myalloc::Allocator a;
    ASSERT_TRUE(a.init(1u << 20));
    auto* p = static_cast<unsigned char*>(a.malloc(40));  // 48 B slot
    fill(p, 40, 0x31);
    EXPECT_EQ(a.realloc(p, 48), p);  // still fits its slot
    auto* q = static_cast<unsigned char*>(a.realloc(p, 3000));
    ASSERT_NE(q, nullptr);
    EXPECT_FALSE(a.slab().owns(q));
    EXPECT_TRUE(all_equal(q, 40, 0x31));
    myalloc::FreeStatus st = myalloc::FreeStatus::Ok;
    EXPECT_EQ(a.realloc(p, 10, &st), nullptr);  // p was freed by the move
    EXPECT_EQ(st, myalloc::FreeStatus::DoubleFree);
    EXPECT_EQ(a.free(q), myalloc::FreeStatus::Ok);
    const char* why = nullptr;
    EXPECT_TRUE(a.check(&why)) << why;
}

TEST(SlabAllocator, StatsCountObjectsNotSlabs) {
    myalloc::Allocator a;
    ASSERT_TRUE(a.init(1u << 20));
    void* p = a.malloc(100);  // 112 B slot inside a 16 KiB slab
    void* q = a.malloc(1000);
    const auto s = a.stats();
    EXPECT_EQ(s.live_blocks, 2u);
    EXPECT_EQ(s.bytes_in_use, 112u + myalloc::block_size_for(1000));
    a.free(p);
    a.free(q);
    EXPECT_EQ(a.stats().live_blocks, 0u);
    EXPECT_EQ(a.stats().bytes_in_use, 0u);
}

// A use-after-free write into a freed slot cannot touch slab bookkeeping (it
// is out of band): the cache and heap must stay fully consistent, not merely
// "detect or survive" as the general heap does (ADR-012).
TEST(SlabFuzz, UseAfterFreeWritesNeverDisturbBookkeeping) {
    for (unsigned seed = 1; seed <= 50; ++seed) {
        myalloc::Heap h;
        ASSERT_TRUE(h.init(1u << 20));
        Slabs c;
        ASSERT_TRUE(c.init(h));
        std::mt19937 rng(seed);
        struct Obj {
            unsigned char* p;
            std::size_t n;
            std::uint8_t tag;
        };
        std::vector<Obj> live, dead;
        for (int op = 0; op < 4000; ++op) {
            const unsigned r = rng() % 100;
            if (r < 10 && !dead.empty()) {
                const Obj& d = dead[rng() % dead.size()];
                // Only slots still inside a live slab; a returned slab's memory
                // belongs to the heap again, whose links ADR-012 covers.
                if (c.owns(d.p))
                    for (std::size_t k = 0; k + 8 <= d.n; k += 8) {
                        const std::uint64_t junk = (std::uint64_t(rng()) << 32) | rng();
                        std::memcpy(d.p + k, &junk, 8);
                    }
            } else if (r < 55 || live.empty()) {
                const std::size_t n = rng() % 257;
                if (auto* p = static_cast<unsigned char*>(c.malloc(n))) {
                    const auto tag = static_cast<std::uint8_t>(rng());
                    fill(p, n, tag);
                    live.push_back({p, n, tag});
                    dead.erase(std::remove_if(dead.begin(), dead.end(),
                                              [&](const Obj& d) { return d.p == p; }),
                               dead.end());
                }
            } else {
                const std::size_t k = rng() % live.size();
                ASSERT_EQ(c.free(live[k].p), FR::Ok);
                dead.push_back(live[k]);
                live[k] = live.back();
                live.pop_back();
            }
            const char* why = nullptr;
            ASSERT_TRUE(c.check(&why)) << "seed " << seed << " op " << op << ": " << why;
            ASSERT_TRUE(h.check(&why)) << "seed " << seed << " op " << op << ": " << why;
        }
        for (const Obj& o : live) ASSERT_TRUE(all_equal(o.p, o.n, o.tag)) << "seed " << seed;
    }
}
