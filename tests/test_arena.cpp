#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "arena.h"

using myalloc::Arena;

TEST(Arena, ReservesPageAlignedRegion) {
    Arena a;
    ASSERT_TRUE(a.init(256u << 20));
    EXPECT_EQ(a.size() % myalloc::page_size(), 0u);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(a.base()) % myalloc::page_size(), 0u);
}

TEST(Arena, RoundsUpToPageSize) {
    Arena a;
    ASSERT_TRUE(a.init(1));
    EXPECT_EQ(a.size(), myalloc::page_size());
}

TEST(Arena, ContainsIsExact) {
    Arena a;
    ASSERT_TRUE(a.init(1u << 20));
    auto* b = static_cast<unsigned char*>(a.base());
    EXPECT_TRUE(a.contains(b));
    EXPECT_TRUE(a.contains(b + a.size() - 1));
    EXPECT_FALSE(a.contains(b + a.size()));
    EXPECT_FALSE(a.contains(b - 1));
    EXPECT_FALSE(a.contains(nullptr));
}

TEST(Arena, MemoryIsWritableAndZeroed) {
    Arena a;
    ASSERT_TRUE(a.init(1u << 20));
    auto* b = static_cast<unsigned char*>(a.base());
    EXPECT_EQ(b[0], 0);
    EXPECT_EQ(b[a.size() - 1], 0);
    std::memset(b, 0xAB, a.size());
    EXPECT_EQ(b[a.size() - 1], 0xAB);
}

TEST(Arena, RejectsZeroAndDoubleInit) {
    Arena a;
    EXPECT_FALSE(a.init(0));
    ASSERT_TRUE(a.init(4096));
    EXPECT_FALSE(a.init(4096));
}
