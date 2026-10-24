#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bench {

enum class OpKind : std::uint8_t { Alloc, Free, Realloc };

// One allocator operation. `slot` indexes the replay's pointer table, so the
// same trace drives every allocator identically.
struct Op {
    OpKind kind;
    std::uint32_t slot;
    std::uint32_t size;      // Alloc/Realloc: requested bytes
    std::uint32_t old_size;  // Realloc: previous requested bytes (for naive copy)
};

struct Trace {
    std::string name;
    std::size_t slots = 0;
    std::vector<Op> ops;
    bool uses_realloc = false;
};

// Pre-generated, seeded traces (ADR-008). Every trace frees all its blocks at
// the end, so a coalescing heap returns to one free block.
Trace random_sizes();      // log-uniform 16 B..64 KiB, ~2000 live
Trace app_like();          // 95% 16..256 B, 5% 4..256 KiB, ~3000 live
Trace adversarial();       // non-fitting holes in front of exact-fit blocks (first-fit worst case)
Trace realloc_growth();    // doubling buffers + small noise (same shape as footprint_probe)

std::vector<Trace> all_traces();

}  // namespace bench
