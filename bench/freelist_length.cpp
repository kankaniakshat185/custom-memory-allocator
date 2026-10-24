// Replays a benchmark trace against a Heap and reports the free-list length
// seen by every malloc: mean, p50, p99, max, and the mean per tenth of the
// trace (ADR-010). Uses only Heap API present in every phase
// (init/malloc/free/stats().free_blocks), so run_phases-style builds of old
// commits work.
//   freelist_length <workload> [--best-fit]
// Trees with fit policies (ADR-009) also report list nodes actually visited.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "heap.h"
#include "workloads.h"

template <class H>
int run(const char* want) {
    for (const bench::Trace& t : bench::all_traces()) {
        if (t.name != want) continue;
        H h;
        if (!h.init(std::size_t{256} << 20)) return 1;
        std::vector<void*> slot(t.slots, nullptr);
        std::vector<std::size_t> len;
        for (const bench::Op& op : t.ops) {
            if (op.kind == bench::OpKind::Alloc) {
                len.push_back(h.stats().free_blocks);
                slot[op.slot] = h.malloc(op.size);
            } else if (op.kind == bench::OpKind::Free) {
                h.free(slot[op.slot]);
                slot[op.slot] = nullptr;
            }
        }
        if (len.empty()) return 1;
        double mean = 0;
        for (auto x : len) mean += static_cast<double>(x);
        mean /= static_cast<double>(len.size());
        std::vector<std::size_t> sorted = len;
        std::sort(sorted.begin(), sorted.end());
        std::printf("mallocs=%zu mean=%.1f p50=%zu p99=%zu max=%zu\n", len.size(), mean,
                    sorted[sorted.size() / 2], sorted[sorted.size() * 99 / 100], sorted.back());
        std::printf("mean by tenth of trace:");
        for (int k = 0; k < 10; ++k) {
            const std::size_t a = len.size() * k / 10, b = len.size() * (k + 1) / 10;
            double m = 0;
            for (std::size_t i = a; i < b; ++i) m += static_cast<double>(len[i]);
            std::printf(" %.0f", m / static_cast<double>(b - a));
        }
#ifdef MYALLOC_HAS_FIT_POLICIES
        std::printf(" | nodes visited per malloc: %.1f",
                    static_cast<double>(h.stats().search_steps) / static_cast<double>(h.stats().mallocs));
#endif
        std::printf("\n");
        return 0;
    }
    std::fprintf(stderr, "unknown workload %s\n", want);
    return 2;
}

int main(int argc, char** argv) {
    const char* want = argc > 1 ? argv[1] : "adversarial_holes";
#ifdef MYALLOC_HAS_FIT_POLICIES
    if (argc > 2 && std::strcmp(argv[2], "--best-fit") == 0) return run<myalloc::BestFitHeap>(want);
#endif
    return run<myalloc::Heap>(want);
}
