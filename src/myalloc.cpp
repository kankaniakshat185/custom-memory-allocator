#include "myalloc/myalloc.h"

#include <pthread.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>

#include "heap.h"
#include "lock.h"

// Per-thread arenas (ADR-016). Each arena is a whole Heap with its own lock;
// threads are assigned arenas round-robin on first use, and up to kMaxArenas
// are created lazily. A free goes to the arena that owns the pointer, so
// cross-thread frees work and every check still runs under that arena's lock.
// MYALLOC_ARENAS=1 gives one global lock (the measured baseline).
#ifndef MYALLOC_ARENAS
#define MYALLOC_ARENAS 8
#endif

namespace myalloc {

namespace {

constexpr int kMaxArenas = MYALLOC_ARENAS;
static_assert(kMaxArenas >= 1, "at least one arena");
constexpr std::size_t kArenaBytes = std::size_t{256} << 20;

// Heaps and locks live in static storage and are never destroyed: with the
// global operator new/delete replacement (ADR-015), static destructors can
// still call operator delete after a function-local static would have been
// destroyed. The OS reclaims the arenas at exit.
//
// Each heap and each lock starts its own cache line (128 B on Apple M-series;
// two 64 B lines elsewhere). Packed, arena i's lock shared a line with the
// start of arena i+1's Heap, and app_like at 4 threads ran at 85 Mops/s
// instead of 167 (ADR-016).
constexpr std::size_t kCacheLine = 128;
struct Slot {
    alignas(kCacheLine) unsigned char heap[sizeof(Heap)];
    alignas(kCacheLine) unsigned char lock[sizeof(HeapLock)];
};
Slot g_slots[kMaxArenas];
std::atomic<Heap*> g_heaps[kMaxArenas];  // published once initialised
std::atomic<int> g_created{0};           // arenas [0, g_created) are published
std::atomic<unsigned> g_next_arena{0};   // round-robin thread assignment
std::mutex* g_create_mutex = nullptr;    // created with arena 0, never destroyed

HeapLock& lock_of(int i) { return *std::launder(reinterpret_cast<HeapLock*>(g_slots[i].lock)); }

// Registered once with arena 0, so it runs before the heaps could matter.
// Reads the environment at exit time, so the variable can be set after the
// heap was created (ADR-013).
void leak_report_at_exit() {
    const char* v = std::getenv("MYALLOC_LEAK_REPORT");
    if (v && v[0] && std::strcmp(v, "0") != 0) {
        my_alloc_stats_t s;
        my_alloc_stats(&s);
        if (s.live_blocks) my_report_leaks(stderr);
    }
}

// fork() in one thread while another holds a lock would leave the child's
// only thread waiting forever: hold every lock across fork, then release them
// in the parent and re-initialise them in the child, as system allocators do.
void before_fork() {
    g_create_mutex->lock();
    for (int i = 0; i < g_created.load(std::memory_order_acquire); ++i) lock_of(i).lock();
}
void after_fork_parent() {
    for (int i = g_created.load(std::memory_order_acquire) - 1; i >= 0; --i) lock_of(i).unlock();
    g_create_mutex->unlock();
}
void after_fork_child() {
    for (int i = 0; i < g_created.load(std::memory_order_acquire); ++i) lock_of(i).reset_in_child();
    new (g_create_mutex) std::mutex;
}

Heap* create_arena(int i) {
    auto* h = new (g_slots[i].heap) Heap;
    new (g_slots[i].lock) HeapLock;
    h->init(kArenaBytes);
    g_heaps[i].store(h, std::memory_order_release);
    return h;
}

// Arena 0 is created by the first call from any thread (thread-safe static
// initialisation), so a single-threaded program has exactly one arena and
// behaves as before ADR-016.
Heap& arena0() {
    static Heap& h = []() -> Heap& {
        alignas(std::mutex) static unsigned char m[sizeof(std::mutex)];
        g_create_mutex = new (m) std::mutex;
        Heap* p = create_arena(0);
        g_created.store(1, std::memory_order_release);
        std::atexit(leak_report_at_exit);
        pthread_atfork(before_fork, after_fork_parent, after_fork_child);
        return *p;
    }();
    return h;
}

// Initialise arena 0 before main(), while the process is single-threaded.
// Lazily, a fork() from one thread while another is inside arena0()'s one-time
// initialisation (before the fork handlers exist) left the child waiting
// forever on the static-initialisation guard: the thread holding it does not
// exist in the child. Measured on Threads.ForkWhileOtherThreadsAllocate-
// DoesNotDeadlock in fresh processes: 96/100 hung lazily, 0/100 eagerly
// (ADR-016). Static initialisers that allocate before this one still work:
// they initialise it lazily.
[[maybe_unused]] const bool g_arena0_ready = (arena0(), true);

Heap& arena(int i) { return *g_heaps[i].load(std::memory_order_acquire); }

thread_local int t_arena = -1;  // this thread's arena; -1 until its first allocation

// This thread's arena index, assigned on first use.
int my_arena() {
    int& index = t_arena;
    if (index >= 0) return index;
    arena0();
    index = static_cast<int>(g_next_arena.fetch_add(1, std::memory_order_relaxed) %
                             static_cast<unsigned>(kMaxArenas));
    if (index >= g_created.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> g(*g_create_mutex);
        // Arenas are created in order, so every index below this one exists.
        for (int i = g_created.load(std::memory_order_relaxed); i <= index; ++i) create_arena(i);
        if (g_created.load(std::memory_order_relaxed) <= index)
            g_created.store(index + 1, std::memory_order_release);
    }
    return index;
}

// The arena whose memory contains p, or -1. Most frees are of memory the same
// thread allocated, so its own arena is checked first (it never assigns one:
// a thread that only frees gets no arena).
int owner_of(const void* p) {
    const int mine = t_arena;
    if (mine >= 0 && arena(mine).arena().contains(p)) return mine;
    const int n = g_created.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        if (arena(i).arena().contains(p)) return i;
    return -1;
}

using Guard = std::lock_guard<HeapLock>;

}  // namespace

Heap& default_heap() { return arena0(); }

int arena_count() {
    arena0();
    return g_created.load(std::memory_order_acquire);
}

int arena_index_of(const void* p) {
    arena0();
    return owner_of(p);
}

bool check_arenas(const char** why) {
    for (int i = 0; i < arena_count(); ++i) {
        Guard g(lock_of(i));
        if (!arena(i).check(why)) return false;
    }
    return true;
}

}  // namespace myalloc

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

// Runs alloc(heap) on this thread's arena; if that arena is exhausted, tries
// the other arenas that already exist. A NULL caused by detected corruption
// aborts instead of looking like OOM.
template <class F>
void* allocate(F alloc, const char* who) {
    const int mine = myalloc::my_arena();
    const int n = myalloc::g_created.load(std::memory_order_acquire);
    for (int k = 0; k < n; ++k) {
        const int i = (mine + k) % n;
        myalloc::Heap& h = myalloc::arena(i);
        myalloc::Guard g(myalloc::lock_of(i));
        if (void* p = alloc(h)) return p;
        if (h.corrupted()) die_if_bad(myalloc::FreeStatus::HeapCorrupted, who);
    }
    return nullptr;
}

}  // namespace

extern "C" {

void* my_malloc(size_t n) {
    return allocate([n](myalloc::Heap& h) { return h.malloc(n); }, "my_malloc");
}

void* my_aligned_alloc(size_t alignment, size_t n) {
    return allocate([=](myalloc::Heap& h) { return h.aligned_malloc(alignment, n); },
                    "my_aligned_alloc");
}

void* my_calloc(size_t count, size_t size) {
    return allocate([=](myalloc::Heap& h) { return h.calloc(count, size); }, "my_calloc");
}

void my_free(void* p) {
    if (!p) return;
    myalloc::arena0();
    const int i = myalloc::owner_of(p);
    if (i < 0) die_if_bad(myalloc::FreeStatus::InvalidPointer, "my_free");
    myalloc::Guard g(myalloc::lock_of(i));
    die_if_bad(myalloc::arena(i).free(p), "my_free");
}

// Resizes within the owning arena; if that arena is exhausted it returns NULL
// like any failed realloc, even if another arena has room.
void* my_realloc(void* p, size_t n) {
    if (!p) return my_malloc(n);
    myalloc::arena0();
    const int i = myalloc::owner_of(p);
    if (i < 0) die_if_bad(myalloc::FreeStatus::InvalidPointer, "my_realloc");
    myalloc::Guard g(myalloc::lock_of(i));
    myalloc::FreeStatus st;
    void* q = myalloc::arena(i).realloc(p, n, &st);
    die_if_bad(st, "my_realloc");
    if (!q && myalloc::arena(i).corrupted())
        die_if_bad(myalloc::FreeStatus::HeapCorrupted, "my_realloc");
    return q;
}

// Sums over the arenas, each read under its own lock: with threads running,
// the totals are not one atomic snapshot. peak_bytes_in_use is the sum of
// per-arena peaks, an upper bound on the true peak. With one arena (any
// single-threaded program) every field is exact.
void my_alloc_stats(my_alloc_stats_t* out) {
    myalloc::arena0();
    *out = my_alloc_stats_t{};
    const int n = myalloc::g_created.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        const myalloc::Heap& h = myalloc::arena(i);
        myalloc::Guard g(myalloc::lock_of(i));
        const myalloc::HeapStats& st = h.stats();
        // Arena minus the 8-byte leading pad and the 8-byte epilogue.
        const size_t arena = h.arena().size() >= 16 ? h.arena().size() - 16 : 0;
        out->arena_bytes += arena;
        out->bytes_in_use += st.bytes_in_use;
        out->payload_in_use += st.bytes_in_use - st.live_blocks * myalloc::kHeaderSize;
        out->peak_bytes_in_use += st.peak_in_use;
        out->live_blocks += st.live_blocks;
        out->free_bytes += arena - st.bytes_in_use;
        out->free_blocks += st.free_blocks;
        out->high_water += st.high_water;
        if (h.corrupted()) out->corrupted = 1;
    }
}

size_t my_report_leaks(FILE* out) {
    myalloc::arena0();
    size_t n = 0, bytes = 0;
    bool complete = true, corrupted = false;
    std::fprintf(out, "myalloc leak report: live blocks in address order\n");
    const int arenas = myalloc::g_created.load(std::memory_order_acquire);
    for (int i = 0; i < arenas; ++i) {
        const myalloc::Heap& h = myalloc::arena(i);
        myalloc::Guard g(myalloc::lock_of(i));
        complete &= h.for_each_live([&](void* p, std::size_t usable) {
            std::fprintf(out, "  %p  %zu bytes\n", p, usable);
            ++n;
            bytes += usable;
        });
        corrupted |= h.corrupted();
    }
    if (!complete)
        std::fprintf(out, "  (walk stopped at a damaged block header; list incomplete)\n");
    std::fprintf(out, "myalloc leak report: %zu live block%s, %zu usable bytes%s\n", n,
                 n == 1 ? "" : "s", bytes, corrupted ? " (heap marked corrupted)" : "");
    return n;
}

}  // extern "C"
