#include "arena.h"

#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>

namespace myalloc {

std::size_t page_size() {
    static const std::size_t ps = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    return ps;
}

Arena::~Arena() { destroy(); }

bool Arena::init(std::size_t bytes) {
    if (base_ != nullptr || bytes == 0) return false;
    const std::size_t ps = page_size();
    bytes = (bytes + ps - 1) & ~(ps - 1);
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) return false;
    base_ = p;
    size_ = bytes;
    return true;
}

void Arena::destroy() {
    if (base_ != nullptr) {
        munmap(base_, size_);
        base_ = nullptr;
        size_ = 0;
    }
}

bool Arena::contains(const void* p) const {
    const auto a = reinterpret_cast<std::uintptr_t>(p);
    const auto b = reinterpret_cast<std::uintptr_t>(base_);
    return base_ != nullptr && a >= b && a < b + size_;
}

}  // namespace myalloc
