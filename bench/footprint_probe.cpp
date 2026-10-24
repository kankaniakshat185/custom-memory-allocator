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
//
// `footprint_probe realloc` instead compares, on the same seeded buffer-growth
// workload: a naive malloc+copy+free policy; Heap::realloc (in place where
// possible, ADR-007); and a control that uses Heap::realloc only for growth and
// moves on shrink, to isolate the effect of in-place shrinking.
#include <cmath>
#include <cstdio>
#include <cstring>
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

template <class H>
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
        std::printf("%s,%zu,%zu,%zu,%zu,%zu,%.3f,%.3f,%zu,%zu,%zu,%.1f\n", name, r_.ops, r_.oom,
                    r_.peak_req, s.peak_in_use, s.high_water,
                    r_.peak_req ? double(s.peak_in_use) / double(r_.peak_req) : 0.0,
                    s.high_water ? double(r_.peak_req) / double(s.high_water) : 0.0, f.count,
                    f.bytes, f.largest, s.mallocs ? double(s.search_steps) / double(s.mallocs) : 0.0);
    }

private:
    H heap_;
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
template <class H>
void random_sizes() {
    Driver<H> d(16u << 20);
    std::mt19937 rng(1);
    for (int i = 0; i < 200000; ++i) {
        if (d.live() < 2000 || rng() % 2) d.alloc(log_uniform(rng, 16, 4096));
        else d.free_at(rng() % d.live());
    }
    d.report("random_16B_4KiB");
}

// App-like: ~95% small (16..256 B), ~5% large (4..64 KiB).
template <class H>
void app_like() {
    Driver<H> d(32u << 20);
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
template <class H>
void phase_shift() {
    Driver<H> d(16u << 20);
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

struct Buf {
    void* p;
    std::size_t n;
};

// Growable buffers (vector/string-like doubling up to 64 KiB) interleaved with
// small short-lived "noise" allocations that land next to them.
enum class Policy { Naive, InPlace, GrowOnlyInPlace };

template <class H>
void realloc_growth(Policy policy, std::size_t arena_mib) {
    H h;
    if (!h.init(arena_mib << 20)) return;
    std::mt19937 rng(4);
    std::vector<Buf> bufs, noise;
    std::size_t calls = 0, moved = 0, copied = 0, oom = 0, req = 0, peak_req = 0;
    auto track = [&](std::ptrdiff_t delta) {
        req = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(req) + delta);
        if (req > peak_req) peak_req = req;
    };
    auto drop = [&](std::vector<Buf>& v, std::size_t k) {
        track(-static_cast<std::ptrdiff_t>(v[k].n));
        h.free(v[k].p);
        v[k] = v.back();
        v.pop_back();
    };
    for (int i = 0; i < 200000; ++i) {
        const unsigned r = rng() % 100;
        if (r < 40 && !bufs.empty()) {
            Buf& b = bufs[rng() % bufs.size()];
            const std::size_t n = b.n >= 65536 ? 16 : b.n * 2;
            ++calls;
            void* q;
            const bool move = policy == Policy::Naive || (policy == Policy::GrowOnlyInPlace && n < b.n);
            if (move) {
                q = h.malloc(n);
                if (q) {
                    const std::size_t c = n < b.n ? n : b.n;
                    std::memcpy(q, b.p, c);
                    h.free(b.p);
                    ++moved;
                    copied += c;
                }
            } else {
                q = h.realloc(b.p, n);
            }
            if (!q) { ++oom; continue; }
            track(static_cast<std::ptrdiff_t>(n) - static_cast<std::ptrdiff_t>(b.n));
            b = {q, n};
        } else if (r < 70) {
            const std::size_t n = 16 + rng() % 241;
            if (void* p = h.malloc(n)) { noise.push_back({p, n}); track(static_cast<std::ptrdiff_t>(n)); }
            else ++oom;
        } else if (r < 85 && !noise.empty()) {
            drop(noise, rng() % noise.size());
        } else if (r < 92 || bufs.empty()) {
            if (void* p = h.malloc(16)) { bufs.push_back({p, 16}); track(16); }
            else ++oom;
        } else {
            drop(bufs, rng() % bufs.size());
        }
    }
    const auto& s = h.stats();
    moved += s.realloc_moved;
    copied += s.realloc_bytes_copied;
    const char* name = policy == Policy::Naive     ? "naive_move"
                       : policy == Policy::InPlace ? "in_place"
                                                   : "grow_in_place_shrink_moves";
    const auto f = h.free_summary();
    std::printf("%s,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%.3f,%zu,%zu,%.1f\n", name, arena_mib, calls,
                calls - moved, moved, copied, oom, peak_req, s.high_water,
                s.high_water ? double(peak_req) / double(s.high_water) : 0.0, f.count, f.largest,
                s.mallocs ? double(s.search_steps) / double(s.mallocs) : 0.0);
}

template <class H>
int run(bool realloc_mode) {
    if (realloc_mode) {
        std::puts("policy,arena_mib,realloc_calls,in_place,moved,bytes_copied,oom,peak_requested,"
                  "high_water,utilisation,free_blocks_end,largest_free_end,avg_search_steps");
        for (std::size_t mib : {32, 64}) {
            realloc_growth<H>(Policy::Naive, mib);
            realloc_growth<H>(Policy::InPlace, mib);
            realloc_growth<H>(Policy::GrowOnlyInPlace, mib);
        }
        return 0;
    }
    std::puts(
        "workload,ops,oom,peak_requested,peak_in_use,high_water,internal_overhead,utilisation,"
        "free_blocks_end,free_bytes_end,largest_free_end,avg_search_steps");
    random_sizes<H>();
    app_like<H>();
    phase_shift<H>();
    return 0;
}

// Usage: footprint_probe [realloc] [--best-fit]
int main(int argc, char** argv) {
    bool realloc_mode = false, best_fit = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "realloc") == 0) realloc_mode = true;
        else if (std::strcmp(argv[i], "--best-fit") == 0) best_fit = true;
    }
    return best_fit ? run<myalloc::BestFitHeap>(realloc_mode) : run<myalloc::FirstFitHeap>(realloc_mode);
}
