#include "workloads.h"

#include <cmath>
#include <random>

namespace bench {
namespace {

std::uint32_t log_uniform(std::mt19937& rng, double lo, double hi) {
    std::uniform_real_distribution<double> d(std::log(lo), std::log(hi));
    return static_cast<std::uint32_t>(std::exp(d(rng)));
}

// Tracks which slots are live and the requested size in each, so generated
// traces are always valid (never free an empty slot, never leak at the end).
class Builder {
public:
    Builder(std::string name, std::size_t slots) : size_(slots, 0), pos_(slots, 0) {
        t_.name = std::move(name);
        t_.slots = slots;
        for (std::size_t i = slots; i-- > 0;) free_.push_back(static_cast<std::uint32_t>(i));
    }
    bool full() const { return free_.empty(); }
    std::size_t live() const { return live_.size(); }

    std::uint32_t alloc(std::uint32_t n) {
        const std::uint32_t s = free_.back();
        free_.pop_back();
        pos_[s] = live_.size();
        live_.push_back(s);
        size_[s] = n;
        t_.ops.push_back({OpKind::Alloc, s, n, 0});
        return s;
    }
    void free_slot(std::uint32_t s) {
        const std::size_t k = pos_[s];
        live_[k] = live_.back();
        pos_[live_[k]] = k;
        live_.pop_back();
        free_.push_back(s);
        t_.ops.push_back({OpKind::Free, s, 0, 0});
    }
    void free_random(std::mt19937& rng) { free_slot(random_live(rng)); }
    std::uint32_t random_live(std::mt19937& rng) const { return live_[rng() % live_.size()]; }
    std::uint32_t size_of(std::uint32_t s) const { return size_[s]; }
    void realloc(std::uint32_t s, std::uint32_t n) {
        t_.ops.push_back({OpKind::Realloc, s, n, size_[s]});
        t_.uses_realloc = true;
        size_[s] = n;
    }
    Trace finish() {
        while (!live_.empty()) free_slot(live_.back());
        return std::move(t_);
    }

private:
    Trace t_;
    std::vector<std::uint32_t> free_, live_;
    std::vector<std::uint32_t> size_;
    std::vector<std::size_t> pos_;
};

}  // namespace

Trace random_sizes() {
    Builder b("random_16B_64KiB", 4096);
    std::mt19937 rng(11);
    while (b.live() < 2000) b.alloc(log_uniform(rng, 16, 65536));
    for (int i = 0; i < 1000000; ++i) {
        if (b.live() > 0 && (rng() % 2 || b.full())) b.free_random(rng);
        else b.alloc(log_uniform(rng, 16, 65536));
    }
    return b.finish();
}

Trace app_like() {
    Builder b("app_like", 8192);
    std::mt19937 rng(12);
    auto size = [&] {
        return rng() % 100 < 95 ? log_uniform(rng, 16, 256) : log_uniform(rng, 4096, 262144);
    };
    while (b.live() < 3000) b.alloc(size());
    for (int i = 0; i < 1000000; ++i) {
        if (b.live() > 0 && (rng() % 2 || b.full())) b.free_random(rng);
        else b.alloc(size());
    }
    return b.finish();
}

// Worst case for a single LIFO first-fit list (ADR-008). Each round:
//   1. 1000 "fit" blocks of exactly 4000 B, each pinned by a live 32 B block;
//   2. 2000 "hole" blocks of 1-2 KiB, each pinned the same way;
//   3. free the fit blocks first, then the holes, so LIFO order puts all 2000
//      non-fitting holes in front of the fit blocks (pins stop coalescing);
//   4. request 4000 B 1000 times. Each request walks every hole, then takes a
//      fit block exactly, so no remainder is pushed to the list head and the
//      next request walks the holes again.
// A size-segregated allocator finds a 4000 B block without looking at holes.
Trace adversarial() {
    Builder b("adversarial_holes", 8192);
    std::mt19937 rng(13);
    for (int round = 0; round < 30; ++round) {
        std::vector<std::uint32_t> pins, fit, holes, got;
        for (int i = 0; i < 1000; ++i) {
            fit.push_back(b.alloc(4000));
            pins.push_back(b.alloc(24));
        }
        for (int i = 0; i < 2000; ++i) {
            holes.push_back(b.alloc(1024 + rng() % 1025));
            pins.push_back(b.alloc(24));
        }
        for (auto s : fit) b.free_slot(s);
        for (auto s : holes) b.free_slot(s);
        for (int i = 0; i < 1000; ++i) got.push_back(b.alloc(4000));
        for (auto s : got) b.free_slot(s);
        for (auto s : pins) b.free_slot(s);
    }
    return b.finish();
}

// Same shape as footprint_probe's realloc workload: growable buffers that
// double up to 64 KiB then reset, interleaved with small noise allocations.
Trace realloc_growth() {
    Builder b("realloc_growth", 16384);
    std::mt19937 rng(14);
    std::vector<std::uint32_t> bufs, noise;
    auto drop = [&](std::vector<std::uint32_t>& v, std::size_t k) {
        b.free_slot(v[k]);
        v[k] = v.back();
        v.pop_back();
    };
    for (int i = 0; i < 500000; ++i) {
        const unsigned r = rng() % 100;
        if (r < 40 && !bufs.empty()) {
            const std::uint32_t s = bufs[rng() % bufs.size()];
            const std::uint32_t n = b.size_of(s) >= 65536 ? 16 : b.size_of(s) * 2;
            b.realloc(s, n);
        } else if (r < 70 && !b.full()) {
            noise.push_back(b.alloc(16 + rng() % 241));
        } else if (r < 85 && !noise.empty()) {
            drop(noise, rng() % noise.size());
        } else if ((r < 92 || bufs.empty()) && !b.full()) {
            bufs.push_back(b.alloc(16));
        } else if (!bufs.empty()) {
            drop(bufs, rng() % bufs.size());
        }
    }
    return b.finish();
}

std::vector<Trace> all_traces() {
    std::vector<Trace> t;
    t.push_back(random_sizes());
    t.push_back(app_like());
    t.push_back(adversarial());
    t.push_back(realloc_growth());
    return t;
}

}  // namespace bench
