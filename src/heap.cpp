#include "heap.h"

#include <cstring>

namespace myalloc {

template <class Fit>
bool BasicHeap<Fit>::init(std::size_t arena_bytes) {
    if (!arena_.init(arena_bytes)) return false;
    auto* base = static_cast<std::uint8_t*>(arena_.base());
    first_ = base + kHeaderSize;
    epilogue_ = base + arena_.size() - kHeaderSize;
    reinterpret_cast<Header*>(epilogue_)->word = kAllocBit;  // size 0, allocated

    // Arena size is a page multiple, so this is a multiple of 16. The first
    // block claims an allocated predecessor so coalescing never looks before it.
    Header* h = reinterpret_cast<Header*>(first_);
    h->word = static_cast<std::size_t>(epilogue_ - first_) | kPrevAllocBit;
    write_footer(h);
    list_push(h);
    return true;
}

template <class Fit>
void BasicHeap<Fit>::list_push(Header* h) {
    FreeLinks* l = links(h);
    l->prev = nullptr;
    l->next = free_head_;
    if (free_head_) links(free_head_)->prev = h;
    free_head_ = h;
    ++stats_.free_blocks;
}

// Is h a block start inside the heap? Then its header and free-list links
// (h .. h + 24) can be read without leaving the arena. This is all the search
// needs to be safe on a pointer read from a freed payload (ADR-012).
template <class Fit>
bool BasicHeap<Fit>::in_heap(const Header* h) const {
    const auto off = static_cast<std::size_t>(reinterpret_cast<const std::uint8_t*>(h) - first_);
    return off < static_cast<std::size_t>(epilogue_ - first_) && off % kAlign == 0;
}

// Could h be a free block on the list? Reads only h's header (which a
// use-after-free write cannot reach) after in_heap(). Used before any link is
// rewritten. The footer is deliberately not checked: it is on a different
// cache line, and release() bounds it where it is used (ADR-012).
template <class Fit>
bool BasicHeap<Fit>::plausible_free(const Header* h) const {
    if (!in_heap(h) || is_alloc(h)) return false;
    const auto* p = reinterpret_cast<const std::uint8_t*>(h);
    const std::size_t sz = block_size(h);
    return sz >= kMinBlock && sz <= static_cast<std::size_t>(epilogue_ - p);
}

// Safe unlinking: h and both neighbours must be plausible and point back at
// each other before any link is rewritten.
template <class Fit>
bool BasicHeap<Fit>::unlinkable(Header* h) const {
    if (!plausible_free(h)) return false;
    const FreeLinks* l = links(h);
    if (l->prev ? !(plausible_free(l->prev) && links(l->prev)->next == h) : free_head_ != h)
        return false;
    if (l->next && !(plausible_free(l->next) && links(l->next)->prev == h)) return false;
    return true;
}

template <class Fit>
void BasicHeap<Fit>::unlink(Header* h) {
    FreeLinks* l = links(h);
    if (l->prev) links(l->prev)->next = l->next;
    else free_head_ = l->next;
    if (l->next) links(l->next)->prev = l->prev;
    --stats_.free_blocks;
}

template <class Fit>
bool BasicHeap<Fit>::list_remove(Header* h) {
    if (!unlinkable(h)) return mark_corrupt();
    unlink(h);
    return true;
}

// Each node must be in the heap (so reading it stays inside the arena) and the
// walk may not exceed the number of free blocks (so a cycle cannot hang it).
// Nothing more is checked per node: a forged link that stays in the heap is
// harmless to follow, and whichever block is chosen goes through safe
// unlinking before anything is written. ADR-012 records the measurements and
// mutation tests behind keeping this per-node check minimal.
template <class Fit>
Header* BasicHeap<Fit>::find_fit(std::size_t need) {
    ++stats_.mallocs;
    std::size_t visited = 0;
    auto valid = [&](Header* h) { return ++visited <= stats_.free_blocks && in_heap(h) && !is_alloc(h); };
    const FitResult r = Fit::find(free_head_, need, stats_.search_steps, valid);
    if (r.corrupt) mark_corrupt();
    return r.block;
}

// Turns the space [h, h + total) into an allocated block of `need` bytes and
// returns any remainder of at least kMinBlock to the free list (ADR-005).
// Precondition: the space was free, so the block after it has its prev-alloc
// bit clear; h's own prev-alloc bit is preserved.
template <class Fit>
void BasicHeap<Fit>::place(Header* h, std::size_t total, std::size_t need) {
    const std::uint64_t prev_bit = h->word & kPrevAllocBit;
    if (total - need >= kMinBlock) {
        h->word = need | kAllocBit | prev_bit;
        Header* rest = next_block(h);
        rest->word = (total - need) | kPrevAllocBit;
        write_footer(rest);
        list_push(rest);
    } else {
        h->word = total | kAllocBit | prev_bit;
        next_block(h)->word |= kPrevAllocBit;
    }
}

// Puts a block that is not on the free list back on it, merging with free
// neighbours first (ADR-006). h's size and prev-alloc bit must be valid; its
// alloc bit is cleared first so a stale header absorbed into the predecessor
// still reads as free (immediate double free stays detectable). The epilogue
// reads as allocated and the first block claims an allocated predecessor, so
// neither direction needs a boundary check. Both neighbours are validated
// before anything is modified (ADR-012); returns false on corruption.
template <class Fit>
bool BasicHeap<Fit>::release(Header* h) {
    Header* next = next_block(h);
    const bool merge_next = !is_alloc(next);
    Header* prev = nullptr;
    if (!prev_alloc(h)) {
        // The predecessor's footer lives in its payload, so it may have been
        // overwritten; bound it before using it to compute an address.
        const std::size_t prev_size = (h - 1)->word;
        const auto room = static_cast<std::size_t>(reinterpret_cast<std::uint8_t*>(h) - first_);
        if (prev_size < kMinBlock || prev_size % kAlign != 0 || prev_size > room) return mark_corrupt();
        prev = prev_block(h);
        if (block_size(prev) != prev_size || !unlinkable(prev)) return mark_corrupt();
    }
    if (merge_next && !unlinkable(next)) return mark_corrupt();

    h->word &= ~kAllocBit;
    Header* merged = h;
    std::size_t total = block_size(h);
    if (merge_next) {
        unlink(next);
        total += block_size(next);
    }
    if (prev) {
        unlink(prev);
        total += block_size(prev);
        merged = prev;
    }
    merged->word = total | (merged->word & kPrevAllocBit);
    write_footer(merged);
    next_block(merged)->word &= ~kPrevAllocBit;
    list_push(merged);
    return true;
}

template <class Fit>
void BasicHeap<Fit>::note_extent(Header* h) {
    const std::size_t end = static_cast<std::size_t>(reinterpret_cast<std::uint8_t*>(next_block(h)) -
                                                     static_cast<std::uint8_t*>(arena_.base()));
    if (end > stats_.high_water) stats_.high_water = end;
    if (stats_.bytes_in_use > stats_.peak_in_use) stats_.peak_in_use = stats_.bytes_in_use;
}

template <class Fit>
void* BasicHeap<Fit>::malloc(std::size_t n) {
    if (corrupt_ || arena_.base() == nullptr || n > arena_.size()) return nullptr;  // also guards overflow
    const std::size_t need = block_size_for(n);

    Header* h = find_fit(need);
    if (!h || !list_remove(h)) return nullptr;
    place(h, block_size(h), need);

    stats_.bytes_in_use += block_size(h);
    ++stats_.live_blocks;
    note_extent(h);
    return payload(h);
}

template <class Fit>
FreeStatus BasicHeap<Fit>::validate(void* p) const {
    auto* b = static_cast<std::uint8_t*>(p);
    if (!arena_.contains(p) || reinterpret_cast<std::uintptr_t>(p) % kAlign != 0 ||
        b - kHeaderSize < first_ || b - kHeaderSize >= epilogue_)
        return FreeStatus::InvalidPointer;

    const Header* h = header_of(p);
    if (!is_alloc(h)) return FreeStatus::DoubleFree;
    const std::size_t sz = block_size(h);
    if (sz < kMinBlock || sz % kAlign != 0 ||
        reinterpret_cast<const std::uint8_t*>(h) + sz > epilogue_)
        return FreeStatus::InvalidPointer;
    return FreeStatus::Ok;
}

template <class Fit>
FreeStatus BasicHeap<Fit>::free(void* p) {
    if (p == nullptr) return FreeStatus::Ok;
    if (corrupt_) return FreeStatus::HeapCorrupted;
    const FreeStatus st = validate(p);
    if (st != FreeStatus::Ok) return st;

    Header* h = header_of(p);
    const std::size_t sz = block_size(h);
    if (!release(h)) return FreeStatus::HeapCorrupted;
    stats_.bytes_in_use -= sz;
    --stats_.live_blocks;
    return FreeStatus::Ok;
}

template <class Fit>
void* BasicHeap<Fit>::realloc(void* p, std::size_t n, FreeStatus* status) {
    if (status) *status = corrupt_ ? FreeStatus::HeapCorrupted : FreeStatus::Ok;
    if (corrupt_) return nullptr;
    if (p == nullptr) return malloc(n);
    const FreeStatus st = validate(p);
    if (status) *status = st;
    if (st != FreeStatus::Ok) return nullptr;
    if (n > arena_.size()) return nullptr;  // also guards overflow in block_size_for

    Header* h = header_of(p);
    const std::size_t have = block_size(h);
    const std::size_t need = block_size_for(n);

    if (need <= have) {
        // Shrink (or no-op) in place. The tail goes through release() so it
        // merges with a free successor.
        if (have - need >= kMinBlock) {
            h->word = need | kAllocBit | (h->word & kPrevAllocBit);
            Header* rest = next_block(h);
            rest->word = (have - need) | kPrevAllocBit;
            if (!release(rest)) {
                if (status) *status = FreeStatus::HeapCorrupted;
                return nullptr;
            }
            stats_.bytes_in_use -= have - need;
        }
        ++stats_.realloc_in_place;
        return p;
    }

    // Grow in place by absorbing a free successor that is large enough.
    Header* next = next_block(h);
    if (!is_alloc(next) && have + block_size(next) >= need) {
        if (!list_remove(next)) {
            if (status) *status = FreeStatus::HeapCorrupted;
            return nullptr;
        }
        place(h, have + block_size(next), need);
        stats_.bytes_in_use += block_size(h) - have;
        note_extent(h);
        ++stats_.realloc_in_place;
        return p;
    }

    // Move. Allocate before freeing so a failure leaves p intact.
    void* q = malloc(n);
    if (!q) {
        if (status && corrupt_) *status = FreeStatus::HeapCorrupted;
        return nullptr;
    }
    const std::size_t copy = have - kHeaderSize;  // < n, since need > have
    std::memcpy(q, p, copy);
    free(p);
    ++stats_.realloc_moved;
    stats_.realloc_bytes_copied += copy;
    return q;
}

template <class Fit>
void* BasicHeap<Fit>::calloc(std::size_t count, std::size_t size) {
    std::size_t n;
    if (__builtin_mul_overflow(count, size, &n)) return nullptr;
    void* p = malloc(n);
    if (p) std::memset(p, 0, n);
    return p;
}

template <class Fit>
bool BasicHeap<Fit>::check(const char** why) const {
    auto fail = [&](const char* m) {
        if (why) *why = m;
        return false;
    };
    if (arena_.base() == nullptr) return fail("heap not initialised");
    if (corrupt_) return fail("heap marked corrupted (metadata overwritten)");
    const Header* epi = reinterpret_cast<const Header*>(epilogue_);
    if ((epi->word & ~kPrevAllocBit) != kAllocBit) return fail("epilogue header corrupted");

    std::size_t walk_free = 0, walk_alloc = 0, in_use = 0;
    bool prev_was_alloc = true;  // the first block claims an allocated predecessor
    for (std::uint8_t* p = first_; p < epilogue_;) {
        Header* h = reinterpret_cast<Header*>(p);
        const std::size_t sz = block_size(h);
        if (sz < kMinBlock) return fail("block smaller than minimum");
        if (sz % kAlign != 0) return fail("block size not 16-aligned");
        if (p + sz > epilogue_) return fail("block overruns epilogue");
        if (reinterpret_cast<std::uintptr_t>(payload(h)) % kAlign != 0)
            return fail("payload misaligned");
        if (prev_alloc(h) != prev_was_alloc) return fail("prev-alloc bit disagrees with predecessor");
        if (is_alloc(h)) {
            ++walk_alloc;
            in_use += sz;
        } else {
            ++walk_free;
            if (!prev_was_alloc) return fail("adjacent free blocks (missed coalesce)");
            if (footer(h)->word != sz) return fail("footer disagrees with header");
        }
        prev_was_alloc = is_alloc(h);
        p += sz;
    }
    if (prev_alloc(epi) != prev_was_alloc) return fail("epilogue prev-alloc bit wrong");
    if (walk_alloc != stats_.live_blocks) return fail("live block count mismatch");
    if (in_use != stats_.bytes_in_use) return fail("bytes_in_use mismatch");

    std::size_t list_len = 0;
    const Header* prev = nullptr;
    for (Header* h = free_head_; h; h = links(h)->next) {
        if (++list_len > walk_free) return fail("free list longer than free blocks (cycle?)");
        auto* hp = reinterpret_cast<std::uint8_t*>(h);
        if (hp < first_ || hp >= epilogue_) return fail("free list node outside heap");
        if (is_alloc(h)) return fail("allocated block on free list");
        if (links(h)->prev != prev) return fail("free list prev link broken");
        prev = h;
    }
    if (list_len != walk_free) return fail("free list length != free blocks in heap");
    if (list_len != stats_.free_blocks) return fail("free_blocks stat mismatch");
    return true;
}

template <class Fit>
FreeSummary BasicHeap<Fit>::free_summary() const {
    FreeSummary s;
    // Bounded and validated like find_fit, so it is safe after corruption.
    for (Header* h = free_head_; h && s.count < stats_.free_blocks && plausible_free(h);
         h = links(h)->next) {
        const std::size_t sz = block_size(h);
        ++s.count;
        s.bytes += sz;
        if (sz > s.largest) s.largest = sz;
    }
    return s;
}

template class BasicHeap<FirstFit>;
template class BasicHeap<BestFit>;

}  // namespace myalloc
