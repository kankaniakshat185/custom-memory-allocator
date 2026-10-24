#include "heap.h"

namespace myalloc {

bool Heap::init(std::size_t arena_bytes) {
    if (!arena_.init(arena_bytes)) return false;
    first_ = static_cast<std::uint8_t*>(arena_.base()) + kHeaderSize;
    top_ = first_;
    end_ = static_cast<std::uint8_t*>(arena_.base()) + arena_.size();
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
    if (h) {
        list_remove(h);
        h->word |= kAllocBit;
    } else {
        if (static_cast<std::size_t>(end_ - top_) < need) return nullptr;
        h = reinterpret_cast<Header*>(top_);
        h->word = need | kAllocBit;
        top_ += need;
        stats_.high_water = static_cast<std::size_t>(top_ - first_) + kHeaderSize;
    }
    stats_.bytes_in_use += block_size(h);
    if (stats_.bytes_in_use > stats_.peak_in_use) stats_.peak_in_use = stats_.bytes_in_use;
    ++stats_.live_blocks;
    return payload(h);
}

FreeStatus Heap::free(void* p) {
    if (p == nullptr) return FreeStatus::Ok;
    auto* b = static_cast<std::uint8_t*>(p);
    if (!arena_.contains(p) || reinterpret_cast<std::uintptr_t>(p) % kAlign != 0 ||
        b - kHeaderSize < first_ || b >= top_)
        return FreeStatus::InvalidPointer;

    Header* h = header_of(p);
    if (!is_alloc(h)) return FreeStatus::DoubleFree;
    const std::size_t sz = block_size(h);
    if (sz < kMinBlock || sz % kAlign != 0 || reinterpret_cast<std::uint8_t*>(h) + sz > top_)
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
    if (top_ < first_ || top_ > end_) return fail("top out of range");

    std::size_t walk_free = 0, walk_alloc = 0, in_use = 0;
    for (std::uint8_t* p = first_; p < top_;) {
        Header* h = reinterpret_cast<Header*>(p);
        const std::size_t sz = block_size(h);
        if (sz < kMinBlock) return fail("block smaller than minimum");
        if (sz % kAlign != 0) return fail("block size not 16-aligned");
        if (p + sz > top_) return fail("block overruns top");
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
        if (hp < first_ || hp >= top_) return fail("free list node outside heap");
        if (is_alloc(h)) return fail("allocated block on free list");
        if (links(h)->prev != prev) return fail("free list prev link broken");
        prev = h;
    }
    if (list_len != walk_free) return fail("free list length != free blocks in heap");
    if (list_len != stats_.free_blocks) return fail("free_blocks stat mismatch");
    return true;
}

}  // namespace myalloc
