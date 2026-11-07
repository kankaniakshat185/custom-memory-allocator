#pragma once
#include <time.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace bench {

// Monotonic, not affected by sleep or NTP. On Apple Silicon the underlying
// counter ticks at 24 MHz (~41.7 ns), so only batches of operations are
// timed, never single calls (ADR-008). The Linux branch exists so the
// harness builds in CI; all published numbers are from macOS.
inline std::uint64_t now_ns() {
#ifdef __APPLE__
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000u + static_cast<std::uint64_t>(ts.tv_nsec);
#endif
}

inline double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double idx = p * static_cast<double>(v.size() - 1);
    const auto lo = static_cast<std::size_t>(idx);
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (idx - static_cast<double>(lo));
}

}  // namespace bench
