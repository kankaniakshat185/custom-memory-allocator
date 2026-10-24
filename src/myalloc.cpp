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
    }
    std::abort();
}

}  // namespace

void* my_malloc(size_t n) { return myalloc::default_heap().malloc(n); }

void my_free(void* p) { die_if_bad(myalloc::default_heap().free(p), "my_free"); }

void* my_realloc(void* p, size_t n) {
    myalloc::FreeStatus st;
    void* q = myalloc::default_heap().realloc(p, n, &st);
    die_if_bad(st, "my_realloc");
    return q;
}

void* my_calloc(size_t count, size_t size) { return myalloc::default_heap().calloc(count, size); }

}  // extern "C"
