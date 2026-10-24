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
};

struct FreeSummary {
    std::size_t count = 0;
    std::size_t bytes = 0;
    std::size_t largest = 0;
};

// Explicit doubly-linked free list, LIFO insert, first-fit (ADR-002).
// The arena starts as one free block followed by a zero-size allocated
// epilogue header; oversized free blocks are split on allocation (ADR-005)
// and freed blocks merge with free neighbours in both directions (ADR-006).
// realloc resizes in place where possible (ADR-007). Single-threaded.
class Heap {
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
    Header* find_first_fit(std::size_t need) const;
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

// Process-wide heap behind the my_* C API (256 MiB, lazily created).
Heap& default_heap();

}  // namespace myalloc
