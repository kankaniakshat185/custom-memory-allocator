#include "heap.h"

namespace myalloc {

bool Heap::init(std::size_t arena_bytes) {
    if (!arena_.init(arena_bytes)) return false;
    auto* base = static_cast<std::uint8_t*>(arena_.base());
    first_ = base + kHeaderSize;
    epilogue_ = base + arena_.size() - kHeaderSize;
    reinterpret_cast<Header*>(epilogue_)->word = kAllocBit;  // size 0, allocated

    // Arena size is a page multiple, so this is a multiple of 16.
    Header* h = reinterpret_cast<Header*>(first_);
    h->word = static_cast<std::size_t>(epilogue_ - first_);
    footer(h)->word = h->word;
    list_push(h);
    return true;
}

void Heap::list_push(Header* h) {
    FreeLinks* l = links(h);
    l->prev = nullptr;
    l->next = free_head_;
    if (free_head_) links(free_head_)->prev = h;
    free_head_ = h;
    ++stats_.free_blocks;
}

void Heap::list_remove(Header* h) {
    FreeLinks* l = links(h);
    if (l->prev) links(l->prev)->next = l->next;
    else free_head_ = l->next;
    if (l->next) links(l->next)->prev = l->prev;
    --stats_.free_blocks;
}

Header* Heap::find_first_fit(std::size_t need) const {
    for (Header* h = free_head_; h; h = links(h)->next)
        if (block_size(h) >= need) return h;
    return nullptr;
}

void* Heap::malloc(std::size_t n) {
    if (arena_.base() == nullptr || n > arena_.size()) return nullptr;  // also guards overflow
    const std::size_t need = block_size_for(n);

    Header* h = find_first_fit(need);
    if (!h) return nullptr;
    list_remove(h);

    const std::size_t have = block_size(h);
    if (have - need >= kMinBlock) {
        // Allocate the front, return the tail to the free list (ADR-005).
        h->word = need | kAllocBit;
        Header* rest = next_block(h);
        rest->word = have - need;
        footer(rest)->word = rest->word;
        list_push(rest);
    } else {
        h->word |= kAllocBit;
    }

    const std::size_t end = static_cast<std::size_t>(reinterpret_cast<std::uint8_t*>(next_block(h)) -
                                                     static_cast<std::uint8_t*>(arena_.base()));
    if (end > stats_.high_water) stats_.high_water = end;
    stats_.bytes_in_use += block_size(h);
    if (stats_.bytes_in_use > stats_.peak_in_use) stats_.peak_in_use = stats_.bytes_in_use;
    ++stats_.live_blocks;
    return payload(h);
}

FreeStatus Heap::free(void* p) {
    if (p == nullptr) return FreeStatus::Ok;
    auto* b = static_cast<std::uint8_t*>(p);
    if (!arena_.contains(p) || reinterpret_cast<std::uintptr_t>(p) % kAlign != 0 ||
        b - kHeaderSize < first_ || b - kHeaderSize >= epilogue_)
        return FreeStatus::InvalidPointer;

    Header* h = header_of(p);
    if (!is_alloc(h)) return FreeStatus::DoubleFree;
    const std::size_t sz = block_size(h);
    if (sz < kMinBlock || sz % kAlign != 0 ||
        reinterpret_cast<std::uint8_t*>(h) + sz > epilogue_)
        return FreeStatus::InvalidPointer;

    stats_.bytes_in_use -= sz;
    --stats_.live_blocks;
    h->word = sz;  // clear alloc bit
    footer(h)->word = sz;
    list_push(h);
    return FreeStatus::Ok;
}

bool Heap::check(const char** why) const {
    auto fail = [&](const char* m) {
        if (why) *why = m;
        return false;
    };
    if (arena_.base() == nullptr) return fail("heap not initialised");
    if (reinterpret_cast<const Header*>(epilogue_)->word != kAllocBit)
        return fail("epilogue header corrupted");

    std::size_t walk_free = 0, walk_alloc = 0, in_use = 0;
    for (std::uint8_t* p = first_; p < epilogue_;) {
        Header* h = reinterpret_cast<Header*>(p);
        const std::size_t sz = block_size(h);
        if (sz < kMinBlock) return fail("block smaller than minimum");
        if (sz % kAlign != 0) return fail("block size not 16-aligned");
        if (p + sz > epilogue_) return fail("block overruns epilogue");
        if (reinterpret_cast<std::uintptr_t>(payload(h)) % kAlign != 0)
            return fail("payload misaligned");
        if (is_alloc(h)) {
            ++walk_alloc;
            in_use += sz;
        } else {
            ++walk_free;
            if (footer(h)->word != h->word) return fail("footer disagrees with header");
        }
        p += sz;
    }
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

FreeSummary Heap::free_summary() const {
    FreeSummary s;
    for (Header* h = free_head_; h; h = links(h)->next) {
        const std::size_t sz = block_size(h);
        ++s.count;
        s.bytes += sz;
        if (sz > s.largest) s.largest = sz;
    }
    return s;
}

}  // namespace myalloc
