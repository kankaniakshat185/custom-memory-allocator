#pragma once
#include <cstddef>

#include "arena.h"
#include "block.h"

namespace myalloc {

// Result of validating a pointer passed to free/realloc. HeapCorrupted means
// free-list metadata was found overwritten (e.g. by a use-after-free write);
// the heap then refuses all further operations (ADR-012).
enum class FreeStatus { Ok, InvalidPointer, DoubleFree, HeapCorrupted };

struct HeapStats {
    std::size_t bytes_in_use = 0;  // sum of allocated block sizes (incl. headers)
    std::size_t peak_in_use = 0;
    std::size_t live_blocks = 0;
    std::size_t high_water = 0;  // highest arena offset ever handed out (footprint proxy)
    std::size_t free_blocks = 0;
    std::size_t realloc_in_place = 0;  // served without moving (shrink, no-op, or grow into next)
    std::size_t realloc_moved = 0;     // malloc + copy + free
    std::size_t realloc_bytes_copied = 0;
    std::size_t mallocs = 0;       // calls that searched the free list
    std::size_t search_steps = 0;  // free-list nodes visited by those searches
};

struct FreeSummary {
    std::size_t count = 0;
    std::size_t bytes = 0;
    std::size_t largest = 0;
};

struct FitResult {
    Header* block;  // nullptr if nothing fits (or corrupt)
    bool corrupt;   // a list node failed validation; nothing past it was read
};

// Free-list heads, one per size bin, plus a bitmap of non-empty bins
// (ADR-011). First-fit and best-fit use a single bin, so for them this is the
// original single LIFO list.
template <std::size_t N>
struct FreeLists {
    static constexpr std::size_t kWords = (N + 63) / 64;
    Header* head[N] = {};
    std::uint64_t nonempty[kWords] = {};

    void mark(std::size_t b) { nonempty[b / 64] |= std::uint64_t{1} << (b % 64); }
    void clear(std::size_t b) { nonempty[b / 64] &= ~(std::uint64_t{1} << (b % 64)); }
    bool marked(std::size_t b) const { return (nonempty[b / 64] >> (b % 64)) & 1u; }
    // Lowest non-empty bin >= b, or N if none.
    std::size_t next_nonempty(std::size_t b) const {
        for (std::size_t w = b / 64; w < kWords; ++w) {
            std::uint64_t bits = nonempty[w];
            if (w == b / 64) bits &= ~std::uint64_t{0} << (b % 64);
            if (bits) return w * 64 + static_cast<std::size_t>(__builtin_ctzll(bits));
        }
        return N;
    }
};

// Fit policies (ADR-009, ADR-011). Each declares its bin count and size->bin
// map; find() returns a free block of at least `need` bytes and adds the
// number of list nodes it visited to `steps`. Every node is passed to
// valid(node) before its size or links are read, so links overwritten by a
// use-after-free are never followed (ADR-012).
struct FirstFit {
    static constexpr std::size_t kBins = 1;
    static constexpr std::size_t bin_of(std::size_t) { return 0; }

    template <class Valid>
    static FitResult find(const FreeLists<kBins>& lists, std::size_t need, std::size_t& steps,
                          Valid&& valid) {
        for (Header* h = lists.head[0]; h; h = links(h)->next) {
            if (!valid(h)) return {nullptr, true};
            ++steps;
            if (block_size(h) >= need) return {h, false};
        }
        return {nullptr, false};
    }
};

struct BestFit {
    static constexpr std::size_t kBins = 1;
    static constexpr std::size_t bin_of(std::size_t) { return 0; }

    template <class Valid>
    static FitResult find(const FreeLists<kBins>& lists, std::size_t need, std::size_t& steps,
                          Valid&& valid) {
        Header* best = nullptr;
        std::size_t best_size = ~std::size_t{0};
        for (Header* h = lists.head[0]; h; h = links(h)->next) {
            if (!valid(h)) return {nullptr, true};
            ++steps;
            const std::size_t sz = block_size(h);
            if (sz >= need && sz < best_size) {
                best = h;
                best_size = sz;
                if (sz == need) break;  // cannot do better than exact
            }
        }
        return {best, false};
    }
};

// Segregated fits (ADR-011): exact-size bins for blocks up to 1 KiB (one per
// 16 B step), then power-of-two range bins. A request scans its own bin
// first-fit (any block in an exact bin fits); if nothing there fits, the
// bitmap finds the next non-empty larger bin, whose every block fits, so the
// search never visits blocks of a smaller size class.
struct SegregatedFit {
    static constexpr std::size_t kExactMax = 1024;
    static constexpr std::size_t kExactBins = (kExactMax - kMinBlock) / kAlign + 1;  // 63
    static constexpr std::size_t kBins = 128;

    static constexpr std::size_t bin_of(std::size_t size) {
        if (size <= kExactMax) return (size - kMinBlock) / kAlign;
        const std::size_t log2 = 63 - static_cast<std::size_t>(__builtin_clzll(size));  // >= 10
        const std::size_t b = kExactBins + (log2 - 10);
        return b < kBins ? b : kBins - 1;
    }

    template <class Valid>
    static FitResult find(const FreeLists<kBins>& lists, std::size_t need, std::size_t& steps,
                          Valid&& valid) {
        const std::size_t b = bin_of(need);
        for (Header* h = lists.head[b]; h; h = links(h)->next) {
            if (!valid(h)) return {nullptr, true};
            ++steps;
            if (block_size(h) >= need) return {h, false};
        }
        const std::size_t next = lists.next_nonempty(b + 1);
        if (next == kBins) return {nullptr, false};
        Header* h = lists.head[next];
        if (!valid(h)) return {nullptr, true};
        ++steps;
        return {h, false};  // every block in a larger bin is larger than `need`
    }
};

// Lets code built against both old and new trees detect the policy API.
#define MYALLOC_HAS_FIT_POLICIES 1
#define MYALLOC_HAS_SEGREGATED_FIT 1

// Explicit doubly-linked free list, LIFO insert (ADR-002), fit policy chosen
// at compile time (ADR-009). The arena starts as one free block followed by a
// zero-size allocated epilogue header; oversized free blocks are split on
// allocation (ADR-005) and freed blocks merge with free neighbours in both
// directions (ADR-006). realloc resizes in place where possible (ADR-007).
// Single-threaded.
template <class Fit>
class BasicHeap {
public:
    bool init(std::size_t arena_bytes);

    void* malloc(std::size_t n);
    FreeStatus free(void* p);
    // On failure returns nullptr and leaves p untouched. If `status` is
    // non-null it receives the validation result for p.
    void* realloc(void* p, std::size_t n, FreeStatus* status = nullptr);
    void* calloc(std::size_t count, std::size_t size);

    // Walks the whole heap and the free list. Returns true if every invariant
    // holds; otherwise false and, if `why` is non-null, a static description.
    bool check(const char** why = nullptr) const;

    // Walks the free list (O(free blocks)); for tests and footprint probes.
    FreeSummary free_summary() const;

    // Calls f(payload, usable_bytes) for every allocated block in address order
    // (ADR-013). Reads headers only; stops at the first implausible header and
    // returns false, so it is safe on a damaged heap. O(blocks).
    template <class F>
    bool for_each_live(F&& f) const {
        for (std::uint8_t* p = first_; p && p < epilogue_;) {
            auto* h = reinterpret_cast<Header*>(p);
            const std::size_t sz = block_size(h);
            if (sz < kMinBlock || sz > static_cast<std::size_t>(epilogue_ - p)) return false;
            if (is_alloc(h)) f(payload(h), sz - kHeaderSize);
            p += sz;
        }
        return true;
    }

    const HeapStats& stats() const { return stats_; }
    const Arena& arena() const { return arena_; }
    // Sticky: set when free-list metadata fails validation (ADR-012).
    bool corrupted() const { return corrupt_; }

private:
    FreeStatus validate(void* p) const;
    Header* find_fit(std::size_t need);
    void place(Header* h, std::size_t total, std::size_t need);
    bool release(Header* h);
    void note_extent(Header* h);
    void list_push(Header* h);
    bool in_heap(const Header* h) const;
    bool plausible_free(const Header* h) const;
    bool unlinkable(Header* h) const;
    void unlink(Header* h);
    bool list_remove(Header* h);
    bool mark_corrupt() {
        corrupt_ = true;
        return false;
    }

    Arena arena_;
    std::uint8_t* first_ = nullptr;     // first block header (base + 8)
    std::uint8_t* epilogue_ = nullptr;  // zero-size allocated header at arena end - 8
    FreeLists<Fit::kBins> lists_;
    HeapStats stats_;
    bool corrupt_ = false;
};

extern template class BasicHeap<FirstFit>;
extern template class BasicHeap<BestFit>;
extern template class BasicHeap<SegregatedFit>;

using FirstFitHeap = BasicHeap<FirstFit>;
using BestFitHeap = BasicHeap<BestFit>;
using SegregatedFitHeap = BasicHeap<SegregatedFit>;

// The default policy is segregated fits (ADR-011). MYALLOC_FIRST_FIT or
// MYALLOC_BEST_FIT switches it, including for the my_* C API; the test suite
// is built once per policy.
#if defined(MYALLOC_FIRST_FIT)
using Heap = FirstFitHeap;
#elif defined(MYALLOC_BEST_FIT)
using Heap = BestFitHeap;
#else
using Heap = SegregatedFitHeap;
#endif

// Process-wide heap behind the my_* C API (256 MiB, lazily created).
Heap& default_heap();

}  // namespace myalloc
