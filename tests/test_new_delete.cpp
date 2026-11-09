// Global operator new/delete replacement (ADR-015). Built into its own test
// binary linked with myalloc_new, so every allocation in this process,
// GoogleTest's own included, goes through the heap.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "heap_test_util.h"
#include "myalloc/myalloc.h"

namespace {

struct alignas(64) CacheLine {
    unsigned char bytes[64];
};
struct alignas(4096) Page {
    unsigned char bytes[4096];
};

bool in_heap(const void* p) { return myalloc::default_heap().arena().contains(p); }

std::size_t live() { return myalloc::default_heap().stats().live_blocks; }

bool aligned(const void* p, std::size_t a) { return reinterpret_cast<std::uintptr_t>(p) % a == 0; }

constexpr std::size_t kTooBig = std::size_t{300} << 20;  // > the 256 MiB arena

}  // namespace

TEST(NewDelete, PlainNewAndDeleteUseTheHeap) {
    const std::size_t before = live();
    int* i = new int(42);
    auto* a = new char[1000];
    EXPECT_TRUE(in_heap(i));
    EXPECT_TRUE(in_heap(a));
    EXPECT_TRUE(aligned(i, MY_MALLOC_ALIGNMENT));
    EXPECT_EQ(live(), before + 2);
    delete i;
    delete[] a;
    EXPECT_EQ(live(), before);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(NewDelete, ZeroSizeNewReturnsDistinctPointers) {
    void* a = ::operator new(0);
    void* b = ::operator new(0);
    EXPECT_NE(a, b);
    EXPECT_TRUE(in_heap(a));
    ::operator delete(a);
    ::operator delete(b);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(NewDelete, DeleteOfNullIsANoOp) {
    const std::size_t before = live();
    int* p = nullptr;
    delete p;
    delete[] p;
    ::operator delete(nullptr, std::align_val_t{64});
    EXPECT_EQ(live(), before);
}

TEST(NewDelete, SizedAndNothrowFormsAllRoundTrip) {
    const std::size_t before = live();
    void* a = ::operator new(48);
    void* b = ::operator new[](48);
    void* c = ::operator new(48, std::nothrow);
    void* d = ::operator new[](48, std::nothrow);
    for (void* p : {a, b, c, d}) EXPECT_TRUE(in_heap(p));
    ::operator delete(a, 48);
    ::operator delete[](b, 48);
    ::operator delete(c, std::nothrow);
    ::operator delete[](d, std::nothrow);
    EXPECT_EQ(live(), before);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(NewDelete, OverAlignedNewIsAligned) {
    const std::size_t before = live();
    auto* line = new CacheLine;
    auto* lines = new CacheLine[33];
    auto* page = new Page;
    EXPECT_TRUE(aligned(line, 64));
    EXPECT_TRUE(aligned(lines, 64));
    EXPECT_TRUE(aligned(page, 4096));
    EXPECT_TRUE(in_heap(page));
    std::memset(page->bytes, 1, sizeof(page->bytes));
    delete line;
    delete[] lines;
    delete page;

    for (std::size_t a = 32; a <= (1u << 16); a *= 2) {  // every explicit form
        const std::align_val_t al{a};
        void* p = ::operator new(100, al);
        void* q = ::operator new[](100, al);
        void* r = ::operator new(100, al, std::nothrow);
        void* s = ::operator new[](100, al, std::nothrow);
        for (void* x : {p, q, r, s}) EXPECT_TRUE(aligned(x, a)) << a;
        ::operator delete(p, al);
        ::operator delete[](q, 100, al);
        ::operator delete(r, al, std::nothrow);
        ::operator delete[](s, al, std::nothrow);
    }
    EXPECT_EQ(live(), before);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(NewDelete, ExhaustionThrowsBadAlloc) {
    EXPECT_THROW((void)::operator new(kTooBig), std::bad_alloc);
    EXPECT_THROW((void)::operator new[](kTooBig), std::bad_alloc);
    EXPECT_THROW((void)::operator new(kTooBig, std::align_val_t{64}), std::bad_alloc);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(NewDelete, NothrowExhaustionReturnsNull) {
    EXPECT_EQ(::operator new(kTooBig, std::nothrow), nullptr);
    EXPECT_EQ(::operator new[](kTooBig, std::nothrow), nullptr);
    EXPECT_EQ(::operator new(kTooBig, std::align_val_t{64}, std::nothrow), nullptr);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

namespace {
void* g_reserve = nullptr;
int g_handler_calls = 0;
void release_reserve() {  // a new_handler: free memory so the retry can succeed
    ++g_handler_calls;
    ::operator delete(g_reserve);
    g_reserve = nullptr;
    std::set_new_handler(nullptr);
}
}  // namespace

TEST(NewDelete, NewHandlerIsCalledAndTheAllocationRetried) {
    g_reserve = ::operator new(std::size_t{200} << 20);  // most of the arena
    g_handler_calls = 0;
    std::set_new_handler(release_reserve);
    void* p = ::operator new(std::size_t{100} << 20);  // fails until the reserve is freed
    EXPECT_NE(p, nullptr);
    EXPECT_EQ(g_handler_calls, 1);
    EXPECT_EQ(g_reserve, nullptr);
    ::operator delete(p);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(NewDelete, NothrowFormRunsTheNewHandlerToo) {
    g_reserve = ::operator new(std::size_t{200} << 20);
    g_handler_calls = 0;
    std::set_new_handler(release_reserve);
    void* p = ::operator new(std::size_t{100} << 20, std::nothrow);
    EXPECT_NE(p, nullptr);
    EXPECT_EQ(g_handler_calls, 1);
    ::operator delete(p);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(NewDelete, StandardContainersRunOnTheHeap) {
    const std::size_t before = live();
    {
        std::vector<std::string> words;
        std::map<int, std::vector<int>> index;
        std::unordered_map<std::string, int> counts;
        for (int i = 0; i < 20000; ++i) {
            words.push_back("word-number-" + std::to_string(i));  // heap strings (> SSO)
            index[i % 500].push_back(i);
            ++counts[words.back().substr(0, 14)];
        }
        auto shared = std::make_shared<std::vector<int>>(1000, 3);
        std::function<int()> f = [big = std::string(200, 'z')] {
            return static_cast<int>(big.size());
        };
        EXPECT_TRUE(in_heap(words.data()));
        EXPECT_TRUE(in_heap(words.back().data()));
        EXPECT_TRUE(in_heap(shared.get()));
        EXPECT_EQ(f(), 200);
        EXPECT_EQ(index[7].size(), 40u);
        EXPECT_HEAP_OK(myalloc::default_heap());
    }
    EXPECT_EQ(live(), before);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(NewDelete, ExceptionsAndUniquePtrWork) {
    const std::size_t before = live();
    try {
        auto p = std::make_unique<std::string>(500, 'e');
        throw std::runtime_error(*p);  // the message string is heap-allocated
    } catch (const std::runtime_error& e) {
        EXPECT_EQ(std::strlen(e.what()), 500u);
    }
    EXPECT_EQ(live(), before);
}

TEST(NewDeleteDeath, DoubleDeleteAborts) {
    EXPECT_DEATH(
        {
            int* p = new int(1);
            delete p;
            delete p;
        },
        "double free");
}

TEST(NewDeleteDeath, DeleteOfForeignPointerAborts) {
    EXPECT_DEATH(
        {
            static int not_from_new = 0;
            // volatile hides the origin, or GCC warns at compile time
            // (-Wfree-nonheap-object) about the very misuse this tests.
            int* volatile p = &not_from_new;
            ::operator delete(p);
        },
        "invalid pointer");
}
