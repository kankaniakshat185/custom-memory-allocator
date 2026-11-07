# Custom Memory Allocator — Project Plan (v2, macOS)

Status: **Tier 1 complete** (Phases 0–8, tagged `v1.0`). This plan is kept as written at the start, with dated resolution notes where things changed, so it shows both the original decisions and their outcomes. The live task list for what's next is [§10](#10-post-v10-task-list-tier-2-and-loose-ends).

## 0. Environment (measured on the dev machine)

| Item | Value |
|---|---|
| CPU | Apple M2, arm64, 8 cores |
| OS | macOS 26.5.2 |
| Page size | 16 KiB (not 4 KiB) |
| Cache line | 128 B |
| Compiler | Apple clang 17.0.0, CMake 4.4.2 (from Phase 5: Xcode's Apple clang 21; see [environment.md](environment.md)) |
| Baseline allocator | macOS system `malloc` (libmalloc), **not glibc** |

Two things found while checking the machine, which change the plan:

1. **`xctrace` is not installed** (`xcrun --find xctrace` fails), so only the Command Line Tools are present, not full Xcode. Instruments (Time Profiler, CPU Counters, Allocations, os_signpost) is unavailable until Xcode is installed. **Phase 0 task: install Xcode and verify Instruments launches. If that fails, fall back to `sample`/`spindump` and `xcrun xctrace` alternatives (see §5).**
2. **An ASan+UBSan smoke test (empty `main`) compiled but hung when run** and had to be killed. This may be a machine/setup problem like the one noted in the Matching Engine docs. **Phase 0 task: timebox 30 min to diagnose** (try `-fsanitize=address` alone, then `undefined` alone, check SIP/Rosetta/Homebrew LLVM vs Apple clang). If unresolved, do not block: see §5 on what replaces it.

> **Resolved (Phase 5, 2026-10-24/25).** (1) Xcode was installed; `xctrace` 16.0 works and was used for all profiling (ADR-010), so the §5 fallback was never needed. (2) The ASan hang was specific to the Command Line Tools' clang 17; with Xcode's clang 21, ASan + UBSan run normally and the full suite passes under them. A third issue surfaced later: the installed CMake is an x86_64 binary that built everything for Rosetta. It is now forced to arm64 (ADR-008, [environment.md](environment.md)).

## 1. Scope guardrails

- **Tier 1 is the whole project:** single-threaded allocator on one fixed arena, first-fit and best-fit variants, correctness tests, benchmarks, ADRs. Nothing from Tier 2 starts until the exit criteria in §7 are met.
- **Fixed arena, no growth.** One `mmap` of 256 MiB (`MAP_PRIVATE | MAP_ANON`) at init, never grown. Pages are committed lazily on first touch. Do not rely on `MAP_NORESERVE` (no useful effect on macOS). Fixed size makes OOM tests deterministic.
- **API:** `my_malloc`, `my_free`, `my_realloc`, `my_calloc`. Free-standing C-style functions.
- **No `LD_PRELOAD`-style interposition.** On macOS the equivalent (`DYLD_INSERT_LIBRARIES`, zone interposition) is restricted by SIP and fragile; drop it. Tier 2 `operator new/delete` covers the drop-in demo.
- **Linux as a bonus only:** optionally run the correctness tests (not benchmarks) on a Linux CI runner for portability. Keep the code POSIX-clean (`MAP_ANON` is accepted on both).

## 2. Core design (justified in ADR-001/002)

| Decision | Proposal | Reason |
|---|---|---|
| Alignment | 16 B | Matches libmalloc on arm64/x86-64; safe for `max_align_t`, NEON types. |
| Header | 8 B: size (multiple of 16, low 4 bits free), bit 0 = this block allocated, bit 1 = previous block allocated | Flags cost no extra space. |
| Free-block layout | header, `next`, `prev` (in payload), footer | Intrusive list, no external bookkeeping. |
| Footer | Free blocks only, located via the prev-allocated bit | O(1) backward coalescing without 8 B on every allocated block (CS:APP boundary-tag optimization). |
| Min block size | 32 B | Header + 2 pointers + footer, 16-aligned. |
| Free list | Explicit doubly-linked, LIFO insert | O(1) unlink during coalescing. |
| Bad / double free | Check pointer in arena, 16-aligned, allocated bit set | Meets the "basic" requirement. |
| Realloc | Shrink: split in place. Grow: absorb next free block if large enough. Else malloc + copy + free. | In-place grow is most of realloc's win. |
| Terminators | Prologue/epilogue sentinel blocks | Removes boundary special cases. |

Single-list first-fit/best-fit is O(free blocks) per allocation. That is expected, and the benchmarks should expose it. Segregated size-class lists are **conditional**: only if profiling shows free-list traversal dominating (§4, Phase 7).

## 3. Phases and time budget (~10–11 working days, 3–4 days slack)

Slack is real slack. Do not spend it on Tier 2 before §7 is met.

| # | Phase | Budget | Output | ADR |
|---|---|---|---|---|
| 0 ✅ | Scaffolding + toolchain verification | 0.5–1 d | CMake, GoogleTest (FetchContent), arena wrapper, `heap_check()` skeleton, Xcode/Instruments check, ASan diagnosis (timeboxed) | ADR-003 (arena) |
| 1 ✅ | Baseline allocator: arena, headers, alignment, explicit free list, first-fit, no split/coalesce | 1.5 d | `my_malloc`/`my_free`, basic tests | ADR-001, ADR-002, ADR-004 (verification) |
| 2 ✅ | Splitting | 0.5 d | Remainder to free list, min-split threshold | ADR-005 |
| 3 ✅ | Coalescing (boundary tags, both directions) | 1.5 d | Coalescing tests, `heap_check()` invariants in every test | ADR-006 |
| 4 ✅ | Realloc, calloc, double-free, OOM | 1 d | Full Tier 1 test suite | ADR-007 |
| 5 ✅ | Benchmark harness: 3 workloads × {libmalloc, first-fit, best-fit} | 1.5 d | `bench/` binary, CSV output, plot script | ADR-008 (methodology) |
| 6 ✅ | Best-fit variant + comparison (compile-time policy template) | 1 d | Variant selectable with no runtime branch | ADR-009 |
| 6b ✅ | Use-after-free hardening (closes a requirements gap found in review) | 0.5 d | Validated links, safe unlinking, fail-stop, UAF fuzz test | ADR-012 |
| 7 ✅ | Profiling and analysis (Instruments) | 1.5 d | Profiler-backed "why" sections; segregated lists only if justified | ADR-010 (+ ADR-011 conditional) |
| 8 ✅ | README, results table, ADR consistency pass | 1 d | Tag `v1.0` | – |

## 4. Benchmark methodology

**Workloads** (seeded, identical op sequences for every allocator):
- (a) random sizes, log-uniform 16 B – 64 KiB, random free order;
- (b) app-like: ~95% small (16–256 B), ~5% large (4–256 KiB), bursty lifetimes;
- (c) adversarial fragmentation: alternating small/large allocations, free every other block so nothing coalesces, then request sizes needing a bigger hole.

**Metrics:**
- ns/op (median of N runs, report spread), throughput;
- **memory efficiency**: peak footprint ÷ peak live bytes;
- free-list length over time (own allocator only).

**macOS-specific measurement rules:**
- **Timer granularity.** On Apple Silicon the system timer ticks at 24 MHz (~41.7 ns). Per-op timing of ~20 ns operations is meaningless. Time **batches** (thousands of ops per sample) with `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)` or `mach_absolute_time`, and derive ns/op. Report p99 over batch samples, not per call.
- **No CPU pinning on macOS.** Mitigate: AC power, Low Power Mode off, close other apps, warm-up runs, more repetitions (≥15), report min/median/max and discard nothing silently. Watch for thermal throttling on long runs; note the M2 mixes performance and efficiency cores, so check run-to-run variance and re-run if it exceeds a few percent.
- **Footprint must be measured identically for both allocators.** Instruments' Allocations instrument only sees allocations that go through libmalloc, so it **cannot see `my_malloc`'s arena**. For the efficiency metric use an external measure for both: `phys_footprint` (and its peak) from `task_info(TASK_VM_INFO)`, plus the allocator's own counters for peak live bytes. Because the arena is never `madvise`d back, touched pages act as a high-water mark. Cross-check libmalloc with `malloc_zone_statistics`. Allocations is still useful for libmalloc-side behaviour.

**Baseline control (correction to the "no control possible" assumption):** libmalloc has no tcache, but it does have a comparable fast path, the **Nano zone** for small allocations (≤256 B). It can be disabled with the environment variable `MallocNanoZone=0`. This gives a controlled experiment in the same spirit as the tcache-off control: run libmalloc with and without Nano and see how much of its small-allocation speed comes from that fast path. **Verify empirically that the variable changes behaviour on this OS version** before relying on it, and document the result either way. If it has no effect, record that as the limitation: "comparison baseline is macOS libmalloc, not glibc; no fast-path-disable control was possible on this platform."

Also document in every ADR that libmalloc is a multi-zone, size-class allocator tuned for thread-safe use, while this allocator is single-threaded with a lock-free fast path by construction. Part of any speed gap is the thread-safety cost libmalloc pays and this project deliberately doesn't.

## 5. Tooling

| Need | Tool | Notes |
|---|---|---|
| CPU hot spots | Instruments **Time Profiler**, `os_signpost` regions per workload | Same workflow as Matching Engine ADR-0022. Needs full Xcode. |
| Microarchitectural "why" | Instruments **CPU Counters** (bottleneck breakdown) | Apple Silicon only; check availability on this OS/Xcode in Phase 0. |
| libmalloc footprint | Instruments **Allocations**, `vmmap`, `heap`, `leaks` CLI tools | Cannot observe `my_malloc` (see §4). |
| Own-allocator footprint | Allocator debug stats + `task_info` | Built in Phase 4/5. |
| Fallback if Instruments unavailable | `sample <pid>` / `spindump` for stacks; wall-clock harness + counters for the rest | Weaker for the "why"; call it out in ADR-010 if used. **Not needed:** Instruments became available in Phase 5. |
| Correctness | GoogleTest + **`heap_check()` after every op in stress tests** | Primary correctness tool, independent of sanitizers. |
| Sanitizers | ASan/UBSan **if the Phase 0 hang is resolved** (resolved in Phase 5: ASan + UBSan now run on every test) | Even when working, ASan cannot see overflows *inside* the mmap'd arena (it doesn't know the arena's block structure), so it covers test code and the allocator's own stack/globals, not heap corruption within the arena. `heap_check()` and guard patterns cover that. If ASan stays broken, use UBSan alone, `MallocScribble`-style fill patterns in debug builds, and Apple Guard Malloc on the *test code* where useful. |

Valgrind and `perf` are not used.

## 6. Repo layout

```
custom-memory-allocator/
├── CMakeLists.txt
├── README.md                  # summary, build, results table
├── .clang-format
├── include/myalloc/
│   └── myalloc.h              # my_malloc / my_free / my_realloc / my_calloc, debug API
├── src/
│   ├── arena.{h,cpp}          # mmap reservation, bounds checks
│   ├── block.h                # header/footer encoding, bit tricks, pointer math
│   ├── allocator.cpp          # core: split, coalesce, realloc
│   ├── policy_first_fit.h
│   ├── policy_best_fit.h
│   └── heap_check.cpp         # invariant checker (debug builds + tests)
├── tests/
│   ├── test_basic.cpp
│   ├── test_alignment.cpp
│   ├── test_coalescing.cpp
│   ├── test_realloc.cpp
│   ├── test_oom_double_free.cpp
│   └── test_stress_fragmentation.cpp
├── bench/
│   ├── harness.{h,cpp}        # batch timing, stats, CSV output
│   ├── workloads.{h,cpp}
│   ├── bench_main.cpp
│   └── scripts/               # run_all.sh, plot.py
└── docs/                      # all documentation (see docs/README.md)
    ├── plan.md                # this file
    ├── adr/
    │   ├── 000-template.md
    │   ├── 001-block-tracking-structure.md
    │   └── ...
    ├── results/               # CSVs, Instruments traces/screenshots, vmmap output
    └── environment.md         # machine, OS, compiler, flags, power state per run
```

C++17, GoogleTest, custom wall-clock harness. Builds: test config (`-O0 -g`, sanitizers if working) and benchmark config (`-O2 -DNDEBUG`, no sanitizers). Page-size-dependent code must use `getpagesize()`/`sysconf`, never a hard-coded 4096 (this machine is 16 KiB).

## 7. Tier 1 exit criteria (gate before any stretch goal)

1. All tests pass; `heap_check()` passes after every operation in the stress tests; sanitizer status (clean, or documented as unavailable) recorded.
2. Three workloads × three allocators (libmalloc, first-fit, best-fit) have repeatable numbers, with the run-to-run spread reported.
3. ADR-001 through ADR-009 complete with measured before/after numbers.
4. README results table filled in; at least one profiler-backed explanation of a result (or the documented fallback if Instruments was unavailable).

Tier 2 order if time remains: debug stats (0.5 d) → `operator new` + `std::vector` demo (0.5–1 d) → slab fast path (2 d) → thread safety last. Note: arm64 has a weaker memory model than x86; relevant only once threads arrive.

## 8. ADRs

All ADRs live in [adr/](adr/); the index with statuses is in [README.md](README.md). The ADR-001 skeleton that was drafted here is now [adr/001-block-tracking-structure.md](adr/001-block-tracking-structure.md).

## 9. Decisions open at the start (all resolved)

1. **Segregated lists:** conditional on profiling evidence (default, as agreed). No budget pre-committed. → **Resolved:** profiling (ADR-010) justified them for memory and worst-case latency, not average speed. Scheduled as Phase 13 (§10), ADR-011.
2. **Harness:** custom wall-clock (agreed). → **Done:** ADR-008.
3. **Instruments availability:** blocked on installing full Xcode; decide in Phase 0 whether the fallback tooling in §5 is acceptable if that fails. → **Resolved in Phase 5:** Xcode installed; Instruments used for ADR-010.
4. **ASan:** diagnose in Phase 0 (30 min timebox); do not let it block Phase 1. → **Resolved in Phase 5:** the toolchain change fixed it. It didn't block Phase 1; UBSan and the invariant checker covered Phases 1–4.

## 10. Post-v1.0 task list (Tier 2 and loose ends)

Added 2026-11-07, after Phases 0–8 met every Tier 1 exit criterion (§7). Ordered for execution. Each phase ships as its own commit (and gets an ADR where it's a design decision), so the project stays complete after every phase. Estimated total: ~8–10 working days. The original 1–2 week window (from 2026-10-24) has passed by the calendar; actual effort through Phase 8 was ~3 days.

**Why this order:**
- **v1.0 first:** locks in a complete deliverable.
- **CI and plots next:** CI protects the big refactors, and the plots serve every later ADR.
- **Cheap Tier 2 item early:** the leak report is also a debugging aid for the refactors after it.
- **Segregated lists before the slab:** the slab reuses the size classes.
- **`operator new` after the speed work:** it's the drop-in demo.
- **Thread safety last:** §1's rule for stretch goals, and the item to cut if time runs out.

### Phase 9: Loose ends → `v1.0` (≈ 0.25 d)
- [x] Add `LICENSE` (MIT, Akshat Kankani)
- [x] Update §0 and §9 of this plan: Instruments, ASan and the harness are resolved; add a status line per phase (the §3 table already marks each phase ✅)
- [x] Update the ADR index entry for 011: "planned, Phase 13" instead of "decision pending"
- [ ] Commit and `git tag v1.0` (the Tier 1 deliverable)

### Phase 10: CI pipeline (≈ 0.5–1 d)
Before the big refactors, so it catches regressions during them.
- [x] GitHub Actions job: Debug + UBSan, both test binaries (first-fit and best-fit)
- [x] ASan + UBSan job
- [x] Release build + footprint-probe determinism check (output must match the committed CSVs)
- [x] Linux build + tests job (checks the "POSIX-portable" claim; benchmark-only macOS calls guarded)
- [x] Coverage job (`llvm-cov` report as an artifact)
- [x] Optional: `.clang-format` + blocking format check (one reformat commit): clang-format 22.1.8 pinned; config chosen for least churn (+160/−101 lines)
- [x] README: update the CI row and the Code Quality section

### Phase 11: Plotting script (≈ 0.5 d)
Before the new features, so every later ADR gets charts.
- [x] `bench/scripts/plot.py`: timing per version and allocator, footprint per policy (plus the hardening and bottleneck charts in ADR-012/010; light + dark)
- [x] Charts in `docs/results/` and linked from the README

### Phase 12: Allocation tracking / leak report, Tier 2 (≈ 0.5 d), ADR-013
- [ ] C API: `my_alloc_stats()` (bytes outstanding, peak, live blocks, free blocks, high-water)
- [ ] `my_report_leaks(FILE*)`: walks the heap and lists live blocks (address, size) plus totals
- [ ] Unit and integration tests (leak reported, then zero after freeing; peak tracking)
- [ ] ADR-013 + README

### Phase 13: Segregated free lists (≈ 1.5–2 d), ADR-011
- [ ] Size-class design: classes plus a bitmap of non-empty classes; search from the request's class upward
- [ ] Implement it as a third policy (`SegregatedFit`), so first-fit and best-fit stay comparable
- [ ] Integrate the use-after-free hardening (per-class lists, the same validation)
- [ ] The whole test suite builds and runs under all three policies; mutation checks
- [ ] Footprint probe, timing and profile against first-fit, best-fit and libmalloc. Targets: the adversarial gap (53×) and best-fit-level memory without its search cost
- [ ] Re-check the ADR-007 realloc result under the new policy
- [ ] Decide the default policy from the data; ADR-011

### Phase 14: Small-object slab fast path, Tier 2 (≈ 2 d), ADR-014
Builds on Phase 13's size classes.
- [ ] Fixed-size slabs for small classes (e.g. ≤ 256 B), carved from the arena
- [ ] Free dispatch: slab vs. general block, identified cheaply and safely
- [ ] Hardening for slab free lists (use-after-free must still never crash)
- [ ] Tests, fuzz and mutation checks
- [ ] Benchmark app-like against libmalloc with Nano on and off. Target: close the measured 1.14× gap; profile to confirm
- [ ] ADR-014

### Phase 15: `operator new`/`delete` + STL allocator, Tier 2 (≈ 0.5–1 d), ADR-015
Once the allocator is at its fastest.
- [ ] Public `myalloc::allocator<T>` header (promoted from the test-only adapter)
- [ ] Opt-in global `operator new`/`delete` replacement as a separate library target, covering the sized, aligned (`align_val_t`) and `nothrow` variants
- [ ] A separate test binary for the global replacement, plus a demo using STL containers
- [ ] ADR-015

### Phase 16: Thread safety, Tier 2 (≈ 1.5–2 d), ADR-016
Last, as §1 requires. If time runs short, this is the first thing to cut.
- [ ] Multithreaded benchmark harness (N threads, per-thread traces, throughput and scaling)
- [ ] Step 1: global-lock wrapper; measure contention and scaling against libmalloc
- [ ] ThreadSanitizer job (local and CI)
- [ ] Step 2, decided by the data: per-thread caches or arenas, or stop at the lock and document why
- [ ] ADR-016

### Phase 17: Final pass → `v2.0` (≈ 0.5 d)
- [ ] Re-run the final comparison (all versions and policies plus libmalloc), coverage, and the profiles where they changed
- [ ] Update the README: Features, Comparative Study, Bottlenecks, Limitations, Testing, plus the charts
- [ ] ADR consistency pass, then refresh the resume bullets with the new results
- [ ] Commit and `git tag v2.0`
