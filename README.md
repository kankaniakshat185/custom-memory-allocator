<h1 align="center">Custom Memory Allocator</h1>


A heap allocator built from scratch in C++17: `my_malloc`, `my_free`, `my_realloc` and `my_calloc` on one `mmap`'d arena. It uses an explicit free list, block splitting, O(1) boundary-tag coalescing, segregated size-class free lists (the default; first-fit and best-fit are compile-time alternatives), and hardening against use-after-free writes. The C API is thread-safe through per-thread arenas. Every performance claim here rests on three things:
- a heap invariant checker that passes after every operation of the stress tests;
- deterministic footprint measurements;
- real hardware counters from Apple Instruments, not just a wall-clock number.

<p align="center">
  <a href="#features">Features</a> ·
  <a href="#system-architecture">Architecture</a> ·
  <a href="#engineering-decisions-that-mattered">Engineering Decisions</a> ·
  <a href="#the-comparative-study">Results</a> ·
  <a href="#local-development-initialization">Build & Run</a>
</p>

## Features

*   **Free-list design.** The free list is explicit, intrusive and doubly linked: `next`/`prev` live inside free blocks, with no separate bookkeeping structure. Headers are 8 B, payloads 16 B-aligned, blocks at least 32 B.
*   **Splitting and coalescing.** Oversized free blocks are split on allocation. Coalescing is O(1) in both directions, using size-only footers on free blocks plus a prev-allocated header bit, so allocated blocks carry no footer at all.
*   **`realloc` and `calloc`.** `realloc` shrinks in place, grows in place into a free successor, and otherwise moves. A failed `realloc` leaves the original block valid and untouched. `calloc` checks the size multiplication for overflow.
*   **Three fit policies behind one template.** `BasicHeap<SegregatedFit>` (the default: exact-size bins up to 1 KiB, power-of-two bins above, a bitmap to find the next non-empty bin), `BasicHeap<FirstFit>` and `BasicHeap<BestFit>` share every line of code except the bins and the search. The policy is chosen at compile time with no runtime branch.
*   **Use-after-free hardening.** A write through a dangling pointer can never make the allocator read outside its arena or loop forever. Free-list links are validated before being followed, unlinking checks both neighbours first, and walks are bounded. Detected corruption stops the heap with a clear diagnostic instead of a segfault later.
*   **Opt-in small-object slab** (`myalloc::Allocator`): 16 KiB slabs carved from the heap, out-of-band free-slot bitmaps (use-after-free writes cannot reach its bookkeeping), exact double-free detection. 10% faster and 14% denser on app-like, but it failed the pre-set rule on the realloc workload, so it is not the default ([ADR-014](docs/adr/014-small-object-slab.md)). The heap also gained `aligned_malloc`.
*   **C++ integration.** `myalloc::allocator<T>` puts any standard container on the heap, including over-aligned types. Linking the opt-in `myalloc_new` library replaces all 20 global `operator new`/`delete` forms (nothrow, `align_val_t`, sized, `new_handler` retry), so an unmodified C++ program runs entirely on the heap ([demo](examples/stl_demo.cpp), [ADR-015](docs/adr/015-operator-new-and-stl-allocator.md)).
*   **Thread-safe C API with per-thread arenas.** Each thread allocates from its own arena (up to 8) under its own lock. Frees can come from any thread, and `fork()` is safe. One global lock *collapsed* to 0.2–0.3× of single-thread throughput at 2 threads; arenas scale 2.9–3.5× at 4 threads, against about 2.1× for libmalloc, at under 1% more instructions than that global lock. Padding the arena slots to separate cache lines doubled 4-thread throughput ([ADR-016](docs/adr/016-thread-safety.md)).
*   **Allocation tracking and leak reporting.** `my_alloc_stats()` returns bytes and blocks in use, peak, free space and fragmentation in O(1); `my_report_leaks()` lists every live block; `MYALLOC_LEAK_REPORT=1` prints that report at exit. No cost on the hot path.
*   **Strict checks at the API boundary.** Double frees, pointers outside the arena, misaligned pointers and requests too large for the arena (including `calloc` overflow) are rejected. Bad frees abort with a named diagnostic; oversized requests return `NULL`.
*   **439 test runs.** 142 tests built three times (once per fit policy), plus 13 in a binary where every allocation, GoogleTest's included, goes through the replaced `operator new`. They cover behaviour, a randomised stress suite that checks every heap invariant after *every* operation, a 300-seed use-after-free fuzz test, and C API integration running `std::vector`, `std::map` and `std::basic_string` on the allocator. All pass under AddressSanitizer + UBSan and under ThreadSanitizer. The allocator core has 99.7% line and 100% function coverage.
*   **The checks are themselves tested.** 22 deliberate mutations (an invariant, a merge, a hardening check, a bin bitmap, slab bookkeeping, the `new_handler` loop, the arena locks and fork handlers broken on purpose) each confirmed that a test fails, except one, which showed a hardening check was redundant; it was removed.
*   **9-job CI pipeline** (GitHub Actions) on every push: a blocking format check, sanitized builds (UBSan, ASan, TSan) and all 439 test runs on macOS and Linux (GCC and Clang), a regression check that the deterministic footprint probe still matches the committed results byte for byte, a benchmark smoke run, and a coverage report.
*   **Measured, not argued.** Deterministic footprint measurements and a trace-replay timing harness compare against the macOS system allocator, with its small-object fast path as a control. Instruments profiles (Time Profiler attributed through inlining, plus the CPU Counters bottleneck breakdown) explain *why* the numbers look the way they do.

## System Architecture

Templates instead of virtual functions, all the way down. `BasicHeap<Fit>` owns an `Arena` and the head of an intrusive free list that lives inside the free blocks themselves. Swapping the fit policy changes one template argument: no virtual call, no code change elsewhere. The C API is a thin layer over one process-wide heap. C++ code and the tests use `BasicHeap` directly, with any arena size.

```mermaid
flowchart TD
    A["Caller: my_malloc / my_free / my_realloc / my_calloc"] --> B["C API glue<br/>abort with a diagnostic on bad free or detected corruption"]
    B --> C["default_heap(): BasicHeap&lt;Fit&gt;, 256 MiB"]
    T["C++ callers, tests, benchmarks<br/>BasicHeap&lt;Fit&gt; directly, any arena size"] --> C
    C -->|"owns"| D["Arena: one fixed mmap reservation, lazily committed"]
    C -->|"head pointer"| E["Explicit free list<br/>links live inside the free blocks"]
    C --> F["malloc: find_fit -&gt; safe unlink -&gt; place (split)<br/>free: validate -&gt; release (coalesce both ways) -&gt; push"]

    C -.->|"template parameter,<br/>zero code changes elsewhere"| G["FirstFit (default): first block that fits"]
    C -.-> H["BestFit: smallest block that fits, stop on exact match"]
```

```
allocated: [ header 8B ][ payload ............................ ]
free:      [ header 8B ][ next 8B ][ prev 8B ] ...... [ footer 8B ]
header word = block size (multiple of 16) | alloc bit | prev-alloc bit
```

## Tech Stack

| Layer | Choice |
| :--- | :--- |
| Language | C++17 |
| Build System | CMake, with GoogleTest v1.17.0 fetched via `FetchContent`; arm64 forced on Apple Silicon |
| Testing | GoogleTest: 155 tests across 16 files; 142 built once per fit policy, plus 13 against the `operator new` replacement (439 runs) |
| Sanitizers | AddressSanitizer + UndefinedBehaviorSanitizer (`MYALLOC_ASAN`, `MYALLOC_UBSAN`), ThreadSanitizer (`MYALLOC_TSAN`) |
| Concurrency | `os_unfair_lock` per arena on macOS (`std::mutex` elsewhere), `pthread_atfork` handlers |
| Coverage | clang source-based coverage (`llvm-cov`): allocator core 99.7% lines, 100% functions |
| Benchmarking | Custom trace-replay wall-clock harness (batch timing, median + IQR over 15 reps) and a deterministic footprint probe |
| Charts | matplotlib, generated from the committed result files (`bench/scripts/plot.py`), light and dark variants |
| Profiling | Apple Instruments via `xctrace`: Time Profiler (attributed through inlining with `atos -i`) and CPU Counters bottleneck breakdown |
| CI | GitHub Actions, 7 jobs on every push: clang-format check; macOS UBSan; macOS ASan + UBSan; macOS Release with footprint-determinism check and harness smoke run; Linux GCC + UBSan; Linux Clang + ASan + UBSan; coverage report |

## Engineering Decisions That Mattered

Four decisions from the [Architecture Decision Log](docs/README.md) that shaped this project the most, each with what was actually measured, not just argued for.

### 1. Shipping a Phase That Made Things Worse, Then Fixing It With Coalescing

**Problem**: Phase 1 reused freed blocks whole, so a 16 B request could consume a 4 KiB block. Internal overhead reached 7.0×, and the app-like workload ran out of a 32 MiB arena while only 4.7 MB was requested.

**Approach**: Add splitting alone first (Phase 2), keeping coalescing for the next phase so each change could be measured on its own.

**Result**: Splitting fixed internal waste (7.0× → 1.01×), but out-of-memory failures rose from **0 to 9,293** on random sizes. The arena ended up 16.2 MB free but in 82,197 pieces, the largest only 1,232 B. On the time axis the phase couldn't finish a 1M-op benchmark run in 80 minutes. Coalescing (Phase 3) took failures to **0 on every workload** and utilisation on the phase-shift workload from 0.134 to **0.997**.

**Tradeoff**: Shipping a phase that is worse on one axis looks bad in isolation. But it's the only way the before/after of coalescing is attributable to coalescing. See [ADR-005](docs/adr/005-splitting.md) and [ADR-006](docs/adr/006-coalescing.md).

### 2. An Invariant Checker That Is Itself Tested

**Problem**: Allocator bugs corrupt the heap silently, and the crash appears thousands of operations later. ASan can't see corruption *inside* an `mmap`'d arena (and wasn't even usable on this machine until the toolchain changed).

**Approach**: `Heap::check()` walks every block and the free list and verifies every structural invariant: block sizes and alignment, footers, prev-alloc bits, no two adjacent free blocks, free list ↔ heap walk agreement, counters. The stress tests call it after **every** operation and verify per-block fill patterns before each free. Then the checker itself was **mutation-tested**: code it should catch was broken on purpose, 11 times across the project.

**Result**: Removing the backward merge fails 14 tests with `adjacent free blocks`. A stale prev-alloc bit fails 23. A truncated `realloc` copy produces 7,728 `realloc lost data` reports. One hardening check removed this way failed *no* test; it was redundant and was deleted.

**Tradeoff**: Per-operation checking makes the stress tests effectively quadratic in heap size, so their workloads are sized to keep the suite fast. It only runs in test builds; benchmarks never call it. See [ADR-004](docs/adr/004-verification-strategy.md).

### 3. A Controlled Experiment Instead of a Story

**Problem**: In-place `realloc` copied 55% fewer bytes than always-move, but its footprint was **2.1×** larger on a buffer-growth workload, and it grew without bound as the arena grew. The first hypothesis (in-place *shrinking* pins small blocks) was tested with a control policy and **refuted**: it explained only about a quarter of the gap.

**Approach**: Change exactly one variable, the fit policy (first-fit → best-fit), and rerun the identical workload.

**Result**: Under best-fit the gap shrank from 2.1× to **1.01×**. The bloat was an interaction with LIFO first-fit, not a property of in-place `realloc`, which stayed, and is 1.2× faster than always-move. The same experiment confirmed why app-like utilisation was only 0.310 under first-fit: best-fit reaches **0.849**.

**Tradeoff**: Best-fit pays for that memory with a full-list scan: 57–219 nodes visited per malloc vs. ~2 for first-fit, which makes it 3–13× slower. First-fit stayed the default until the structural fix, size-segregated lists, was built and measured; it became the default in [ADR-011](docs/adr/011-segregated-free-lists.md). See [ADR-007](docs/adr/007-realloc-and-calloc.md) and [ADR-009](docs/adr/009-first-fit-vs-best-fit.md).

### 4. Hardening Tuned by Measurement, Not Guesswork

**Problem**: The requirement said a use-after-free write must not crash the allocator's bookkeeping. One 16-byte write into a freed block followed by `malloc` → **segmentation fault**.

**Approach**: Validate every link before following it, check both neighbours before unlinking, bound every walk, and stop with a diagnostic on detected corruption. Then measure the cost and remove what it shows to be unnecessary.

**Result**: Four measured iterations took best-fit's overhead from **+66–78%** to **+7–9%**, and found two specific costs. A footer check cost a cache miss per node, and dropping it took the adversarial overhead from +38% to ~0%. The per-node search check was best-fit's whole overhead. A 300-seed use-after-free fuzz test runs without a crash or hang under both policies and under ASan.

**Tradeoff**: First-fit still pays **~+28%** on typical workloads. The CPU Counters profile shows that this is extra retired instructions (+3.07 G useful slot-cycles) on the free path, not cache misses or mispredicts. That cost flipped app-like from 1.12× faster than libmalloc to 1.14× slower. See [ADR-012](docs/adr/012-use-after-free-hardening.md) and [ADR-010](docs/adr/010-profiling-findings.md).

## Benchmarking

Four synthetic workloads, each isolating a different behaviour:
- random sizes, 16 B–64 KiB;
- app-like: 95% ≤ 256 B, 5% up to 256 KiB;
- an adversarial trace that puts 2,000 non-fitting holes in front of every exact-fit block;
- buffer growth through `realloc`.

Each workload is a **pre-generated, seeded op trace**, so RNG cost is never timed and every allocator replays the identical sequence.

The Apple Silicon timer ticks every ~41.7 ns, about the cost of one `malloc`, so operations are timed in **batches of 1,024**, never individually. Each heap rep gets a fresh arena **pre-faulted to the trace's high-water mark**, so this allocator doesn't pay first-touch page faults that the warm system allocator doesn't. Every run does an untimed warm-up, then 15 timed reps, reported as **median with IQR**. Max − min was dominated by single outlier reps on this machine.

Three measurement problems were found and fixed along the way, all documented in [ADR-008](docs/adr/008-benchmark-methodology.md):
- **An inherited setting changed the baseline.** The editor exported `MallocNanoZone=0`, which silently switched off libmalloc's small-object fast path. The scripts now set it explicitly on every run.
- **The installed CMake built for x86_64.** Every build ran under Rosetta translation until this was caught. CMake now forces arm64, and the harness refuses to run translated.
- **Max − min hid real stability.** One row showed a max − min spread of 55% with an IQR of 1.2%, so IQR is the noise criterion now.

**Environment**: Apple M2 (arm64), macOS 26.5.2, Xcode Apple clang 21, `-O3 -DNDEBUG`, AC power, no other significant load (machine state recorded per run in [`docs/results/`](docs/results/)).

## Baseline Performance (Phase 1)

The first working version uses an explicit free list with first-fit, reuses freed blocks *whole* (no splitting) and has no coalescing. Fresh memory comes from a bump pointer. It is deliberately simple and correct, not optimised yet.

| Workload | Median | Batch p99 | Internal overhead | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Random sizes** | 15.1 ns/op | 95 ns | 3.83× | the fastest median of any version, paid for in memory |
| **App-like** | (26.0 ns/op) | 122 ns | **7.00×** | **436 failed allocations per rep even in 256 MiB**, so not comparable as a speed |
| **Adversarial** | 2,000.5 ns/op | 8.0 µs | – | free list grows every round, averaging 3,039 blocks at each malloc |

Phase 1's random-size median beating every later version is why the comparative study measures memory alongside time: its speed is bought with a 3.8–7× waste of memory and with out-of-memory failures.

## The Comparative Study

The allocator was built one variable at a time, and each phase was verified by the full test suite and the invariant checker before being measured. Full detail is in the [Architecture Decision Log](docs/README.md); the short version:

*   **Phase 2, splitting:** internal overhead 3.83–7.00× → **1.00–1.02×**. But with no coalescing, out-of-memory failures rose **0 → 9,293** on random sizes, the free list shredded into 107,655 pieces, and **the phase could not finish a timing run in 80 minutes**. Splitting without merging is a measured regression, kept on purpose so the next phase's effect is attributable ([ADR-005](docs/adr/005-splitting.md)).
*   **Phase 3, coalescing:** out-of-memory failures **0 on every workload**. Utilisation on random sizes 0.099 → **0.615**, on phase shift 0.134 → **0.997**. The adversarial workload is **2.9× faster than Phase 1**, because its free list stays at 358 blocks instead of growing to 3,000+. Coalescing costs ~0.8 ns/op against Phase 1 on random sizes and cuts the batch p99 from 95 to 26 ns ([ADR-006](docs/adr/006-coalescing.md)).
*   **Phase 4, `realloc`/`calloc`:** the allocation paths were refactored into shared `place()`/`release()` helpers, with byte-identical footprint output, and timing is unchanged within noise. In-place `realloc` is **1.2× faster than always-move** ([ADR-007](docs/adr/007-realloc-and-calloc.md)).
*   **Phase 6, best-fit variant:** **2.7× less memory on app-like** (utilisation 0.310 → 0.849), but **12.8× slower on random sizes**. First-fit visits ~2 free blocks per malloc and best-fit 57–219. First-fit stayed the default at the time ([ADR-009](docs/adr/009-first-fit-vs-best-fit.md)); segregated fits replaced it in Phase 13.
*   **Phase 13, segregated free lists (now the default):** the worst case is **42× faster than first-fit** (16.5 vs. 694 ns/op), within 1.3× of libmalloc instead of 53×. App-like is 29% faster and realloc 1.8× faster than first-fit, with **2.3× less memory** on app-like. Random sizes are 8.6% slower (range bins are walked). The gain on app-like comes from 6.7× fewer split/merge cycles, not from faster search. Measured in one run on a loaded machine; the tables and charts below are refreshed in Phase 17 ([ADR-011](docs/adr/011-segregated-free-lists.md)).
*   **Hardened:** use-after-free safety for **+28% on first-fit** typical workloads and **+1% on adversarial**, with byte-identical footprint ([ADR-012](docs/adr/012-use-after-free-hardening.md)).

All versions measured back-to-back in one invocation of `bench/scripts/run_phases.sh` (v1.0 final run, before ADR-011, so every column is first-fit; median ns/op; IQR ≤ 5.6% except where marked; [data](docs/results/final/timing-summary.md)):

| Workload | Phase 1 | Phase 2 | Phase 3 | Phase 4 | Phase 6 | **Hardened** |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Random sizes | 15.08 | untimeable | 15.86 | 15.93 | 16.06 | **20.63** |
| App-like | 26.03 (436 OOM/rep) | untimeable | 16.49 | 16.59 | 16.66 | **21.27** |
| Adversarial | 2,000.5 | untimeable | 685.5 | 681.0 | 632.9 | **640.1** |
| `realloc` growth | – | – | – | 26.53 | 28.16 | **39.15** (noisy, IQR 10.4%) |

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/results/charts/versions-dark.png">
  <img alt="Bar charts, one per workload, of ns per operation for Phase 1 to Hardened; values as in the table above" src="docs/results/charts/versions-light.png">
</picture>

Memory (deterministic footprint probe; utilisation = peak live bytes ÷ arena high-water; OOM = failed allocations):

| Workload | Phase 1 | Phase 2 | Phase 3–Hardened (first-fit) | Best-fit |
| :--- | :--- | :--- | :--- | :--- |
| Random sizes | 0.242 | 0.099, **9,293 OOM** | 0.615 | **0.913** |
| App-like | 0.142, **1,035 OOM** | 0.142, 549 OOM | 0.310 | **0.849** |
| Phase shift | 0.589 | 0.134, 226 OOM | **0.997** | **0.997** |

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/results/charts/footprint-dark.png">
  <img alt="Bar charts of memory utilisation per version and policy for three workloads; values as in the table above" src="docs/results/charts/footprint-light.png">
</picture>

**Against the system allocator** (same final session, separate invocation; [data](docs/results/final/timing-summary.md)):

| Workload | libmalloc | libmalloc, Nano zone off | **This allocator (first-fit)** | |
| :--- | :--- | :--- | :--- | :--- |
| Random sizes | 29.88 | 29.02 | **22.63** | 1.32× faster |
| App-like | **18.75** | 21.62 | 21.36 | 1.14× slower; level with libmalloc once its Nano fast path is off |
| Adversarial | **12.37** | 13.65 | 653.28 | 53× slower |
| `realloc` growth | 37.51 | 43.39 | **33.93** | 1.11× faster |

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/results/charts/vs-libmalloc-dark.png">
  <img alt="Bar charts per workload comparing libmalloc, libmalloc with Nano off, first-fit and best-fit; values as in the table above" src="docs/results/charts/vs-libmalloc-light.png">
</picture>

These are first-fit numbers from the v1.0 final run. With segregated fits, now the default, the same comparison gives (one run on a loaded machine, ADR-011): random 1.29× faster than libmalloc, **app-like 1.21× faster**, adversarial 1.26× slower (was 53×), realloc 1.89× faster.

These numbers are for the heap itself, which takes no lock. The thread-safe C API adds 8–12% instructions per call for its lock; under that harness, libmalloc is ahead single-threaded on random sizes (ADR-016).

## Bottlenecks

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/results/charts/bottlenecks-dark.png">
  <img alt="Stacked bars of CPU pipeline-slot shares (useful, delivery, processing, discarded) per profiled run; values in ADR-010" src="docs/results/charts/bottlenecks-light.png">
</picture>

What still limits this allocator, as measured, not guessed at ([ADR-010](docs/adr/010-profiling-findings.md)):

*   **The free path, not the search, dominates typical workloads.** `release` plus unlinking take ~51% of the time; the free-list search ~10%. LIFO first-fit visits only ~2 of the 855–1,610 free blocks per malloc. Segregated fits attacked exactly this cost: exact-size reuse cuts split/merge churn 6.7× on app-like (ADR-011).
*   **Branch mis-speculation.** ~37–52% of pipeline slots are discarded on random-size workloads: every operation is ~50 cycles of data-dependent decisions. This includes the harness's own unpredictable alloc/free dispatch; the counter samples can't separate the two.
*   **No small-object fast path yet.** Under first-fit, libmalloc won app-like by 14% (level with its Nano zone off). Segregated fits' exact bins now put the heap 1.21× ahead there, even without a slab.
*   **Range-bin walks.** Segregated fits removed the 53× worst case (the walk through 2,000 holes of the wrong size; now 1.26× of libmalloc). Sizes above 1 KiB still share power-of-two bins that are walked first-fit, so random sizes are 8.6% slower than first-fit, with search at 30% of the time. Two-level bins are the targeted fix (ADR-011).
*   **Measurement limits.** Cross-run drift is up to ~10%, so only same-run comparisons are trusted. The ~41.7 ns timer means per-call latency isn't observable, only 1,024-op batches. There is no core pinning on macOS.

## Limitations & Non-Goals

These are scope boundaries, not oversights (see [docs/plan.md](docs/plan.md)):

*   **At most 8 arenas, no memory shared between them.** Free space in one arena is only used by another thread once that thread's own arena is full, and `my_realloc` never moves a block across arenas. Each arena is a fixed 256 MiB reservation.
*   **Fixed arenas.** They never grow, and freed memory is never returned to the OS.
*   **The slab is opt-in, not the default** (C++ `myalloc::Allocator` only): it regresses the realloc workload (+20% time, +40% footprint, ADR-014). **Range bins are single-level** (ADR-011 Alternative 2).
*   **Use-after-free hardening guarantees memory safety and termination, not detection**, and is not a defence against a deliberate attacker. Free-list pointers aren't mangled or checksummed.
*   **Not a drop-in system `malloc`.** C++ `operator new` can be replaced (opt-in), but there is no `malloc` symbol interposition, so C code and system libraries keep using libmalloc. On container workloads the heap-backed replacement retires 15–18% more instructions than libmalloc on `hash_count` and `list_queue`; a slab-backed variant is lower on all four, but is bench-only until a clean timing run (ADR-015).
*   **One machine, one baseline.** Measured on an Apple M2 against macOS libmalloc only; glibc was not measured.

## Testing

142 tests across 15 files, each built three times: `myalloc_tests` (segregated fits, the default), `myalloc_tests_firstfit` and `myalloc_tests_bestfit` (that policy as the default, C API included), plus `myalloc_new_tests` (13 tests) linked with the global `operator new`/`delete` replacement, so **439 runs**.

*   **Unit tests:** alloc/free and reuse order, split arithmetic and thresholds, merges in each direction, prev-alloc bit maintenance, every `realloc` path (shrink, grow in place, move, failure leaves the original intact), `calloc` zeroing and overflow, alignment for every size 0–600, fit-policy choices with exact search-step counts.
*   **Randomised stress:** seeded alloc/free/realloc mixes with the heap invariant checker after **every** operation and fill patterns verified before every free. One configuration uses a 64 KiB arena to keep the out-of-memory path constantly exercised. Every seed must end with the arena back to one free block.
*   **Use-after-free:** targeted tests for each kind of metadata overwrite (garbage links, links into allocated blocks or outside the arena, cycles, bad footers), plus a **fuzz test**: 300 seeds × 3,000 ops with random writes through dangling pointers. Its real assertion is "no crash, no hang".
*   **Integration:** the C API used like a program would: linked lists, strings, a 32 MiB block, a `realloc` string builder, a `calloc` matrix, and `std::vector`, `std::map` and `std::basic_string` running entirely on `my_malloc` through the public `myalloc::allocator<T>`, including over-aligned element types. Death tests check that bad frees and detected corruption abort with the right message.
*   **Concurrency:** 8-thread churn with every arena checked afterwards, cross-thread frees (producer/consumer), arena fallback on exhaustion, stats and leak report across arenas, and 20 `fork()`s while 4 threads allocate. ThreadSanitizer runs the whole suite.
*   **Tracking:** the heap walk visits exactly the live blocks in address order and stops safely at a damaged header; C API stats deltas and peak; the leak report's exact output; the at-exit report checked in forked children with the variable set and unset.
*   **Mutation checks:** 22 deliberate breakages of invariants, merges, `realloc`, the hardening checks, segregated-fit bins, slab bitmaps, the `operator new` replacement, the arena locks and the fork handling, each confirming that a test fails, except the one that exposed a redundant check (see [ADR-004](docs/adr/004-verification-strategy.md), [ADR-012](docs/adr/012-use-after-free-hardening.md)).

*   **`operator new` replacement:** every replaced form lands in the heap's arena, over-aligned `new` up to 64 KiB, `bad_alloc`/`nullptr` on exhaustion, the `new_handler` retry loop (throwing and nothrow), containers, `make_shared` and exceptions, and death tests for double and foreign `delete`.

All 439 runs pass under UBSan, under ASan + UBSan and under ThreadSanitizer (0 reports).

**Coverage** (clang source-based, both binaries merged): allocator core 99.7% lines, 100% functions, 86.3% branches. Including the C API glue, 97.1% lines and 100% functions. The uncovered glue lines are abort diagnostics, which death tests do execute, but in child processes that `abort()` before writing profile data ([report](docs/results/coverage.md)).

## Code Quality

*   **Concurrency confined to one layer.** Locks, atomics and `thread_local` appear only in `src/myalloc.cpp` and `src/lock.h` (verified by grep); `BasicHeap` and the slab contain no synchronisation. Every call holds at most one arena lock at a time. Only the fork handlers take several, always the creation mutex first, then arenas in index order, so there is no lock-order cycle. ThreadSanitizer runs the whole suite in CI.
*   **Warning-clean** under `-Wall -Wextra -Wpedantic`, in Debug, Release, ASan and coverage builds.
*   **Dead code removed when found.** Coverage exposed a helper overload left unused after a hardening iteration, and it was deleted.
*   **CI on every push** ([`ci.yml`](.github/workflows/ci.yml)): every test under UBSan and under ASan + UBSan on macOS and Linux (GCC 13 and Clang), warnings visible in the logs, a footprint-determinism check that fails if a change alters any placement decision, and coverage published as a job summary.
*   **`clang-format`, blocking in CI** (pinned 22.1.8, [`.clang-format`](.clang-format)): a formatting diff fails the build. The style was chosen to match the existing code with the least churn.
*   **Not set up yet:** `clang-tidy`.

## Local Development Initialization

The project fetches GoogleTest automatically via CMake `FetchContent` on first configure (network needed once). The allocator itself has no dependencies beyond POSIX `mmap`. Developed on macOS (Xcode clang); on Apple Silicon CMake forces native arm64 builds. It also builds warning-free and passes all tests on Linux with GCC 13 and Clang 18 (Ubuntu 24.04; arm64 locally through Phase 14, x86_64 in CI). All published benchmark numbers are from macOS.

```bash
# Correctness: 142 tests x 3 fit policies + 13 operator new tests, with UBSan
cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Debug -DMYALLOC_UBSAN=ON
cmake --build build-test -j
ctest --test-dir build-test --output-on-failure

# Same suite under AddressSanitizer + UBSan
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMYALLOC_ASAN=ON -DMYALLOC_UBSAN=ON
cmake --build build-asan -j && ctest --test-dir build-asan

# ThreadSanitizer
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DMYALLOC_TSAN=ON
cmake --build build-tsan -j && ctest --test-dir build-tsan

# Release tools
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/footprint_probe                   # memory metrics, first-fit (add --best-fit or --segregated)
./build/footprint_probe realloc           # realloc policy comparison
./build/freelist_length app_like          # free-list length and nodes visited per malloc

# Timing: vs libmalloc (+ Nano-off control), then every phase commit
bench/scripts/run_final.sh
python3 bench/scripts/summarize.py        # median + IQR -> docs/results/timing-summary.md
bench/scripts/run_mt.sh                   # threads: libmalloc vs arenas vs global lock
bench/scripts/run_stl.sh                  # standard containers under each operator new
python3 bench/scripts/plot.py             # charts (light + dark) -> docs/results/charts/, needs matplotlib

# Formatting check (same pinned version as CI: pip install clang-format==22.1.8)
git ls-files '*.cpp' '*.h' | xargs clang-format --style=file --dry-run -Werror

# Profiling (Instruments): see docs/results/profiles/README.md
python3 bench/scripts/profile_attrib.py <trace> <binary>
python3 bench/scripts/counters_summary.py <trace> [...]
```

Using it:

```c
#include <myalloc/myalloc.h>

void* p = my_malloc(100);       /* 16-byte aligned; NULL when the arena is exhausted */
p = my_realloc(p, 4000);        /* on failure returns NULL and p stays valid */
void* z = my_calloc(10, 8);     /* zeroed; NULL on size overflow */
my_free(p);
my_free(z);                     /* double free / invalid pointer / corruption -> abort with a message */

my_alloc_stats_t s;
my_alloc_stats(&s);             /* O(1): bytes/blocks in use, peak, free bytes, free blocks, ... */
my_report_leaks(stderr);        /* every live block, address-ordered; MYALLOC_LEAK_REPORT=1 does this at exit */
```

## Project Structure

```
include/myalloc/  # myalloc.h: the C API
src/              # arena (mmap), block layout, BasicHeap<FirstFit|BestFit>, C API glue
tests/            # unit, stress, use-after-free fuzz and integration tests (GoogleTest)
bench/            # timing harness + workloads, footprint probe, free-list probe
bench/scripts/    # run_all / run_phases / run_final, summarize, profile_attrib, counters_summary
docs/adr/         # architecture decision records 001-012
docs/results/     # raw CSVs, summaries, machine state per run, profiles, coverage
```

`BasicHeap` (in `src/heap.h`) is templated on the fit policy, so segregated fits, first-fit and best-fit are the same class instantiated three times. The benchmark scripts build *older phase commits* from `git archive` against the current harness, so every phase can be re-measured at any time.

## Documentation

The full writeup lives in [`docs/`](docs/):

*   [Project plan](docs/plan.md): scope, phases, budget, exit criteria
*   [Architecture Decision Log](docs/README.md): 16 ADRs (001–016), each with alternatives considered and measured before/after numbers
*   [Testing strategy](docs/testing.md)
*   [Environment](docs/environment.md): machine, toolchain, and the toolchain incidents found along the way
*   [Results](docs/results/): every CSV behind every number in this README, with the machine state at the time

## License

MIT License. See `LICENSE` for more information.
