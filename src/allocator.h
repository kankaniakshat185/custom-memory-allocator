#pragma once
#include <cstddef>
#include <cstring>

#include "heap.h"
#include "slab.h"

// Lets code built against older trees detect the slab front end.
#define MYALLOC_HAS_SLAB 1

namespace myalloc {

// The allocator behind the C API (ADR-014): requests up to 256 B go to the
// small-object slab cache, whose slabs are carved from the heap; everything
// else (and small requests if no slab can be had) goes to the general heap
// with fit policy Fit. free and realloc find slab pointers with one bit test.
template <class Fit>
class BasicAllocator {
public:
    using Heap = BasicHeap<Fit>;
    using Slabs = SlabCache<Heap>;

    bool init(std::size_t heap_bytes) { return heap_.init(heap_bytes) && slab_.init(heap_); }

    void* malloc(std::size_t n) {
        void* p = n <= Slabs::kMaxSize ? slab_.malloc(n) : nullptr;
        if (p == nullptr) p = heap_.malloc(n);
        if (p) note_peak();
        return p;
    }

    FreeStatus free(void* p) {
        if (p == nullptr) return FreeStatus::Ok;
        const typename Slabs::FreeResult r = slab_.free(p);
        if (r == Slabs::FreeResult::NotMine) return heap_.free(p);
        return to_status(r);
    }

    void* realloc(void* p, std::size_t n, FreeStatus* status = nullptr) {
        if (status) *status = FreeStatus::Ok;
        if (p == nullptr || !slab_.owns(p)) {
            void* q = p == nullptr ? malloc(n) : heap_.realloc(p, n, status);
            if (q) note_peak();
            return q;
        }
        const typename Slabs::FreeResult r = slab_.classify(p);
        if (r != Slabs::FreeResult::Ok) {
            if (status) *status = to_status(r);
            return nullptr;
        }
        const std::size_t have = slab_.usable_size(p);
        if (n <= have) return p;  // still fits its slot: in place
        void* q = malloc(n);
        if (q == nullptr) return nullptr;  // p stays valid
        std::memcpy(q, p, have);
        slab_.free(p);
        return q;
    }

    void* calloc(std::size_t count, std::size_t size) {
        std::size_t n;
        if (__builtin_mul_overflow(count, size, &n)) return nullptr;
        void* p = malloc(n);
        if (p) std::memset(p, 0, n);
        return p;
    }

    // User-level view, shaped like HeapStats so probes and the benchmark
    // harness can drive either: slabs are heap blocks, so they are taken out
    // of the heap's in-use counts and replaced by the live slab objects.
    // high_water is the heap's (slabs live inside the heap arena).
    HeapStats stats() const {
        HeapStats s = heap_.stats();
        const typename Slabs::Stats& c = slab_.stats();
        s.bytes_in_use = s.bytes_in_use - c.slab_block_bytes + c.slot_bytes_in_use;
        s.live_blocks = s.live_blocks - c.slabs_in_use + c.live_objects;
        s.peak_in_use = peak_;
        return s;
    }
    FreeSummary free_summary() const { return heap_.free_summary(); }
    bool check(const char** why = nullptr) const { return heap_.check(why) && slab_.check(why); }
    bool corrupted() const { return heap_.corrupted() || slab_.corrupted(); }

    Heap& heap() { return heap_; }
    const Heap& heap() const { return heap_; }
    const Slabs& slab() const { return slab_; }
    const Arena& arena() const { return heap_.arena(); }

private:
    static FreeStatus to_status(typename Slabs::FreeResult r) {
        switch (r) {
            case Slabs::FreeResult::Ok:
                return FreeStatus::Ok;
            case Slabs::FreeResult::DoubleFree:
                return FreeStatus::DoubleFree;
            case Slabs::FreeResult::Corrupted:
                return FreeStatus::HeapCorrupted;
            default:
                return FreeStatus::InvalidPointer;
        }
    }
    void note_peak() {
        const typename Slabs::Stats& c = slab_.stats();
        const std::size_t in_use =
            heap_.stats().bytes_in_use - c.slab_block_bytes + c.slot_bytes_in_use;
        if (in_use > peak_) peak_ = in_use;
    }

    Heap heap_;
    Slabs slab_;
    std::size_t peak_ = 0;
};

using Allocator = BasicAllocator<SegregatedFit>;

}  // namespace myalloc
