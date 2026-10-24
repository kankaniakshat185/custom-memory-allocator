#pragma once
#include <cstddef>

#include "arena.h"
#include "block.h"

namespace myalloc {

// Result of validating a pointer passed to free/realloc.
enum class FreeStatus { Ok, InvalidPointer, DoubleFree };

struct HeapStats {
    std::size_t bytes_in_use = 0;   // sum of allocated block sizes (incl. headers)
    std::size_t peak_in_use = 0;
    std::size_t live_blocks = 0;
    std::size_t high_water = 0;     // highest arena offset ever handed out (footprint proxy)
    std::size_t free_blocks = 0;
    std::size_t realloc_in_place = 0;  // served without moving (shrink, no-op, or grow into next)
    std::size_t realloc_moved = 0;     // malloc + copy + free
    std::size_t realloc_bytes_copied = 0;
    std::size_t mallocs = 0;        // calls that searched the free list
    std::size_t search_steps = 0;   // free-list nodes visited by those searches
};

struct FreeSummary {
    std::size_t count = 0;
    std::size_t bytes = 0;
    std::size_t largest = 0;
};

// Fit policies (ADR-009). find() returns a free block of at least `need`
// bytes, or nullptr, and adds the number of list nodes it visited to `steps`.
struct FirstFit {
    static Header* find(Header* head, std::size_t need, std::size_t& steps) {
        for (Header* h = head; h; h = links(h)->next) {
            ++steps;
            if (block_size(h) >= need) return h;
        }
        return nullptr;
    }
};

struct BestFit {
    static Header* find(Header* head, std::size_t need, std::size_t& steps) {
        Header* best = nullptr;
        std::size_t best_size = ~std::size_t{0};
        for (Header* h = head; h; h = links(h)->next) {
            ++steps;
            const std::size_t sz = block_size(h);
            if (sz >= need && sz < best_size) {
                best = h;
                best_size = sz;
                if (sz == need) break;  // cannot do better than exact
            }
        }
        return best;
    }
};

// Lets code built against both old and new trees detect the policy API.
#define MYALLOC_HAS_FIT_POLICIES 1

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

    const HeapStats& stats() const { return stats_; }
    const Arena& arena() const { return arena_; }

private:
    FreeStatus validate(void* p) const;
    Header* find_fit(std::size_t need);
    void place(Header* h, std::size_t total, std::size_t need);
    void release(Header* h);
    void note_extent(Header* h);
    void list_push(Header* h);
    void list_remove(Header* h);

    Arena arena_;
    std::uint8_t* first_ = nullptr;     // first block header (base + 8)
    std::uint8_t* epilogue_ = nullptr;  // zero-size allocated header at arena end - 8
    Header* free_head_ = nullptr;
    HeapStats stats_;
};

extern template class BasicHeap<FirstFit>;
extern template class BasicHeap<BestFit>;

using FirstFitHeap = BasicHeap<FirstFit>;
using BestFitHeap = BasicHeap<BestFit>;

// The default policy. Building with MYALLOC_BEST_FIT switches it, including
// for the my_* C API; the test suite is built once per policy (ADR-009).
#ifdef MYALLOC_BEST_FIT
using Heap = BestFitHeap;
#else
using Heap = FirstFitHeap;
#endif

// Process-wide heap behind the my_* C API (256 MiB, lazily created).
Heap& default_heap();

}  // namespace myalloc
