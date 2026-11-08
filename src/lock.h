// The lock that makes the my_* C API thread-safe (ADR-016). BasicHeap itself
// stays single-threaded: it is the building block, and callers that own a
// heap per thread pay nothing.
//
// MYALLOC_LOCK selects the implementation, for measurement:
//   0 = std::mutex, 1 = os_unfair_lock (macOS; std::mutex elsewhere),
//   2 = test-and-test-and-set spinlock with yield,
//  -1 = no lock (single-threaded overhead baseline only; not thread-safe).
#pragma once
#include <atomic>
#include <mutex>
#include <new>
#include <thread>

#if defined(__APPLE__)
#include <os/lock.h>
#endif

#ifndef MYALLOC_LOCK
#define MYALLOC_LOCK 1
#endif

namespace myalloc {

#if MYALLOC_LOCK < 0

class HeapLock {
public:
    void lock() {}
    void unlock() {}
    void reset_in_child() {}
};

#elif MYALLOC_LOCK == 1 && defined(__APPLE__)

// What macOS's own malloc uses: no fairness, no spinning in user space, and
// the kernel boosts the owner's priority (no inversion with E-core threads).
class HeapLock {
public:
    void lock() { os_unfair_lock_lock(&lock_); }
    void unlock() { os_unfair_lock_unlock(&lock_); }
    // After fork() the child has one thread, but the lock records the
    // parent's owner: re-initialise instead of unlocking.
    void reset_in_child() { lock_ = OS_UNFAIR_LOCK_INIT; }

private:
    os_unfair_lock lock_ = OS_UNFAIR_LOCK_INIT;
};

#elif MYALLOC_LOCK == 2

class HeapLock {
public:
    void lock() {
        for (int spins = 0;; ++spins) {
            if (!locked_.exchange(true, std::memory_order_acquire)) return;
            while (locked_.load(std::memory_order_relaxed))
                if (++spins > 64) std::this_thread::yield();
        }
    }
    void unlock() { locked_.store(false, std::memory_order_release); }
    void reset_in_child() { locked_.store(false, std::memory_order_relaxed); }

private:
    std::atomic<bool> locked_{false};
};

#else

class HeapLock {
public:
    void lock() { mutex_.lock(); }
    void unlock() { mutex_.unlock(); }
    // A std::mutex locked by the parent cannot be unlocked by the child's
    // thread portably; construct a fresh one in place.
    void reset_in_child() { new (&mutex_) std::mutex; }

private:
    std::mutex mutex_;
};

#endif

}  // namespace myalloc
