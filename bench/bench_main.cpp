// Wall-clock benchmark harness (ADR-008).
//
// Replays pre-generated traces against each allocator and reports ns/op over
// whole runs (min/median/max across reps) and over 1024-op batches (p50/p99).
// Built against the current tree by CMake, and against older phase commits by
// bench/scripts/run_phases.sh, so it only relies on the Heap API every phase
// has (init/malloc/free/stats/arena); realloc is detected at compile time.
//
// Usage: bench_timing [--label L] [--reps N] [--alloc a,b] [--workload w,x] [--raw FILE]
//   allocators: system, heap, heap_naive_realloc, heap_growonly_realloc
//   MallocNanoZone=0 in the environment relabels "system" as "system_nano_off".
#include <sys/sysctl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "harness.h"
#include "heap.h"
#include "workloads.h"

namespace {

using bench::Op;
using bench::OpKind;
using bench::Trace;

constexpr std::size_t kArena = std::size_t{256} << 20;
constexpr std::size_t kBatch = 1024;

template <class H, class = void>
struct has_realloc : std::false_type {};
template <class H>
struct has_realloc<H, std::void_t<decltype(std::declval<H&>().realloc(nullptr, 0))>>
    : std::true_type {};
constexpr bool kHeapHasRealloc = has_realloc<myalloc::Heap>::value;

enum class ReallocMode { Native, Naive, GrowOnly };

struct SystemAlloc {
    void* malloc(std::size_t n) { return std::malloc(n); }
    void free(void* p) { std::free(p); }
    void* realloc(void* p, std::size_t n, std::size_t) { return std::realloc(p, n); }
};

// H is a template parameter only so that h.realloc is a dependent call and the
// discarded if-constexpr branch compiles against phases without Heap::realloc.
template <ReallocMode Mode, class H = myalloc::Heap>
struct HeapAlloc {
    H& h;
    void* malloc(std::size_t n) { return h.malloc(n); }
    void free(void* p) { h.free(p); }
    void* move(void* p, std::size_t n, std::size_t old) {
        void* q = h.malloc(n);
        if (q && p) {
            std::memcpy(q, p, old < n ? old : n);
            h.free(p);
        }
        return q;
    }
    void* realloc(void* p, std::size_t n, std::size_t old) {
        if constexpr (Mode == ReallocMode::Naive || !has_realloc<H>::value) {
            return move(p, n, old);
        } else {
            if (Mode == ReallocMode::GrowOnly && n < old) return move(p, n, old);
            return h.realloc(p, n);
        }
    }
};

struct RunResult {
    double ns_per_op = 0;
    std::vector<double> batches;  // ns/op per batch
    std::size_t failures = 0;
};

template <class A>
RunResult replay(A& a, const Trace& t) {
    std::vector<void*> slot(t.slots, nullptr);
    RunResult r;
    r.batches.reserve(t.ops.size() / kBatch + 1);
    std::uint64_t total = 0;
    const std::size_t n = t.ops.size();
    for (std::size_t i = 0; i < n;) {
        const std::size_t end = i + kBatch < n ? i + kBatch : n;
        const std::size_t count = end - i;
        const std::uint64_t t0 = bench::now_ns();
        for (; i < end; ++i) {
            const Op& op = t.ops[i];
            switch (op.kind) {
                case OpKind::Alloc: {
                    void* p = a.malloc(op.size);
                    if (p) static_cast<std::uint64_t*>(p)[0] = op.size;  // touch, like a real caller
                    else ++r.failures;
                    slot[op.slot] = p;
                    break;
                }
                case OpKind::Free:
                    a.free(slot[op.slot]);
                    slot[op.slot] = nullptr;
                    break;
                case OpKind::Realloc: {
                    void* q = a.realloc(slot[op.slot], op.size, op.old_size);
                    if (q) {
                        static_cast<std::uint64_t*>(q)[0] = op.size;
                        slot[op.slot] = q;
                    } else {
                        ++r.failures;  // original stays valid and is freed later
                    }
                    break;
                }
            }
        }
        const std::uint64_t dt = bench::now_ns() - t0;
        total += dt;
        r.batches.push_back(static_cast<double>(dt) / static_cast<double>(count));
    }
    r.ns_per_op = static_cast<double>(total) / static_cast<double>(n);
    return r;
}

// Write-fault every page a run will use, so a fresh arena does not pay
// first-touch faults that the warm system allocator does not (ADR-008).
// Offset 64 in each page is zero payload in a fresh heap in every phase, so
// writing 0 there changes no allocator state.
void prefault(myalloc::Heap& h, std::size_t bytes) {
    auto* base = static_cast<volatile std::uint8_t*>(const_cast<void*>(h.arena().base()));
    const std::size_t ps = myalloc::page_size();
    for (std::size_t off = 0; off < bytes && off + 64 < h.arena().size(); off += ps) base[off + 64] = 0;
}

struct Options {
    std::string label = "current";
    int reps = 15;
    std::vector<std::string> allocs{"system", "heap"};
    std::vector<std::string> workloads;
    std::string raw;
};

std::vector<std::string> split(const char* s) {
    std::vector<std::string> out;
    std::string cur;
    for (; *s; ++s) {
        if (*s == ',') { out.push_back(cur); cur.clear(); }
        else cur += *s;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool wanted(const std::vector<std::string>& list, const std::string& name) {
    if (list.empty()) return true;
    for (const auto& x : list)
        if (x == name) return true;
    return false;
}

void report(const Options& o, const std::string& alloc, const Trace& t,
            const std::vector<RunResult>& runs, std::FILE* raw) {
    std::vector<double> totals, batches;
    std::size_t failures = 0;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        totals.push_back(runs[i].ns_per_op);
        batches.insert(batches.end(), runs[i].batches.begin(), runs[i].batches.end());
        failures += runs[i].failures;
        if (raw)
            std::fprintf(raw, "%s,%s,%s,%zu,%.3f,%zu\n", o.label.c_str(), alloc.c_str(),
                         t.name.c_str(), i, runs[i].ns_per_op, runs[i].failures);
    }
    const double med = bench::percentile(totals, 0.5);
    const double lo = bench::percentile(totals, 0.0), hi = bench::percentile(totals, 1.0);
    std::printf("%s,%s,%s,%zu,%zu,%.2f,%.2f,%.2f,%.1f,%.2f,%.2f,%zu\n", o.label.c_str(),
                alloc.c_str(), t.name.c_str(), t.ops.size(), runs.size(), lo, med, hi,
                med > 0 ? 100.0 * (hi - lo) / med : 0.0, bench::percentile(batches, 0.5),
                bench::percentile(batches, 0.99), failures / (runs.empty() ? 1 : runs.size()));
    std::fflush(stdout);
}

template <ReallocMode Mode>
std::vector<RunResult> run_heap(const Trace& t, int reps) {
    std::size_t high_water;
    {  // warm-up on a fresh heap: discovers the pages this trace touches
        myalloc::Heap h;
        if (!h.init(kArena)) return {};
        HeapAlloc<Mode> a{h};
        replay(a, t);
        high_water = h.stats().high_water;
    }
    std::vector<RunResult> runs;
    for (int r = 0; r < reps; ++r) {
        auto h = std::make_unique<myalloc::Heap>();
        if (!h->init(kArena)) return runs;
        prefault(*h, high_water);
        HeapAlloc<Mode> a{*h};
        runs.push_back(replay(a, t));
    }
    return runs;
}

std::vector<RunResult> run_system(const Trace& t, int reps) {
    SystemAlloc a;
    replay(a, t);  // warm-up
    std::vector<RunResult> runs;
    for (int r = 0; r < reps; ++r) runs.push_back(replay(a, t));
    return runs;
}

// True if this process is being translated by Rosetta 2. Timing an x86_64
// binary on Apple Silicon measures the translator, not the allocator.
bool under_rosetta() {
    int translated = 0;
    std::size_t len = sizeof(translated);
    if (sysctlbyname("sysctl.proc_translated", &translated, &len, nullptr, 0) != 0) return false;
    return translated == 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (under_rosetta() && !std::getenv("BENCH_ALLOW_ROSETTA")) {
        std::fprintf(stderr,
                     "bench_timing: running under Rosetta (x86_64 binary on Apple Silicon); "
                     "rebuild for arm64. Set BENCH_ALLOW_ROSETTA=1 to override.\n");
        return 3;
    }
    Options o;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i];
        if (k == "--label") o.label = argv[i + 1];
        else if (k == "--reps") o.reps = std::atoi(argv[i + 1]);
        else if (k == "--alloc") o.allocs = split(argv[i + 1]);
        else if (k == "--workload") o.workloads = split(argv[i + 1]);
        else if (k == "--raw") o.raw = argv[i + 1];
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
    }
    std::FILE* raw = o.raw.empty() ? nullptr : std::fopen(o.raw.c_str(), "a");
    const char* nano = std::getenv("MallocNanoZone");
    const std::string system_name = (nano && std::strcmp(nano, "0") == 0) ? "system_nano_off" : "system";

    std::printf("label,allocator,workload,ops,reps,min_ns,median_ns,max_ns,spread_pct,"
                "batch_p50_ns,batch_p99_ns,failures_per_rep\n");
    for (const Trace& t : bench::all_traces()) {
        if (!wanted(o.workloads, t.name)) continue;
        for (const std::string& a : o.allocs) {
            if (t.uses_realloc && a != "system" && !kHeapHasRealloc) continue;
            if (!t.uses_realloc && (a == "heap_naive_realloc" || a == "heap_growonly_realloc")) continue;
            if (a == "system") report(o, system_name, t, run_system(t, o.reps), raw);
            else if (a == "heap") report(o, a, t, run_heap<ReallocMode::Native>(t, o.reps), raw);
            else if (a == "heap_naive_realloc") report(o, a, t, run_heap<ReallocMode::Naive>(t, o.reps), raw);
            else if (a == "heap_growonly_realloc") report(o, a, t, run_heap<ReallocMode::GrowOnly>(t, o.reps), raw);
            else std::fprintf(stderr, "unknown allocator %s\n", a.c_str());
        }
    }
    if (raw) std::fclose(raw);
}
