// Standard-container workloads timed under whatever global operator new the
// binary is linked with (ADR-015): built three times, as stl_bench_system (the
// platform allocator), stl_bench_myalloc (linked with myalloc_new) and
// stl_bench_slab (new/delete over heap + slab, bench/new_delete_slab.cpp).
//
// Each workload is seeded, so all three binaries do identical work. All reps run
// in one process on one heap, as a real program would; the median rep is
// reported with its IQR.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef STL_BENCH_MYALLOC
#include "myalloc/myalloc.h"
#endif

namespace {

using Clock = std::chrono::steady_clock;

// Random-key std::map: 100k inserts, then 400k erase+insert pairs.
std::size_t map_churn(std::mt19937& rng) {
    std::map<std::uint32_t, std::uint64_t> m;
    std::vector<std::uint32_t> keys;
    keys.reserve(100000);
    for (int i = 0; i < 100000; ++i) {
        keys.push_back(static_cast<std::uint32_t>(rng()));
        m.emplace(keys.back(), i);
    }
    for (int i = 0; i < 400000; ++i) {
        std::uint32_t& k = keys[rng() % keys.size()];
        m.erase(k);
        k = static_cast<std::uint32_t>(rng());
        m.emplace(k, i);
    }
    return 100000 + 2 * 400000;
}

// Strings of 0-256 chars (many beyond the small-string buffer) appended to a
// vector, half of them replaced at random, repeatedly.
std::size_t string_vector(std::mt19937& rng) {
    std::vector<std::string> v;
    std::size_t ops = 0;
    for (int round = 0; round < 4; ++round) {
        for (int i = 0; i < 50000; ++i, ++ops) v.emplace_back(rng() % 257, 'a');
        for (int i = 0; i < 50000; ++i, ++ops) v[rng() % v.size()] = std::string(rng() % 257, 'b');
    }
    return ops;
}

// Word counting into an unordered_map<string, int>, then erasing half.
std::size_t hash_count(std::mt19937& rng) {
    std::unordered_map<std::string, int> counts;
    std::size_t ops = 0;
    for (int i = 0; i < 300000; ++i, ++ops)
        ++counts["key-with-a-long-prefix-" + std::to_string(rng() % 60000)];
    for (auto it = counts.begin(); it != counts.end(); ++ops)
        it = (it->second % 2) ? counts.erase(it) : std::next(it);
    return ops;
}

// std::list as a FIFO of small nodes: push back, pop front.
std::size_t list_queue(std::mt19937& rng) {
    std::list<std::uint64_t> q;
    std::size_t ops = 0;
    for (int i = 0; i < 1000000; ++i, ++ops) {
        q.push_back(rng());
        if (q.size() > 5000 && (rng() & 1)) q.pop_front();
    }
    return ops;
}

struct Workload {
    const char* name;
    std::size_t (*run)(std::mt19937&);
};

constexpr Workload kWorkloads[] = {{"map_churn", map_churn},
                                   {"string_vector", string_vector},
                                   {"hash_count", hash_count},
                                   {"list_queue", list_queue}};

}  // namespace

int main(int argc, char** argv) {
    int reps = 15;
    const char* only = nullptr;  // --workload NAME runs just that one
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--reps") == 0) reps = std::atoi(argv[i + 1]);
        if (std::strcmp(argv[i], "--workload") == 0) only = argv[i + 1];
    }
#if defined(STL_BENCH_MYALLOC)
    const char* label = "myalloc";
#elif defined(STL_BENCH_SLAB)
    const char* label = "slab";
#else
    const char* label = "system";
#endif
    std::printf("allocator,workload,reps,median_ns_per_op,iqr_pct\n");
    for (const Workload& w : kWorkloads) {
        if (only && std::strcmp(only, w.name) != 0) continue;
        std::vector<double> ns;
        for (int r = -1; r < reps; ++r) {  // rep -1 is a warm-up
            std::mt19937 rng(12345);
            const auto t0 = Clock::now();
            const std::size_t ops = w.run(rng);
            const auto t1 = Clock::now();
            if (r >= 0)
                ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() /
                             static_cast<double>(ops));
        }
        if (ns.empty()) continue;  // --reps 0: warm-up only (instruction baselines)
        std::sort(ns.begin(), ns.end());
        const double med = ns[ns.size() / 2];
        const double iqr = ns[ns.size() * 3 / 4] - ns[ns.size() / 4];
        std::printf("%s,%s,%d,%.2f,%.1f\n", label, w.name, reps, med, 100.0 * iqr / med);
    }
#ifdef STL_BENCH_MYALLOC
    my_alloc_stats_t s;
    my_alloc_stats(&s);
    std::fprintf(stderr, "myalloc: peak in use %zu B, high-water %zu B, live at end %zu\n",
                 s.peak_bytes_in_use, s.high_water, s.live_blocks);
#endif
    return 0;
}
