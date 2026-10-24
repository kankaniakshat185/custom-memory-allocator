#include <vector>

#include "heap_test_util.h"

TEST(Alignment, EveryPayloadIs16ByteAligned) {
    myalloc::Heap h;
    ASSERT_TRUE(h.init(8u << 20));
    std::vector<void*> ptrs;
    for (std::size_t n = 0; n <= 600; ++n) {
        void* p = h.malloc(n);
        ASSERT_NE(p, nullptr) << n;
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % 16, 0u) << "size " << n;
        ptrs.push_back(p);
    }
    EXPECT_HEAP_OK(h);
    for (void* p : ptrs) EXPECT_EQ(h.free(p), myalloc::FreeStatus::Ok);
    EXPECT_HEAP_OK(h);
}

TEST(Alignment, SuitableForMaxAlignTypes) {
    myalloc::Heap h;
    ASSERT_TRUE(h.init(1u << 20));
    void* p = h.malloc(sizeof(max_align_t));
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % alignof(max_align_t), 0u);
    struct alignas(16) Vec { double a, b; };
    auto* v = new (h.malloc(sizeof(Vec))) Vec{1.0, 2.0};
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(v) % alignof(Vec), 0u);
}
