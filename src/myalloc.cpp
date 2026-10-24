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

void* my_malloc(size_t n) { return myalloc::default_heap().malloc(n); }

void my_free(void* p) {
    switch (myalloc::default_heap().free(p)) {
        case myalloc::FreeStatus::Ok: return;
        case myalloc::FreeStatus::DoubleFree:
            std::fputs("my_free: double free detected\n", stderr);
            break;
        case myalloc::FreeStatus::InvalidPointer:
            std::fputs("my_free: invalid pointer\n", stderr);
            break;
    }
    std::abort();
}

}  // extern "C"
