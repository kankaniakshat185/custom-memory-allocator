// The my_* C API under concurrency (ADR-016): per-thread arenas, cross-thread
// frees, exhaustion fallback, fork safety. Run under ThreadSanitizer in CI
// (MYALLOC_TSAN); under the other builds they check the results.
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "heap_test_util.h"
#include "myalloc/myalloc.h"

namespace {

std::size_t live() {
    my_alloc_stats_t s;
    my_alloc_stats(&s);
    return s.live_blocks;
}

#define EXPECT_ARENAS_OK()                                                             \
    do {                                                                               \
        const char* why_ = nullptr;                                                    \
        EXPECT_TRUE(myalloc::check_arenas(&why_)) << "check: " << (why_ ? why_ : "?"); \
    } while (0)

// Seeded alloc/free/realloc mix with per-block fill patterns, verified before
// every free and realloc, as in test_stress.cpp but on the shared C API.
void churn(std::uint32_t seed, int ops, std::atomic<int>* failures) {
    std::mt19937 rng(seed);
    struct Block {
        unsigned char* p;
        std::size_t n;
        unsigned char tag;
    };
    std::vector<Block> live_blocks;
    auto intact = [](const Block& b) { return all_equal(b.p, b.n, b.tag); };
    for (int i = 0; i < ops; ++i) {
        const unsigned r = rng() % 10;
        if (r < 5 || live_blocks.empty()) {
            const std::size_t n = 1 + rng() % (rng() % 8 == 0 ? 20000 : 300);
            auto* p = static_cast<unsigned char*>(my_malloc(n));
            if (!p) {
                ++*failures;
                continue;
            }
            const auto tag = static_cast<unsigned char>(rng());
            std::memset(p, tag, n);
            live_blocks.push_back({p, n, tag});
        } else if (r < 8) {
            const std::size_t k = rng() % live_blocks.size();
            if (!intact(live_blocks[k])) ++*failures;
            my_free(live_blocks[k].p);
            live_blocks[k] = live_blocks.back();
            live_blocks.pop_back();
        } else {
            Block& b = live_blocks[rng() % live_blocks.size()];
            if (!intact(b)) ++*failures;
            const std::size_t n = 1 + rng() % 4000;
            auto* q = static_cast<unsigned char*>(my_realloc(b.p, n));
            if (!q) {
                ++*failures;
                continue;
            }
            if (n > b.n) std::memset(q + b.n, b.tag, n - b.n);
            b = {q, n, b.tag};
        }
    }
    for (const Block& b : live_blocks) {
        if (!intact(b)) ++*failures;
        my_free(b.p);
    }
}

}  // namespace

TEST(Threads, ConcurrentChurnKeepsEveryArenaConsistent) {
    const std::size_t before = live();
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (std::uint32_t t = 0; t < 8; ++t) threads.emplace_back(churn, 100 + t, 20000, &failures);
    for (std::thread& th : threads) th.join();
    EXPECT_EQ(failures.load(), 0);
    EXPECT_EQ(live(), before);
    EXPECT_GE(myalloc::arena_count(), 2);
    EXPECT_ARENAS_OK();
}

TEST(Threads, ConsecutiveThreadsGetDistinctArenas) {
    // Threads are assigned round-robin, so up to 8 consecutive new threads
    // land in 8 different arenas, whatever ran before.
    constexpr int kThreads = 4;
    std::vector<void*> ptrs(kThreads);
    for (int t = 0; t < kThreads; ++t)  // one at a time: assignment order is creation order
        std::thread([&, t] { ptrs[static_cast<std::size_t>(t)] = my_malloc(64); }).join();
    std::set<int> arenas;
    for (void* p : ptrs) {
        ASSERT_NE(p, nullptr);
        arenas.insert(myalloc::arena_index_of(p));
    }
    EXPECT_EQ(arenas.size(), static_cast<std::size_t>(kThreads));
    EXPECT_EQ(arenas.count(-1), 0u);
    for (void* p : ptrs) my_free(p);  // freed by another thread than allocated them
    EXPECT_ARENAS_OK();
}

TEST(Threads, CrossThreadFreesProducerConsumer) {
    const std::size_t before = live();
    std::mutex m;
    std::condition_variable cv;
    std::deque<unsigned char*> queue;
    bool done = false;
    std::atomic<int> corrupt{0};
    std::thread consumer([&] {
        for (;;) {
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [&] { return !queue.empty() || done; });
            if (queue.empty()) return;
            unsigned char* p = queue.front();
            queue.pop_front();
            lk.unlock();
            if (!all_equal(p, 48, 0x5A)) ++corrupt;
            my_free(p);  // memory from the producer's arena
        }
    });
    std::thread producer([&] {
        for (int i = 0; i < 50000; ++i) {
            auto* p = static_cast<unsigned char*>(my_malloc(48));
            std::memset(p, 0x5A, 48);
            {
                std::lock_guard<std::mutex> lk(m);
                queue.push_back(p);
            }
            cv.notify_one();
        }
        {
            std::lock_guard<std::mutex> lk(m);
            done = true;
        }
        cv.notify_one();
    });
    producer.join();
    consumer.join();
    EXPECT_EQ(corrupt.load(), 0);
    EXPECT_EQ(live(), before);
    EXPECT_ARENAS_OK();
}

TEST(Threads, ExhaustedArenaFallsBackToAnotherArena) {
    my_free(my_malloc(16));  // this thread takes its arena first (in a fresh process, arena 0)
    std::thread([] { my_free(my_malloc(16)); }).join();  // so this one creates a second
    ASSERT_GE(myalloc::arena_count(), 2);
    void* big = my_malloc(std::size_t{200} << 20);  // most of this thread's arena
    ASSERT_NE(big, nullptr);
    void* more = my_malloc(std::size_t{100} << 20);  // cannot fit there any more
    ASSERT_NE(more, nullptr);
    EXPECT_NE(myalloc::arena_index_of(more), myalloc::arena_index_of(big));
    my_free(more);
    my_free(big);
    EXPECT_ARENAS_OK();
}

TEST(Threads, StatsAndLeakReportCoverEveryArena) {
    const std::size_t before = live();
    void* from_thread = nullptr;
    std::thread([&] { from_thread = my_malloc(1000); }).join();
    ASSERT_NE(from_thread, nullptr);
    EXPECT_EQ(live(), before + 1);
    char buf[1 << 16];
    FILE* f = fmemopen(buf, sizeof(buf), "w");
    ASSERT_NE(f, nullptr);
    my_report_leaks(f);
    std::fclose(f);
    char addr[32];
    std::snprintf(addr, sizeof(addr), "%p", from_thread);
    EXPECT_NE(std::strstr(buf, addr), nullptr) << "block from another thread's arena not listed";
    my_free(from_thread);
    EXPECT_EQ(live(), before);
}

TEST(Threads, ForkWhileOtherThreadsAllocateDoesNotDeadlock) {
    std::atomic<bool> stop{false};
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t)
        workers.emplace_back([&] {
            while (!stop.load()) my_free(my_malloc(64));
        });
    for (int i = 0; i < 20; ++i) {
        const pid_t pid = fork();
        ASSERT_GE(pid, 0);
        if (pid == 0) {
            alarm(5);  // a deadlock kills the child instead of hanging the test
            void* p = my_malloc(128);
            my_free(p);
            _exit(p ? 0 : 1);
        }
        int status = 0;
        ASSERT_EQ(waitpid(pid, &status, 0), pid);
        EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0) << "child " << i;
    }
    stop = true;
    for (std::thread& th : workers) th.join();
    EXPECT_ARENAS_OK();
}
