#pragma once
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "heap.h"

// Fails the test with the checker's message if any heap invariant is broken.
#define EXPECT_HEAP_OK(heap)                                  \
    do {                                                      \
        const char* why_ = nullptr;                           \
        EXPECT_TRUE((heap).check(&why_)) << "heap_check: " << (why_ ? why_ : "?"); \
    } while (0)

inline void fill(void* p, std::size_t n, std::uint8_t v) { std::memset(p, v, n); }

inline bool all_equal(const void* p, std::size_t n, std::uint8_t v) {
    const auto* b = static_cast<const std::uint8_t*>(p);
    for (std::size_t i = 0; i < n; ++i)
        if (b[i] != v) return false;
    return true;
}

#ifdef MYALLOC_BEST_FIT
constexpr bool kBestFit = true;
#else
constexpr bool kBestFit = false;
#endif

