#include "myalloc/myalloc.h"

#include <cstdio>
#include <cstdlib>

#include "heap.h"

namespace myalloc {

Heap& default_heap() {
    static Heap heap;
    static const bool ok = heap.init(std::size_t{256} << 20);
    (void)ok;
    return heap;
}

}  // namespace myalloc

extern "C" {

namespace {

void die_if_bad(myalloc::FreeStatus st, const char* who) {
    switch (st) {
        case myalloc::FreeStatus::Ok: return;
        case myalloc::FreeStatus::DoubleFree:
            std::fprintf(stderr, "%s: double free detected\n", who);
            break;
        case myalloc::FreeStatus::InvalidPointer:
            std::fprintf(stderr, "%s: invalid pointer\n", who);
            break;
        case myalloc::FreeStatus::HeapCorrupted:
            std::fprintf(stderr, "%s: heap corruption detected (free-list metadata overwritten)\n", who);
            break;
    }
    std::abort();
}

// A NULL caused by detected corruption aborts instead of looking like OOM.
void* checked(void* p, const char* who) {
    if (!p && myalloc::default_heap().corrupted()) die_if_bad(myalloc::FreeStatus::HeapCorrupted, who);
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

}  // extern "C"
