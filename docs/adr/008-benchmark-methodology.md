# ADR-008: Benchmark methodology — trace replay, batch timing, controlled baseline

- Status: Accepted (methodology). Results are recorded in [../results/](../results/) and quoted by the ADRs they inform.
- Date: 2026-10-24
- Environment: see [../environment.md](../environment.md). Timing builds use Xcode's Apple clang 21 with `-O2 -DNDEBUG`, no sanitizers, no `check()` calls.
- Code: [bench/bench_main.cpp](../../bench/bench_main.cpp), [bench/workloads.cpp](../../bench/workloads.cpp), [bench/harness.h](../../bench/harness.h), [bench/scripts/](../../bench/scripts/)

## Context
ADR-005 to ADR-007 have deterministic *footprint* numbers but no *timing*. Phase 5 adds wall-clock comparisons against the macOS system allocator (libmalloc) and between the phase commits. Timing on this platform has specific traps:

- **Timer resolution.** `CLOCK_UPTIME_RAW` / `mach_absolute_time` tick at 24 MHz on Apple Silicon (~41.7 ns), comparable to the cost of one malloc. Single-call timing is meaningless.
- **No CPU pinning** on macOS, and the M2 has performance and efficiency cores.
- **Unequal starting states.** A fresh mmap'd arena pays a first-touch page fault per 16 KiB page. libmalloc, running in a warm process, mostly does not.
- **Hidden configuration of the baseline.** libmalloc behaviour depends on environment variables (see Decision 6).
- **Comparing phases** requires running the same harness against older commits whose API is smaller (no `realloc` before Phase 4, no `free_summary` before Phase 2).

## Decision
1. **Pre-generated traces.** Each workload is a seeded vector of `{Alloc, Free, Realloc}` ops on numbered slots, generated before timing. RNG cost is never timed, and every allocator replays the identical sequence. Every trace frees all its blocks at the end.
2. **Batch timing.** Ops are timed in batches of 1,024. Reported per run: total ns ÷ ops. Across all batches of all reps: p50 and p99 of batch ns/op. p99 here means "the slowest 1% of 1,024-op windows", not single-call tail latency, which this timer cannot resolve.
3. **Repetitions.** One untimed warm-up run, then 15 timed reps. Reported: min / median / max of per-run ns/op, and the spread `(max − min) / median`. Raw per-rep values go to a `*-raw.csv` file.
4. **Equal page-fault treatment.** Each `Heap` rep uses a fresh 256 MiB arena, pre-faulted up to the high-water mark that the warm-up run of the same trace reached. The prefault writes 0 at offset 64 of each page, which is zero payload in a fresh heap in every phase, so no allocator state changes. libmalloc gets its natural warm state from its own warm-up run.
5. **Touch on allocation.** After each successful malloc/realloc the harness writes the first 8 bytes, as a real caller would. It does not touch whole blocks: that would mostly measure page faults and memset bandwidth rather than the allocator.
6. **Controlled libmalloc baseline.** libmalloc serves small requests (≤ 256 B) from its **Nano zone**, which `MallocNanoZone=0` disables. The system allocator runs twice: `system` (Nano on, the macOS default) and `system_nano_off` (control). Both scripts set the variable explicitly on every run. **Why that matters:** this development environment (launched from VS Code) already exports `MallocNanoZone=0`. It is not set in any shell rc file or `launchctl`, so it is inherited from the parent process. A benchmark started from this terminal without the explicit setting would silently measure the control configuration and label it as the default. The first smoke run did exactly that, and the harness's labelling caught it.
7. **Phase comparison without touching git.** `run_phases.sh` extracts each phase commit's `src/` and `include/` with `git archive` into a temp directory and compiles the *current* harness against it. The harness uses only API every phase has, and detects `Heap::realloc` at compile time (SFINAE), so the realloc workload runs only for Phase 4 onwards. **Phase 2 runs 3 reps, not 15.** Without coalescing its free list grows without bound, so one adversarial rep takes minutes (a preliminary 15-rep run was still on Phase 2 after 5 minutes). It is a known-broken intermediate state whose timing only needs to show the trend. Phase 1's `app_like` row has hundreds of OOM failures per rep even in 256 MiB, and failed allocations return early, so that row is not comparable as a speed and is marked as such wherever it is quoted.
8. **realloc policy comparison** (for ADR-007) runs in the harness on the same `Heap`: `heap` (in-place, ADR-007), `heap_naive_realloc` (always malloc + copy + free) and `heap_growonly_realloc` (move on shrink).
9. **Run protocol:** AC power, Low Power Mode off, other load minimised, and the machine state recorded with each result set. A result set whose spread exceeds 10% on any row is labelled *preliminary*.

### Workloads
| Name | Ops | Shape | Purpose |
|---|---|---|---|
| `random_16B_64KiB` | 1.0 M | log-uniform 16 B–64 KiB, ~2,000 live, random frees | general mix |
| `app_like` | 1.0 M | 95% 16–256 B, 5% 4–256 KiB, ~3,000 live | typical application |
| `adversarial_holes` | 0.42 M | see below | worst case for LIFO first-fit |
| `realloc_growth` | 0.51 M | buffers doubling to 64 KiB, plus 16–256 B noise | realloc policy (ADR-007) |

**`adversarial_holes`, and why the first design was wrong.** The first version followed the plan literally: alternate small and large blocks, free the large ones so they cannot coalesce, then request blocks larger than any hole. The heap did it at 15.6 ns/op, faster than libmalloc, so it was not adversarial. The reason: the first large request does walk all 2,000 holes, but it then splits the arena tail, and `place()` pushes the remainder to the **head** of the LIFO list. Every later request finds the remainder immediately, so the walk cost is amortised over the whole round. **LIFO + remainder-at-head makes repeated non-fitting requests cheap.** The redesigned workload defeats that: 1,000 exact-fit 4,000 B blocks are freed *first*, then 2,000 non-fitting 1–2 KiB holes, all pinned apart by live 32 B blocks. Each 4,000 B request walks every hole and then takes a fit block **exactly**, so no remainder is pushed, and the next request walks all the holes again.

## Alternatives considered
1. **Google Benchmark.** Good statistics and a familiar output format, but its per-iteration model fits poorly with replaying a stateful trace, and the other portfolio projects use custom wall-clock harnesses. Rejected for consistency (plan §4).
2. **Generating ops inline with the RNG.** Simpler, but RNG time would be included and the op sequence would depend on allocation outcomes (failures). Rejected.
3. **Per-call timing with `mach_absolute_time`.** About 41.7 ns resolution, so most calls would read as 0 or 1 tick. Rejected; batch percentiles are reported instead.
4. **Reusing one `Heap` across reps.** Avoids page faults naturally, but Phases 1–2 never return to a pristine state (no coalescing), so later reps would measure a different heap than the first. Rejected in favour of fresh heap + prefault.
5. **Touching whole blocks.** More realistic for some programs, but on 64–256 KiB blocks it is dominated by memory bandwidth and faults, which hides the allocator. Rejected (Decision 5); noted as a limitation.
6. **git worktrees for old phases.** Works, but modifies `.git` metadata. `git archive` is read-only. Rejected.

## Consequences
- The numbers measure allocator bookkeeping plus one cache line of payload per allocation. They are not end-to-end application speed.
- Single-call tail latency is not observable. Batch p99 shows long stalls (e.g. a 2,000-hole walk) but averages them over 1,024 ops.
- libmalloc is thread-safe and pays for locking or atomics, while this allocator is single-threaded. Part of any gap in this allocator's favour is that cost, not better design. Stated next to every comparison.
- libmalloc can return memory to the OS during a run; this allocator never does (ADR-003). This affects its page-fault behaviour, not footprint numbers, which come from the probe.
- **Measurement noise on this machine is real.** The preliminary run (2026-10-24, 22:46) was taken with load average ~5.6: `BTLEServer` at ~98% CPU, `cloudd` at ~91%, Docker running. Spreads reached 26–75% on some rows, so those numbers are preliminary only and are not quoted as results.

## Validation
- Smoke-checked that a workload change shows up as expected: `adversarial_holes` takes the heap from ~16 to ~790 ns/op, while libmalloc stays at ~25–37 ns/op, the expected signature of an O(free-list) search against size-segregated bins.
- The Nano zone control has a large, consistent effect on small-object workloads (`app_like`: ~24 ns/op with Nano on vs. ~45 with it off), so `MallocNanoZone=0` is confirmed to work on macOS 26.5.2. That resolves the open item from plan §4.
