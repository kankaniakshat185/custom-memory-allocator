// Deterministic memory-footprint probe (not a timing benchmark; that is the
// Phase 5 harness). Replays seeded alloc/free workloads against a fresh Heap
// and reports how efficiently the arena was used. Used for ADR before/after
// numbers on fragmentation and utilisation, which don't depend on timer noise.
//
// Output (CSV): workload,ops,oom,peak_requested,peak_in_use,high_water,
//               internal_overhead,utilisation,free_blocks_end,free_bytes_end,largest_free_end
//   internal_overhead = peak_in_use / peak_requested   (headers, rounding, unsplit slack)
//   utilisation       = peak_requested / high_water    (1.0 = no arena wasted)
//   *_end             = free list at the end of the run, live blocks still held
//                       (how shredded the free memory is)
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "heap.h"

namespace {

struct Live {
    void* p;
    std::size_t n;
};

struct Result {
    std::size_t ops = 0, oom = 0, peak_req = 0;
};

class Driver {
public:
    explicit Driver(std::size_t arena) { ok_ = heap_.init(arena); }
    bool ok() const { return ok_; }

    void alloc(std::size_t n) {
        ++r_.ops;
        void* p = heap_.malloc(n);
        if (!p) { ++r_.oom; return; }
        live_.push_back({p, n});
        req_ += n;
        if (req_ > r_.peak_req) r_.peak_req = req_;
    }
    void free_at(std::size_t k) {
        ++r_.ops;
        req_ -= live_[k].n;
        heap_.free(live_[k].p);
        live_[k] = live_.back();
        live_.pop_back();
    }
    std::size_t live() const { return live_.size(); }

    void report(const char* name) const {
        const auto& s = heap_.stats();
        const auto f = heap_.free_summary();
        std::printf("%s,%zu,%zu,%zu,%zu,%zu,%.3f,%.3f,%zu,%zu,%zu\n", name, r_.ops, r_.oom,
                    r_.peak_req, s.peak_in_use, s.high_water,
                    r_.peak_req ? double(s.peak_in_use) / double(r_.peak_req) : 0.0,
                    s.high_water ? double(r_.peak_req) / double(s.high_water) : 0.0, f.count,
                    f.bytes, f.largest);
    }

private:
    myalloc::Heap heap_;
    bool ok_ = false;
    std::vector<Live> live_;
    std::size_t req_ = 0;
    Result r_;
};

std::size_t log_uniform(std::mt19937& rng, double lo, double hi) {
    std::uniform_real_distribution<double> d(std::log(lo), std::log(hi));
    return static_cast<std::size_t>(std::exp(d(rng)));
}

// Random sizes 16 B..4 KiB, steady state around 2000 live blocks.
void random_sizes() {
    Driver d(16u << 20);
    std::mt19937 rng(1);
    for (int i = 0; i < 200000; ++i) {
        if (d.live() < 2000 || rng() % 2) d.alloc(log_uniform(rng, 16, 4096));
        else d.free_at(rng() % d.live());
    }
    d.report("random_16B_4KiB");
}

// App-like: ~95% small (16..256 B), ~5% large (4..64 KiB).
void app_like() {
    Driver d(32u << 20);
    std::mt19937 rng(2);
    for (int i = 0; i < 200000; ++i) {
        if (d.live() < 3000 || rng() % 2) {
            bool large = rng() % 100 < 5;
            d.alloc(large ? log_uniform(rng, 4096, 65536) : log_uniform(rng, 16, 256));
        } else {
            d.free_at(rng() % d.live());
        }
    }
    d.report("app_like");
}

// Phase shift: fill with large blocks, free them all, then fill with small
// blocks. Shows whether freed large blocks can serve small requests efficiently.
void phase_shift() {
    Driver d(16u << 20);
    std::mt19937 rng(3);
    for (int round = 0; round < 20; ++round) {
        for (int i = 0; i < 500; ++i) d.alloc(log_uniform(rng, 2048, 8192));
        while (d.live()) d.free_at(d.live() - 1);
        for (int i = 0; i < 5000; ++i) d.alloc(log_uniform(rng, 16, 128));
        while (d.live()) d.free_at(d.live() - 1);
    }
    d.report("phase_shift");
}

}  // namespace

int main() {
    std::puts(
        "workload,ops,oom,peak_requested,peak_in_use,high_water,internal_overhead,utilisation,"
        "free_blocks_end,free_bytes_end,largest_free_end");
    random_sizes();
    app_like();
    phase_shift();
}
