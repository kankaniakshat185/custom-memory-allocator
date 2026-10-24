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
void my_free(void* p);

#ifdef __cplusplus
}
#endif
