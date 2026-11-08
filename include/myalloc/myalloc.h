#pragma once
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Every function is thread-safe (ADR-016). Each thread allocates from its
// own arena (up to 8, assigned round-robin; arena 0 is created before main),
// and a block can be freed or reallocated from any thread. fork() is safe:
// the arena locks are held across it.

// Alignment of every pointer my_malloc returns.
#define MY_MALLOC_ALIGNMENT 16

// Returns a 16-byte aligned block of at least n bytes, or NULL if the arena is
// exhausted. my_malloc(0) returns a valid, unique, freeable pointer.
void* my_malloc(size_t n);

// Frees a pointer from my_malloc. NULL is a no-op. A pointer outside the arena
// or an already-freed block aborts with a diagnostic (like glibc).
//
// All functions abort with a diagnostic if they detect that free-list metadata
// was overwritten (e.g. by a write through a dangling pointer), instead of
// following corrupted links (ADR-012).
void my_free(void* p);

// Resizes p to at least n bytes, preserving the first min(old, n) bytes.
// realloc(NULL, n) == my_malloc(n). realloc(p, 0) shrinks p to the minimum
// block and returns it (never NULL, never frees). Returns NULL if the arena
// is exhausted, leaving p valid and unchanged. Invalid p aborts like my_free.
void* my_realloc(void* p, size_t n);

// Zeroed array of count * size bytes; NULL if the product overflows or the
// arena is exhausted.
void* my_calloc(size_t count, size_t size);

// A block of at least n bytes whose address is a multiple of `alignment`, or
// NULL if alignment is not a power of two or the arena is exhausted. Freed
// with my_free. Alignments <= MY_MALLOC_ALIGNMENT behave like my_malloc
// (ADR-014, ADR-015).
void* my_aligned_alloc(size_t alignment, size_t n);

// ---- Allocation tracking (ADR-013) ----------------------------------------
// Sizes are block-based: the allocator does not store requested sizes, so
// "payload" bytes are what callers can use (>= what they asked for).
typedef struct my_alloc_stats_t {
    size_t arena_bytes;        // bytes the heap manages (arena minus sentinels)
    size_t bytes_in_use;       // allocated blocks, including their 8-byte headers
    size_t payload_in_use;     // usable bytes in allocated blocks
    size_t peak_bytes_in_use;  // high-water of bytes_in_use
    size_t live_blocks;        // allocations not yet freed
    size_t free_bytes;         // arena_bytes - bytes_in_use
    size_t free_blocks;        // separate free blocks (fragmentation indicator)
    size_t high_water;         // highest arena offset ever handed out
    int corrupted;             // nonzero once metadata corruption was detected
} my_alloc_stats_t;

// Fills *out in O(1) from counters the heap maintains anyway.
void my_alloc_stats(my_alloc_stats_t* out);

// Writes every live block (address, usable size) in address order plus
// totals to out, and returns the number of live blocks. O(blocks).
// Setting MYALLOC_LEAK_REPORT=1 in the environment prints this report to
// stderr at process exit when blocks are still live.
size_t my_report_leaks(FILE* out);

#ifdef __cplusplus
}
#endif
