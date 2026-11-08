// Global operator new/delete replacement over the my_* C API (ADR-015).
// Opt-in: linking the myalloc_new library replaces every replaceable form in
// the program (C++17: plain, array, nothrow, align_val_t, sized delete).
//
// Not thread-safe, like the C API: only for single-threaded programs until
// thread safety lands (Phase 16).
#include <cstddef>
#include <new>

#include "myalloc/myalloc.h"

namespace {

void* aligned_or_plain(std::size_t n, std::size_t alignment) {
    return alignment > MY_MALLOC_ALIGNMENT ? my_aligned_alloc(alignment, n) : my_malloc(n);
}

// [new.delete.single]: on failure call the installed new_handler and retry;
// throw std::bad_alloc if there is none.
void* allocate_or_throw(std::size_t n, std::size_t alignment) {
    for (;;) {
        if (void* p = aligned_or_plain(n, alignment)) return p;
        std::new_handler handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc();
        handler();
    }
}

// The nothrow forms behave as if they called the throwing form and caught
// std::bad_alloc, so they run the new_handler too.
void* allocate_or_null(std::size_t n, std::size_t alignment) noexcept {
    try {
        return allocate_or_throw(n, alignment);
    } catch (...) {
        return nullptr;
    }
}

constexpr std::size_t kDefault = MY_MALLOC_ALIGNMENT;

}  // namespace

// Every form frees through my_free, which validates the pointer and aborts on
// a double free or a pointer that never came from the heap. The sized forms
// ignore the size: the block header already records it.
void* operator new(std::size_t n) { return allocate_or_throw(n, kDefault); }
void* operator new[](std::size_t n) { return allocate_or_throw(n, kDefault); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    return allocate_or_null(n, kDefault);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    return allocate_or_null(n, kDefault);
}

void* operator new(std::size_t n, std::align_val_t a) {
    return allocate_or_throw(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a) {
    return allocate_or_throw(n, static_cast<std::size_t>(a));
}
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return allocate_or_null(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return allocate_or_null(n, static_cast<std::size_t>(a));
}

void operator delete(void* p) noexcept { my_free(p); }
void operator delete[](void* p) noexcept { my_free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { my_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { my_free(p); }
void operator delete(void* p, std::size_t) noexcept { my_free(p); }
void operator delete[](void* p, std::size_t) noexcept { my_free(p); }

void operator delete(void* p, std::align_val_t) noexcept { my_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { my_free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { my_free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { my_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { my_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { my_free(p); }
