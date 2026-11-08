// Benchmark-only variant of the global operator new/delete replacement, backed
// by myalloc::Allocator (heap + small-object slab, ADR-014) instead of the
// my_* C API (ADR-015). Linked only into stl_bench_slab; not a shipped target.
//
// SlabCache::init allocates its chunk bitmap with std::vector, which would
// re-enter operator new while the allocator is being constructed. Those
// allocations are sent to the C API heap and freed there (dispatch by arena),
// which is the workaround that keeps this variant bench-only (ADR-015).
#include <cstdlib>
#include <new>

#include "allocator.h"
#include "myalloc/myalloc.h"

namespace {

bool g_initialising = false;

myalloc::Allocator& alloc() {
    alignas(myalloc::Allocator) static unsigned char storage[sizeof(myalloc::Allocator)];
    static myalloc::Allocator& a = []() -> myalloc::Allocator& {
        g_initialising = true;
        auto* p = new (storage) myalloc::Allocator;
        if (!p->init(std::size_t{256} << 20)) std::abort();
        g_initialising = false;
        return *p;
    }();
    return a;
}

void* get(std::size_t n, std::size_t alignment) {
    if (g_initialising) return my_malloc(n);
    void* p = alignment > MY_MALLOC_ALIGNMENT ? alloc().heap().aligned_malloc(alignment, n)
                                              : alloc().malloc(n);
    if (!p) throw std::bad_alloc();
    return p;
}

void* get_or_null(std::size_t n, std::size_t alignment) noexcept {
    try {
        return get(n, alignment);
    } catch (...) {
        return nullptr;
    }
}

void put(void* p) {
    if (!p) return;
    if (myalloc::default_heap().arena().contains(p)) return my_free(p);
    if (alloc().free(p) != myalloc::FreeStatus::Ok) std::abort();
}

constexpr std::size_t kDefault = MY_MALLOC_ALIGNMENT;

}  // namespace

void* operator new(std::size_t n) { return get(n, kDefault); }
void* operator new[](std::size_t n) { return get(n, kDefault); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    return get_or_null(n, kDefault);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    return get_or_null(n, kDefault);
}
void* operator new(std::size_t n, std::align_val_t a) {
    return get(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a) {
    return get(n, static_cast<std::size_t>(a));
}
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return get_or_null(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return get_or_null(n, static_cast<std::size_t>(a));
}

void operator delete(void* p) noexcept { put(p); }
void operator delete[](void* p) noexcept { put(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { put(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { put(p); }
void operator delete(void* p, std::size_t) noexcept { put(p); }
void operator delete[](void* p, std::size_t) noexcept { put(p); }
void operator delete(void* p, std::align_val_t) noexcept { put(p); }
void operator delete[](void* p, std::align_val_t) noexcept { put(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { put(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { put(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { put(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { put(p); }
