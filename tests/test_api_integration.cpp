// Integration tests: the public C API (my_malloc / my_free) on the process-wide
// 256 MiB heap, used the way a program would use it.
#include <gtest/gtest.h>

#include <cstring>
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
