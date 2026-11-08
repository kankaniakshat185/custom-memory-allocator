// Standard-library allocator over the my_* C API (ADR-015), so a container can
// draw from the process-wide heap without replacing global operator new:
//
//     std::vector<int, myalloc::allocator<int>> v;
//     std::map<K, V, std::less<K>, myalloc::allocator<std::pair<const K, V>>> m;
//
// Stateless: every instance uses the same heap, so all instances compare
// equal and containers can swap or move storage between them freely.
// Thread-safe, like the C API it calls (ADR-016).
#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include "myalloc/myalloc.h"

namespace myalloc {

template <class T>
class allocator {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;

    allocator() noexcept = default;
    template <class U>
    allocator(const allocator<U>&) noexcept {}

    // Over-aligned types (alignof(T) > 16) go through my_aligned_alloc.
    // Throws std::bad_array_new_length if n * sizeof(T) overflows and
    // std::bad_alloc if the arena is exhausted.
    [[nodiscard]] T* allocate(std::size_t n) {
        if (n > max_size()) throw std::bad_array_new_length();
        void* p = alignof(T) > MY_MALLOC_ALIGNMENT ? my_aligned_alloc(alignof(T), n * sizeof(T))
                                                   : my_malloc(n * sizeof(T));
        if (!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }

    void deallocate(T* p, std::size_t) noexcept { my_free(p); }

    static constexpr size_type max_size() noexcept { return SIZE_MAX / sizeof(T); }
};

template <class T, class U>
constexpr bool operator==(const allocator<T>&, const allocator<U>&) noexcept {
    return true;
}

template <class T, class U>
constexpr bool operator!=(const allocator<T>&, const allocator<U>&) noexcept {
    return false;
}

}  // namespace myalloc
