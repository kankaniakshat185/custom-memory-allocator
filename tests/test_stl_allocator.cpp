// myalloc::allocator<T> and my_aligned_alloc (ADR-015): standard containers
// drawing from the process-wide heap through the public C API.
#include <gtest/gtest.h>

#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "heap_test_util.h"
#include "myalloc/allocator.hpp"
#include "myalloc/myalloc.h"

namespace {

template <class T>
using Vec = std::vector<T, myalloc::allocator<T>>;

struct alignas(64) CacheLine {
    unsigned char bytes[64];
};
struct alignas(4096) Page {
    unsigned char bytes[4096];
};

bool in_default_heap(const void* p) { return myalloc::default_heap().arena().contains(p); }

std::size_t live() { return myalloc::default_heap().stats().live_blocks; }

}  // namespace

TEST(StlAllocator, MeetsTheAllocatorRequirements) {
    using A = myalloc::allocator<int>;
    using Traits = std::allocator_traits<A>;
    static_assert(std::is_same_v<Traits::value_type, int>);
    static_assert(std::is_same_v<Traits::rebind_alloc<double>, myalloc::allocator<double>>);
    static_assert(Traits::is_always_equal::value);
    static_assert(Traits::propagate_on_container_move_assignment::value);
    static_assert(std::is_nothrow_default_constructible_v<A>);
    const A a;
    const myalloc::allocator<double> b(a);  // converting copy (rebind)
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_EQ(Traits::max_size(a), SIZE_MAX / sizeof(int));
}

TEST(StlAllocator, AllocateReturnsAlignedHeapMemory) {
    myalloc::allocator<std::uint64_t> a;
    const std::size_t before = live();
    std::uint64_t* p = a.allocate(100);
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(in_default_heap(p));
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % MY_MALLOC_ALIGNMENT, 0u);
    for (int i = 0; i < 100; ++i) p[i] = static_cast<std::uint64_t>(i);
    EXPECT_EQ(live(), before + 1);
    a.deallocate(p, 100);
    EXPECT_EQ(live(), before);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(StlAllocator, OverAlignedTypesAreAligned) {
    const std::size_t before = live();
    {
        Vec<CacheLine> lines(1000);
        Vec<Page> pages(8);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(lines.data()) % 64, 0u);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pages.data()) % 4096, 0u);
        EXPECT_TRUE(in_default_heap(lines.data()));
        EXPECT_TRUE(in_default_heap(pages.data()));
        for (std::size_t i = 0; i < 2000; ++i) lines.push_back(CacheLine{});  // regrows
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(lines.data()) % 64, 0u);
        EXPECT_HEAP_OK(myalloc::default_heap());
    }
    EXPECT_EQ(live(), before);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(StlAllocator, SizeOverflowThrowsBadArrayNewLength) {
    myalloc::allocator<std::uint64_t> a;
    EXPECT_THROW((void)a.allocate(SIZE_MAX / 4), std::bad_array_new_length);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(StlAllocator, ExhaustedArenaThrowsBadAllocAndLeavesHeapUsable) {
    myalloc::allocator<char> a;
    EXPECT_THROW((void)a.allocate(std::size_t{300} << 20), std::bad_alloc);  // > 256 MiB arena
    Vec<int> v(1000, 7);  // still works afterwards
    EXPECT_EQ(v[999], 7);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(StlAllocator, NodeAndHashContainersReturnEveryBlock) {
    using Str = std::basic_string<char, std::char_traits<char>, myalloc::allocator<char>>;
    const std::size_t before = live();
    {
        std::list<int, myalloc::allocator<int>> l;
        std::deque<Str, myalloc::allocator<Str>> d;
        std::set<int, std::less<int>, myalloc::allocator<int>> s;
        std::unordered_map<int, Str, std::hash<int>, std::equal_to<int>,
                           myalloc::allocator<std::pair<const int, Str>>>
            m;
        for (int i = 0; i < 20000; ++i) {
            l.push_back(i);
            s.insert(i * 7 % 10007);
            d.emplace_back(static_cast<std::size_t>(i % 97), 'a');  // short and heap strings
            m.emplace(i, Str(static_cast<std::size_t>(i % 61), 'b'));
            if (i % 3 == 0) {
                l.pop_front();
                m.erase(i / 2);
            }
        }
        EXPECT_TRUE(in_default_heap(&l.back()));
        EXPECT_TRUE(in_default_heap(&*s.begin()));
        EXPECT_TRUE(in_default_heap(&m.begin()->second));
        EXPECT_GT(live(), before + 20000);  // nodes really come from the heap
        EXPECT_HEAP_OK(myalloc::default_heap());

        // Moves between containers share storage: the allocators are equal.
        auto moved = std::move(m);
        EXPECT_TRUE(m.empty());
        EXPECT_FALSE(moved.empty());
    }
    EXPECT_EQ(live(), before);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(StlAllocator, AllocateSharedUsesTheHeap) {
    const std::size_t before = live();
    {
        auto p = std::allocate_shared<std::string>(myalloc::allocator<std::string>(), "hello");
        EXPECT_TRUE(in_default_heap(p.get()));
        EXPECT_EQ(*p, "hello");
        EXPECT_EQ(live(), before + 1);  // control block and object in one allocation
    }
    EXPECT_EQ(live(), before);
}

TEST(AlignedAllocApi, AlignsFreesAndRejectsBadAlignments) {
    const std::size_t before = live();
    for (std::size_t a = 1; a <= (1u << 16); a *= 2) {
        void* p = my_aligned_alloc(a, 100);
        ASSERT_NE(p, nullptr) << a;
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % (a < 16 ? 16 : a), 0u) << a;
        std::memset(p, 0xAB, 100);
        my_free(p);
    }
    EXPECT_EQ(my_aligned_alloc(48, 100), nullptr);                     // not a power of two
    EXPECT_EQ(my_aligned_alloc(64, std::size_t{300} << 20), nullptr);  // larger than the arena
    EXPECT_EQ(live(), before);
    EXPECT_HEAP_OK(myalloc::default_heap());
}
