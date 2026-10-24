#pragma once
#include <cstddef>

namespace myalloc {

// One fixed anonymous mmap reservation. Pages are committed lazily on first
// touch; the arena never grows (docs/plan.md §1).
class Arena {
public:
    Arena() = default;
    ~Arena();
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    // Reserve `bytes` (rounded up to a page multiple). Returns false on failure.
    bool init(std::size_t bytes);
    void destroy();

    void* base() const { return base_; }
    std::size_t size() const { return size_; }
    bool contains(const void* p) const;

private:
    void* base_ = nullptr;
    std::size_t size_ = 0;
};

std::size_t page_size();

}  // namespace myalloc
