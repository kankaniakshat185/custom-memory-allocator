#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "heap.h"

namespace myalloc {

// Small-object slab cache (ADR-014). Requests up to kMaxSize bytes are served
// from 16 KiB slabs, each holding equal-size slots of one size class (16, 32,
// ..., 256 B). Slabs are carved from the general heap H as 16 KiB-aligned
// blocks of exactly 16 KiB, and an emptied slab is freed back to it, so memory
// moves freely between small and large objects. All metadata (a bitmap of free
// slots in each slab's header, plus one bit per 16 KiB chunk of the heap
// marking slabs) is out of band: freed slots hold no links, so a write through
// a dangling pointer cannot corrupt the cache, and a double free is caught
// exactly by the bitmap.
template <class H>
class SlabCache {
public:
    static constexpr std::size_t kSlabBytes = 16384;  // alignment and chunk size
    static constexpr std::size_t kSlabUsable = kSlabBytes - kHeaderSize;  // block is exactly 16 KiB
    static constexpr std::size_t kHeaderBytes = 256;  // slots start after the header
    static constexpr std::size_t kQuantum = 16;
    static constexpr std::size_t kMaxSize = 256;
    static constexpr std::size_t kClasses = kMaxSize / kQuantum;                       // 16
    static constexpr std::size_t kMaxSlots = (kSlabUsable - kHeaderBytes) / kQuantum;  // 1007
    static constexpr std::size_t kBitmapWords = (kMaxSlots + 63) / 64;                 // 16

    enum class FreeResult { Ok, NotMine, InvalidPointer, DoubleFree, Corrupted };

    struct Stats {
        std::size_t slot_bytes_in_use = 0;  // sum of slot sizes of live objects
        std::size_t live_objects = 0;
        std::size_t slabs_in_use = 0;
        std::size_t slab_block_bytes = 0;  // heap bytes held by slabs (their block sizes)
    };

    bool init(H& heap) {
        heap_ = &heap;
        const auto base = reinterpret_cast<std::uintptr_t>(heap.arena().base());
        if (base == 0) return false;
        const std::uintptr_t lo = base & ~std::uintptr_t{kSlabBytes - 1};
        chunk_base_ = reinterpret_cast<std::uint8_t*>(lo);
        nchunks_ = (base + heap.arena().size() - lo + kSlabBytes - 1) / kSlabBytes;
        is_slab_.assign((nchunks_ + 63) / 64, 0);
        return true;
    }

    // Returns nullptr if n is too large for the cache or the heap cannot
    // provide a new slab (the caller then falls back to the heap).
    void* malloc(std::size_t n) {
        if (n > kMaxSize || corrupt_ || heap_ == nullptr) return nullptr;
        const std::size_t cls = class_of(n);
        SlabHeader* s = partial_[cls];
        if (s == nullptr) {
            s = take_slab(cls);
            if (s == nullptr) return nullptr;
            partial_push(s);
        } else if (!valid_header(s) || s->cls != cls) {
            fail();
            return nullptr;
        }
        for (std::size_t w = s->hint; w < kBitmapWords; ++w) {
            if (const std::uint64_t bits = s->free_bits[w]) {
                const std::size_t bit = static_cast<std::size_t>(__builtin_ctzll(bits));
                s->free_bits[w] = bits & (bits - 1);
                s->hint = static_cast<std::uint32_t>(w);
                if (++s->live == s->capacity) partial_remove(s);
                const std::size_t size = slot_size(cls);
                stats_.slot_bytes_in_use += size;
                ++stats_.live_objects;
                return slots(s) + (w * 64 + bit) * size;
            }
        }
        fail();  // a partial slab with no free bit: the header lies
        return nullptr;
    }

    FreeResult free(void* p) {
        const FreeResult r = classify(p);
        if (r == FreeResult::Corrupted) fail();
        if (r != FreeResult::Ok) return r;
        SlabHeader* s = slab_of(p);
        const std::size_t size = slot_size(s->cls);
        const std::size_t idx =
            static_cast<std::size_t>(static_cast<std::uint8_t*>(p) - slots(s)) / size;
        s->free_bits[idx / 64] |= std::uint64_t{1} << (idx % 64);
        if (idx / 64 < s->hint) s->hint = static_cast<std::uint32_t>(idx / 64);
        const bool was_full = s->live == s->capacity;
        --s->live;
        stats_.slot_bytes_in_use -= size;
        --stats_.live_objects;
        if (was_full) partial_push(s);
        // Give an emptied slab back to the heap, but keep the class's last
        // partial slab so a single alloc/free pair does not churn slabs.
        if (s->live == 0 && !(partial_[s->cls] == s && s->next == nullptr)) {
            partial_remove(s);
            release(s);
        }
        return FreeResult::Ok;
    }

    // Slab membership is one bit per 16 KiB chunk, kept outside the arena.
    bool owns(const void* p) const {
        const auto* b = static_cast<const std::uint8_t*>(p);
        if (chunk_base_ == nullptr || b < chunk_base_) return false;
        const auto chunk = static_cast<std::size_t>(b - chunk_base_) / kSlabBytes;
        return chunk < nchunks_ && ((is_slab_[chunk / 64] >> (chunk % 64)) & 1u);
    }

    // Classifies p without changing anything: Ok if it is a live object.
    FreeResult classify(const void* p) const {
        if (!owns(p)) return FreeResult::NotMine;
        if (corrupt_) return FreeResult::Corrupted;
        const SlabHeader* s = slab_of(p);
        if (!valid_header(s)) return FreeResult::Corrupted;
        const auto* b = static_cast<const std::uint8_t*>(p);
        const std::size_t size = slot_size(s->cls);
        if (b < slots(s) || static_cast<std::size_t>(b - slots(s)) % size != 0)
            return FreeResult::InvalidPointer;
        const std::size_t idx = static_cast<std::size_t>(b - slots(s)) / size;
        if (idx >= s->capacity) return FreeResult::InvalidPointer;
        if ((s->free_bits[idx / 64] >> (idx % 64)) & 1u) return FreeResult::DoubleFree;
        return FreeResult::Ok;
    }

    // Usable size of a slab object (its slot size); 0 if p is not a live slot.
    std::size_t usable_size(const void* p) const {
        return classify(p) == FreeResult::Ok ? slot_size(slab_of(p)->cls) : 0;
    }

    // Calls f(ptr, slot_bytes) for every live object, slab by slab in address
    // order. Returns false if a slab header failed validation.
    template <class F>
    bool for_each_live(F&& f) const {
        for (std::size_t c = 0; c < nchunks_; ++c) {
            if (!((is_slab_[c / 64] >> (c % 64)) & 1u)) continue;
            const auto* s = reinterpret_cast<const SlabHeader*>(chunk_base_ + c * kSlabBytes);
            if (!valid_header(s)) return false;
            const std::size_t size = slot_size(s->cls);
            for (std::size_t k = 0; k < s->capacity; ++k)
                if (!((s->free_bits[k / 64] >> (k % 64)) & 1u))
                    f(const_cast<std::uint8_t*>(slots(s)) + k * size, size);
        }
        return true;
    }

    // Verifies every slab header and bitmap, that each slab is a live heap
    // block, the partial lists and the counters. For tests.
    bool check(const char** why = nullptr) const {
        auto bad = [&](const char* m) {
            if (why) *why = m;
            return false;
        };
        if (corrupt_) return bad("slab cache marked corrupted");
        std::size_t slabs = 0, live = 0, bytes = 0, partial_expected = 0, block_bytes = 0;
        for (std::size_t c = 0; c < nchunks_; ++c) {
            if (!((is_slab_[c / 64] >> (c % 64)) & 1u)) continue;
            const auto* s = reinterpret_cast<const SlabHeader*>(chunk_base_ + c * kSlabBytes);
            if (!valid_header(s)) return bad("invalid slab header");
            const auto* hb = reinterpret_cast<const Header*>(
                reinterpret_cast<const std::uint8_t*>(s) - kHeaderSize);
            if (!is_alloc(hb) || block_size(hb) < kSlabBytes)
                return bad("slab is not a live heap block of at least 16 KiB");
            std::size_t free_count = 0;
            for (std::size_t w = 0; w < kBitmapWords; ++w) {
                if (s->free_bits[w] & ~valid_bits(s->capacity, w))
                    return bad("free bit set beyond slab capacity");
                free_count += static_cast<std::size_t>(__builtin_popcountll(s->free_bits[w]));
                if (s->free_bits[w] && w < s->hint) return bad("free slot below the bitmap hint");
            }
            if (s->live != s->capacity - free_count)
                return bad("slab live count disagrees with bitmap");
            ++slabs;
            live += s->live;
            bytes += s->live * slot_size(s->cls);
            block_bytes += block_size(hb);
            if (s->live < s->capacity) ++partial_expected;
        }
        std::size_t partial_seen = 0;
        for (std::size_t c = 0; c < kClasses; ++c) {
            const SlabHeader* prev = nullptr;
            for (const SlabHeader* s = partial_[c]; s; s = s->next) {
                if (++partial_seen > slabs) return bad("partial list longer than slabs (cycle?)");
                if (!valid_header(s) || s->cls != c)
                    return bad("partial list holds a foreign slab");
                if (s->prev != prev) return bad("partial list prev link broken");
                if (s->live == s->capacity) return bad("full slab on the partial list");
                prev = s;
            }
        }
        if (partial_seen != partial_expected)
            return bad("slab with free slots missing from partial list");
        if (slabs != stats_.slabs_in_use || block_bytes != stats_.slab_block_bytes)
            return bad("slab counters mismatch");
        if (live != stats_.live_objects || bytes != stats_.slot_bytes_in_use)
            return bad("slab live counters mismatch");
        return true;
    }

    const Stats& stats() const { return stats_; }
    bool corrupted() const { return corrupt_; }

    static constexpr std::size_t class_of(std::size_t n) { return n == 0 ? 0 : (n - 1) / kQuantum; }
    static constexpr std::size_t slot_size(std::size_t cls) { return (cls + 1) * kQuantum; }
    static constexpr std::size_t capacity_of(std::size_t cls) {
        return (kSlabUsable - kHeaderBytes) / slot_size(cls);
    }

private:
    struct SlabHeader {
        std::uint64_t magic;
        std::uint32_t cls;
        std::uint32_t capacity;
        std::uint32_t live;
        std::uint32_t hint;  // first bitmap word that may have a free slot
        SlabHeader* prev;    // partial list of the class
        SlabHeader* next;
        std::uint64_t free_bits[kBitmapWords];  // 1 = free slot
    };
    static_assert(sizeof(SlabHeader) <= kHeaderBytes, "slab header must fit before the slots");
    static constexpr std::uint64_t kMagic = 0x5ABC0DE51AB00001ull;

    static std::uint64_t valid_bits(std::size_t capacity, std::size_t w) {
        const std::size_t lo = w * 64;
        const std::size_t n = capacity > lo ? capacity - lo : 0;
        return n >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << n) - 1;
    }
    static std::uint8_t* slots(SlabHeader* s) {
        return reinterpret_cast<std::uint8_t*>(s) + kHeaderBytes;
    }
    static const std::uint8_t* slots(const SlabHeader* s) {
        return reinterpret_cast<const std::uint8_t*>(s) + kHeaderBytes;
    }
    SlabHeader* slab_of(const void* p) const {
        const auto off =
            static_cast<std::size_t>(static_cast<const std::uint8_t*>(p) - chunk_base_);
        return reinterpret_cast<SlabHeader*>(chunk_base_ + (off / kSlabBytes) * kSlabBytes);
    }
    void mark(const void* s, bool on) {
        const auto chunk =
            static_cast<std::size_t>(static_cast<const std::uint8_t*>(s) - chunk_base_) /
            kSlabBytes;
        if (on) is_slab_[chunk / 64] |= std::uint64_t{1} << (chunk % 64);
        else is_slab_[chunk / 64] &= ~(std::uint64_t{1} << (chunk % 64));
    }
    // A header can only be damaged by an overflow out of the memory before it
    // (a use-after-free write into a slot cannot reach it), but it is still
    // checked before every use, and a bad one stops the cache (ADR-012 rules).
    bool valid_header(const SlabHeader* s) const {
        if (!owns(s) || reinterpret_cast<std::uintptr_t>(s) % kSlabBytes != 0) return false;
        if (s->magic != kMagic || s->cls >= kClasses) return false;
        if (s->capacity != capacity_of(s->cls)) return false;
        return s->live <= s->capacity && s->hint < kBitmapWords;
    }

    SlabHeader* take_slab(std::size_t cls) {
        void* m = heap_->aligned_malloc(kSlabBytes, kSlabUsable);
        if (m == nullptr) return nullptr;
        auto* s = static_cast<SlabHeader*>(m);
        mark(s, true);
        s->magic = kMagic;
        s->cls = static_cast<std::uint32_t>(cls);
        s->capacity = static_cast<std::uint32_t>(capacity_of(cls));
        s->live = 0;
        s->hint = 0;
        s->prev = s->next = nullptr;
        for (std::size_t w = 0; w < kBitmapWords; ++w) s->free_bits[w] = valid_bits(s->capacity, w);
        ++stats_.slabs_in_use;
        stats_.slab_block_bytes += block_size(header_of(m));
        return s;
    }

    void release(SlabHeader* s) {
        stats_.slab_block_bytes -= block_size(header_of(s));
        --stats_.slabs_in_use;
        s->magic = 0;
        mark(s, false);
        if (heap_->free(s) != FreeStatus::Ok) fail();
    }

    void partial_push(SlabHeader* s) {
        s->prev = nullptr;
        s->next = partial_[s->cls];
        if (s->next) s->next->prev = s;
        partial_[s->cls] = s;
    }
    void partial_remove(SlabHeader* s) {
        if (s->prev) s->prev->next = s->next;
        else partial_[s->cls] = s->next;
        if (s->next) s->next->prev = s->prev;
        s->prev = s->next = nullptr;
    }
    bool fail() {
        corrupt_ = true;
        return false;
    }

    H* heap_ = nullptr;
    std::uint8_t* chunk_base_ = nullptr;  // heap arena base rounded down to 16 KiB
    std::size_t nchunks_ = 0;
    std::vector<std::uint64_t> is_slab_;  // one bit per chunk: is a slab
    SlabHeader* partial_[kClasses] = {};
    Stats stats_;
    bool corrupt_ = false;
};

}  // namespace myalloc
