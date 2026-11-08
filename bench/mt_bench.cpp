// Multithreaded throughput and scaling (ADR-016). N threads each replay their
// own seeded trace (same distribution, seed = base + thread index) against
// one allocator; all threads start together at a barrier and the wall time
// runs until the last one finishes. The threads live across all reps of a
// configuration: starting fresh threads per rep gave each one a new,
// never-touched arena (round-robin assignment), so the per-thread-arena
// variant paid page faults the others did not (ADR-016).
//
//   mt_bench [--alloc system,myalloc] [--threads 1,2,4,8] [--trace app_like]
//            [--reps 7]
//
// Output CSV: allocator,trace,threads,reps,median_mops,iqr_pct,
// median_ns_per_op_per_thread. Throughput is total operations (all threads)
// per second of wall time. The M2 has 4 performance and 4 efficiency cores,
// so scaling past 4 threads mixes core types (docs/environment.md).
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "harness.h"
#include "myalloc/myalloc.h"
#include "workloads.h"

namespace {

struct SystemApi {
    static void* alloc(std::size_t n) { return std::malloc(n); }
    static void release(void* p) { std::free(p); }
    static void* resize(void* p, std::size_t n) { return std::realloc(p, n); }
};

struct MyallocApi {
    static void* alloc(std::size_t n) { return my_malloc(n); }
    static void release(void* p) { my_free(p); }
    static void* resize(void* p, std::size_t n) { return my_realloc(p, n); }
};

template <class Api>
void replay(const bench::Trace& t) {
    std::vector<void*> slot(t.slots, nullptr);
    for (const bench::Op& op : t.ops) {
        switch (op.kind) {
            case bench::OpKind::Alloc: {
                void* p = Api::alloc(op.size);
                if (!p) {
                    std::fprintf(stderr, "mt_bench: allocation of %u bytes failed\n", op.size);
                    std::abort();
                }
                *static_cast<volatile char*>(p) = 1;  // touch it, as a program would
                slot[op.slot] = p;
                break;
            }
            case bench::OpKind::Free:
                Api::release(slot[op.slot]);
                slot[op.slot] = nullptr;
                break;
            case bench::OpKind::Realloc:
                slot[op.slot] = Api::resize(slot[op.slot], op.size);
                break;
        }
    }
}

// Runs the traces `runs` times on the same N threads (kept alive, so each
// keeps its arena and per-thread allocator state across runs, as a long-lived
// thread would). Returns wall nanoseconds per run, from releasing the threads
// to the last one finishing. Run 0 is the warm-up.
template <class Api>
std::vector<double> run_all(const std::vector<bench::Trace>& traces, int runs) {
    const int n = static_cast<int>(traces.size());
    std::atomic<int> start{0};  // run number the threads may start (1-based)
    // Replays finished, over all runs. The main thread sleeps on the condition
    // variable instead of spinning, so its waiting adds no instructions to
    // /usr/bin/time's count (run_mt.sh); the workers spin only between runs.
    int done = 0;
    std::mutex m;
    std::condition_variable cv;
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        threads.emplace_back([&, i] {
            for (int r = 1; r <= runs; ++r) {
                while (start.load(std::memory_order_acquire) < r) std::this_thread::yield();
                replay<Api>(traces[static_cast<std::size_t>(i)]);
                {
                    std::lock_guard<std::mutex> lk(m);
                    ++done;
                }
                cv.notify_one();
            }
        });
    std::vector<double> ns;
    for (int r = 1; r <= runs; ++r) {
        const std::uint64_t t0 = bench::now_ns();
        start.store(r, std::memory_order_release);
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return done >= n * r; });
        ns.push_back(static_cast<double>(bench::now_ns() - t0));
    }
    for (std::thread& th : threads) th.join();
    return ns;
}

std::vector<std::string> split(const char* s) {
    std::vector<std::string> out;
    std::string cur;
    for (; *s; ++s) {
        if (*s == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += *s;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bench::Trace make_trace(const std::string& name, std::uint32_t seed) {
    if (name == "random") return bench::random_sizes(seed);
    if (name == "app_like") return bench::app_like(seed);
    std::fprintf(stderr, "mt_bench: unknown trace %s (app_like, random)\n", name.c_str());
    std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> allocs{"system", "myalloc"};
    std::vector<std::string> thread_counts{"1", "2", "4", "8"};
    std::string trace_name = "app_like";
    int reps = 7;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--alloc")) allocs = split(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--threads")) thread_counts = split(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--trace")) trace_name = argv[i + 1];
        else if (!std::strcmp(argv[i], "--reps")) reps = std::atoi(argv[i + 1]);
    }

    int max_threads = 0;
    for (const std::string& c : thread_counts) max_threads = std::max(max_threads, std::stoi(c));
    std::vector<bench::Trace> all;  // generated once, before any timing
    for (int i = 0; i < max_threads; ++i)
        all.push_back(make_trace(trace_name, 1000u + static_cast<std::uint32_t>(i)));

    std::printf("allocator,trace,threads,reps,median_mops,iqr_pct,median_ns_per_op_per_thread\n");
    for (const std::string& a : allocs) {
        for (const std::string& c : thread_counts) {
            const int n = std::stoi(c);
            const std::vector<bench::Trace> traces(all.begin(), all.begin() + n);
            std::size_t ops = 0;
            for (const bench::Trace& t : traces) ops += t.ops.size();
            std::vector<double> runs;
            if (a == "system") runs = run_all<SystemApi>(traces, reps + 1);
            else if (a == "myalloc") runs = run_all<MyallocApi>(traces, reps + 1);
            else {
                std::fprintf(stderr, "mt_bench: unknown allocator %s\n", a.c_str());
                return 2;
            }
            std::vector<double> mops, per_thread_ns;
            for (std::size_t r = 1; r < runs.size(); ++r) {  // runs[0] is the warm-up
                mops.push_back(static_cast<double>(ops) / runs[r] * 1e3);
                per_thread_ns.push_back(runs[r] / (static_cast<double>(ops) / n));
            }
            const double med = bench::percentile(mops, 0.5);
            const double iqr = bench::percentile(mops, 0.75) - bench::percentile(mops, 0.25);
            std::printf("%s,%s,%d,%d,%.2f,%.1f,%.2f\n", a.c_str(), trace_name.c_str(), n, reps, med,
                        100.0 * iqr / med, bench::percentile(per_thread_ns, 0.5));
            std::fflush(stdout);
        }
    }
    return 0;
}
