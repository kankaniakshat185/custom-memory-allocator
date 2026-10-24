#pragma once
#include <cstddef>
#include <cstdint>

// Block layout (ADR-002). All sizes are multiples of 16, so the low 4 bits of
// the size word are free for flags.
//
//   allocated:  [ header 8B ][ payload ............................ ]
//   free:       [ header 8B ][ next 8B ][ prev 8B ][ ... ][ footer 8B ]
//
// Block starts are 8 mod 16, so payloads (header + 8) are 16-byte aligned.
namespace myalloc {

constexpr std::size_t kAlign = 16;
constexpr std::size_t kHeaderSize = 8;
constexpr std::size_t kMinBlock = 32;  // header + next + prev + footer

constexpr std::uint64_t kAllocBit = 0x1;      // this block is allocated
constexpr std::uint64_t kPrevAllocBit = 0x2;  // previous block is allocated (ADR-006)
constexpr std::uint64_t kFlagMask = 0xF;

struct Header {
    std::uint64_t word;
};

struct FreeLinks {  // lives in the payload of a free block
    Header* next;
    Header* prev;
};

constexpr std::size_t align_up(std::size_t n, std::size_t a) { return (n + a - 1) & ~(a - 1); }

inline std::size_t block_size(const Header* h) { return h->word & ~kFlagMask; }
inline bool is_alloc(const Header* h) { return (h->word & kAllocBit) != 0; }
inline bool prev_alloc(const Header* h) { return (h->word & kPrevAllocBit) != 0; }

inline void* payload(Header* h) { return reinterpret_cast<std::uint8_t*>(h) + kHeaderSize; }
inline Header* header_of(void* p) {
    return reinterpret_cast<Header*>(static_cast<std::uint8_t*>(p) - kHeaderSize);
}
inline FreeLinks* links(Header* h) { return reinterpret_cast<FreeLinks*>(payload(h)); }
inline Header* footer(Header* h) {
    return reinterpret_cast<Header*>(reinterpret_cast<std::uint8_t*>(h) + block_size(h) -
                                     kHeaderSize);
}
inline const Header* footer(const Header* h) {
    return reinterpret_cast<const Header*>(reinterpret_cast<const std::uint8_t*>(h) +
                                           block_size(h) - kHeaderSize);
}
inline Header* next_block(Header* h) {
    return reinterpret_cast<Header*>(reinterpret_cast<std::uint8_t*>(h) + block_size(h));
}
// Footers hold the size only; flags live in the header.
inline void write_footer(Header* h) { footer(h)->word = block_size(h); }
// Only valid when !prev_alloc(h): the previous block is free, so it has a footer.
inline Header* prev_block(Header* h) {
    const std::size_t prev_size = (h - 1)->word;
    return reinterpret_cast<Header*>(reinterpret_cast<std::uint8_t*>(h) - prev_size);
}

// Block size needed to serve a request of n payload bytes.
inline std::size_t block_size_for(std::size_t n) {
    std::size_t s = align_up(n + kHeaderSize, kAlign);
    return s < kMinBlock ? kMinBlock : s;
}

}  // namespace myalloc
