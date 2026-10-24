#pragma once
#include <cstddef>

#include "arena.h"
#include "block.h"

namespace myalloc {

enum class FreeStatus { Ok, InvalidPointer, DoubleFree };

struct HeapStats {
    std::size_t bytes_in_use = 0;   // sum of allocated block sizes (incl. headers)
    std::size_t peak_in_use = 0;
    std::size_t live_blocks = 0;
    std::size_t high_water = 0;     // bytes of arena ever carved
    std::size_t free_blocks = 0;
};

// Phase 1 baseline (ADR-002): explicit doubly-linked free list, LIFO insert,
// first-fit, no splitting, no coalescing. Fresh memory is bump-carved from the
// uncarved tail of the arena when no free block fits.
// Single-threaded.
class Heap {
public:
    bool init(std::size_t arena_bytes);

    void* malloc(std::size_t n);
    FreeStatus free(void* p);

    // Walks the whole heap and the free list. Returns true if every invariant
    // holds; otherwise false and, if `why` is non-null, a static description.
    bool check(const char** why = nullptr) const;

    const HeapStats& stats() const { return stats_; }
    const Arena& arena() const { return arena_; }

private:
    Header* find_first_fit(std::size_t need) const;
    void list_push(Header* h);
    void list_remove(Header* h);

    Arena arena_;
    std::uint8_t* first_ = nullptr;  // first block header (base + 8)
    std::uint8_t* top_ = nullptr;    // next uncarved header address
    std::uint8_t* end_ = nullptr;    // arena end
    Header* free_head_ = nullptr;
    HeapStats stats_;
};

// Process-wide heap behind my_malloc / my_free (256 MiB, lazily created).
Heap& default_heap();

}  // namespace myalloc
