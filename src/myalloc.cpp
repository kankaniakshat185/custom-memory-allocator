#include "myalloc/myalloc.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "heap.h"

namespace myalloc {

namespace {

// Registered once with the default heap, so it runs before the heap is
// destroyed. Reads the environment at exit time, so the variable can be set
// after the heap was created (ADR-013).
void leak_report_at_exit() {
    const char* v = std::getenv("MYALLOC_LEAK_REPORT");
    if (v && v[0] && std::strcmp(v, "0") != 0) {
        my_alloc_stats_t s;
        my_alloc_stats(&s);
        if (s.live_blocks) my_report_leaks(stderr);
    }
}

}  // namespace

Heap& default_heap() {
    static Heap heap;
    static const bool ok = [] {
        const bool initialised = heap.init(std::size_t{256} << 20);
        std::atexit(leak_report_at_exit);
        return initialised;
    }();
    (void)ok;
    return heap;
}

}  // namespace myalloc

extern "C" {

namespace {

void die_if_bad(myalloc::FreeStatus st, const char* who) {
    switch (st) {
        case myalloc::FreeStatus::Ok:
            return;
        case myalloc::FreeStatus::DoubleFree:
            std::fprintf(stderr, "%s: double free detected\n", who);
            break;
        case myalloc::FreeStatus::InvalidPointer:
            std::fprintf(stderr, "%s: invalid pointer\n", who);
            break;
        case myalloc::FreeStatus::HeapCorrupted:
            std::fprintf(stderr, "%s: heap corruption detected (free-list metadata overwritten)\n",
                         who);
            break;
    }
    std::abort();
}

// A NULL caused by detected corruption aborts instead of looking like OOM.
void* checked(void* p, const char* who) {
    if (!p && myalloc::default_heap().corrupted())
        die_if_bad(myalloc::FreeStatus::HeapCorrupted, who);
    return p;
}

}  // namespace

void* my_malloc(size_t n) { return checked(myalloc::default_heap().malloc(n), "my_malloc"); }

void my_free(void* p) { die_if_bad(myalloc::default_heap().free(p), "my_free"); }

void* my_realloc(void* p, size_t n) {
    myalloc::FreeStatus st;
    void* q = myalloc::default_heap().realloc(p, n, &st);
    die_if_bad(st, "my_realloc");
    return q;
}

void* my_calloc(size_t count, size_t size) {
    return checked(myalloc::default_heap().calloc(count, size), "my_calloc");
}

void my_alloc_stats(my_alloc_stats_t* out) {
    const myalloc::Heap& h = myalloc::default_heap();
    const myalloc::HeapStats& st = h.stats();
    // Arena minus the 8-byte leading pad and the 8-byte epilogue.
    const size_t arena = h.arena().size() >= 16 ? h.arena().size() - 16 : 0;
    out->arena_bytes = arena;
    out->bytes_in_use = st.bytes_in_use;
    out->payload_in_use = st.bytes_in_use - st.live_blocks * myalloc::kHeaderSize;
    out->peak_bytes_in_use = st.peak_in_use;
    out->live_blocks = st.live_blocks;
    out->free_bytes = arena - st.bytes_in_use;
    out->free_blocks = st.free_blocks;
    out->high_water = st.high_water;
    out->corrupted = h.corrupted() ? 1 : 0;
}

size_t my_report_leaks(FILE* out) {
    const myalloc::Heap& h = myalloc::default_heap();
    size_t n = 0, bytes = 0;
    std::fprintf(out, "myalloc leak report: live blocks in address order\n");
    const bool complete = h.for_each_live([&](void* p, std::size_t usable) {
        std::fprintf(out, "  %p  %zu bytes\n", p, usable);
        ++n;
        bytes += usable;
    });
    if (!complete)
        std::fprintf(out, "  (walk stopped at a damaged block header; list incomplete)\n");
    std::fprintf(out, "myalloc leak report: %zu live block%s, %zu usable bytes%s\n", n,
                 n == 1 ? "" : "s", bytes, h.corrupted() ? " (heap marked corrupted)" : "");
    return n;
}

}  // extern "C"
