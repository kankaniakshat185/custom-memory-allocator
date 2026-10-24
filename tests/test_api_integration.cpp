// Integration tests: the public C API (my_malloc / my_free / my_realloc /
// my_calloc) on the process-wide 256 MiB heap, used the way a program would
// use it, including behind standard containers.
#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

#include "heap.h"
#include "heap_test_util.h"
#include "myalloc/myalloc.h"

namespace {
struct Node {  // a linked structure built entirely from my_malloc
    int value;
    Node* next;
    char pad[20];
};

// Minimal C++17 allocator adapter so standard containers draw from my_malloc.
// Test-only; a public operator new / allocator wrapper is a Tier 2 item.
template <class T>
struct MyAlloc {
    using value_type = T;
    MyAlloc() = default;
    template <class U>
    MyAlloc(const MyAlloc<U>&) {}
    T* allocate(std::size_t n) {
        if (n > SIZE_MAX / sizeof(T)) throw std::bad_alloc();
        if (void* p = my_malloc(n * sizeof(T))) return static_cast<T*>(p);
        throw std::bad_alloc();
    }
    void deallocate(T* p, std::size_t) { my_free(p); }
    template <class U>
    bool operator==(const MyAlloc<U>&) const { return true; }
    template <class U>
    bool operator!=(const MyAlloc<U>&) const { return false; }
};
}  // namespace

TEST(Api, MallocFreeRoundTrip) {
    void* p = my_malloc(128);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % 16, 0u);
    std::memset(p, 1, 128);
    my_free(p);
    my_free(nullptr);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(Api, LinkedListBuiltAndTornDown) {
    Node* head = nullptr;
    for (int i = 0; i < 10000; ++i) {
        auto* n = static_cast<Node*>(my_malloc(sizeof(Node)));
        ASSERT_NE(n, nullptr);
        n->value = i;
        n->next = head;
        head = n;
    }
    int expect = 9999;
    for (Node* n = head; n; n = n->next) ASSERT_EQ(n->value, expect--);
    while (head) {
        Node* next = head->next;
        my_free(head);
        head = next;
    }
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(Api, StringsOfVaryingLengthKeepTheirContents) {
    std::mt19937 rng(42);
    std::vector<std::pair<char*, std::string>> items;
    for (int i = 0; i < 2000; ++i) {
        std::string s(rng() % 300, static_cast<char>('a' + rng() % 26));
        auto* p = static_cast<char*>(my_malloc(s.size() + 1));
        ASSERT_NE(p, nullptr);
        std::memcpy(p, s.c_str(), s.size() + 1);
        items.emplace_back(p, std::move(s));
        if (rng() % 3 == 0) {  // free a random earlier one
            std::size_t k = rng() % items.size();
            ASSERT_STREQ(items[k].first, items[k].second.c_str());
            my_free(items[k].first);
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(k));
        }
    }
    for (auto& [p, s] : items) {
        ASSERT_STREQ(p, s.c_str());
        my_free(p);
    }
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(Api, DoubleFreeAbortsWithDiagnostic) {
    void* p = my_malloc(64);
    my_free(p);
    EXPECT_DEATH(my_free(p), "double free");
}

TEST(Api, InvalidFreeAbortsWithDiagnostic) {
    int on_stack = 0;
    EXPECT_DEATH(my_free(&on_stack), "invalid pointer");
}

TEST(Api, LargeAllocationSucceedsAndIsUsable) {
    const std::size_t n = 32u << 20;
    auto* p = static_cast<unsigned char*>(my_malloc(n));
    ASSERT_NE(p, nullptr);
    p[0] = 1;
    p[n - 1] = 2;
    my_free(p);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(Api, ReallocGrowsStringBuilderPreservingContents) {
    std::size_t cap = 8, len = 0;
    auto* buf = static_cast<char*>(my_malloc(cap));
    ASSERT_NE(buf, nullptr);
    std::string expected;
    for (int i = 0; i < 20000; ++i) {
        const std::string piece = std::to_string(i) + ",";
        if (len + piece.size() + 1 > cap) {
            cap *= 2;
            buf = static_cast<char*>(my_realloc(buf, cap));
            ASSERT_NE(buf, nullptr);
        }
        std::memcpy(buf + len, piece.data(), piece.size());
        len += piece.size();
        expected += piece;
    }
    buf[len] = '\0';
    EXPECT_EQ(expected, buf);
    buf = static_cast<char*>(my_realloc(buf, len + 1));  // shrink to fit
    EXPECT_EQ(expected, buf);
    my_free(buf);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(Api, ReallocNullAndZero) {
    void* p = my_realloc(nullptr, 64);
    ASSERT_NE(p, nullptr);
    void* q = my_realloc(p, 0);
    ASSERT_NE(q, nullptr);  // documented: shrinks, never frees
    my_free(q);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(Api, CallocMatrixIsZeroed) {
    const std::size_t rows = 300, cols = 400;
    auto* m = static_cast<double*>(my_calloc(rows * cols, sizeof(double)));
    ASSERT_NE(m, nullptr);
    for (std::size_t i = 0; i < rows * cols; ++i) ASSERT_EQ(m[i], 0.0);
    my_free(m);
    EXPECT_EQ(my_calloc(SIZE_MAX / 4, 8), nullptr);
}

TEST(Api, StdVectorWithMyAllocator) {
    std::vector<int, MyAlloc<int>> v;
    for (int i = 0; i < 100000; ++i) v.push_back(i);
    long long sum = 0;
    for (int x : v) sum += x;
    EXPECT_EQ(sum, 100000LL * 99999 / 2);
    v.clear();
    v.shrink_to_fit();
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(Api, StdMapAndStringsWithMyAllocator) {
    using Str = std::basic_string<char, std::char_traits<char>, MyAlloc<char>>;
    {
        std::map<int, Str, std::less<int>, MyAlloc<std::pair<const int, Str>>> m;
        std::mt19937 rng(9);
        for (int i = 0; i < 5000; ++i) m[static_cast<int>(rng() % 2000)] = Str(rng() % 200, 'x');
        for (int i = 0; i < 2000; i += 3) m.erase(i);
        for (const auto& [k, s] : m) ASSERT_EQ(k % 3 == 0, false);
        EXPECT_HEAP_OK(myalloc::default_heap());
    }  // map and all strings destroyed through my_free
    EXPECT_EQ(myalloc::default_heap().stats().live_blocks, 0u);
    EXPECT_HEAP_OK(myalloc::default_heap());
}

TEST(Api, ReallocOfFreedPointerAborts) {
    void* p = my_malloc(64);
    my_free(p);
    EXPECT_DEATH(my_realloc(p, 128), "my_realloc: double free");
}
