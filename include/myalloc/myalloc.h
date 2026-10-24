#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif
