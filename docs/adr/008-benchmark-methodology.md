# ADR-008: Benchmark methodology — trace replay, batch timing, controlled baseline

- Status: Accepted. Results: [results/timing-summary.md](../results/timing-summary.md), raw data in [results/](../results/).
- Date: 2026-10-24 (methodology), 2026-10-25 (final native runs)
- Environment: see [../environment.md](../environment.md). Timing builds: Xcode Apple clang 21, `-O3 -DNDEBUG`, **native arm64**, no sanitizers, no `check()` calls. Machine state per run: [results/timing-machine-state.txt](../results/timing-machine-state.txt).
- Code: [bench/bench_main.cpp](../../bench/bench_main.cpp), [bench/workloads.cpp](../../bench/workloads.cpp), [bench/harness.h](../../bench/harness.h), [bench/scripts/](../../bench/scripts/)

## Context
ADR-005 to ADR-007 have deterministic *footprint* numbers but no *timing*. Phase 5 adds wall-clock comparisons against the macOS system allocator (libmalloc) and between the phase commits. Timing on this platform has specific traps:

- **Timer resolution.** `CLOCK_UPTIME_RAW` / `mach_absolute_time` tick at 24 MHz on Apple Silicon (~41.7 ns), comparable to the cost of one malloc. Single-call timing is meaningless.
- **No CPU pinning** on macOS, and the M2 has performance and efficiency cores.
- **Unequal starting states.** A fresh mmap'd arena pays a first-touch page fault per 16 KiB page. libmalloc, running in a warm process, mostly does not.
- **Hidden configuration of the baseline.** libmalloc behaviour depends on environment variables (Decision 6).
- **Hidden build configuration.** See "Incidents" below: the first final run was silently built for x86_64.
- **Comparing phases** requires running the same harness against older commits whose API is smaller (no `realloc` before Phase 4, no `free_summary` before Phase 2).

## Decision
1. **Pre-generated traces.** Each workload is a seeded vector of `{Alloc, Free, Realloc}` ops on numbered slots, generated before timing. RNG cost is never timed, and every allocator replays the identical sequence. Every trace frees all its blocks at the end.
2. **Batch timing.** Ops are timed in batches of 1,024. Reported per run: total ns ÷ ops. Across all batches of all reps: p50 and p99 of batch ns/op. p99 here means "the slowest 1% of 1,024-op windows", not single-call tail latency, which this timer cannot resolve.
3. **Repetitions and statistic.** One untimed warm-up run, then 15 timed reps. The headline number is the **median** ns/op across reps, with spread reported as **IQR ÷ median**. Rows with IQR > 10% are marked *noisy*. Max − min is also kept in the CSVs but not used for that threshold: on this machine it is dominated by 1–3 outlier reps per row (e.g. one row had max − min = 55% with IQR = 1.2%). [bench/scripts/summarize.py](../../bench/scripts/summarize.py) produces the summary from the raw per-rep CSVs.
4. **Equal page-fault treatment.** Each `Heap` rep uses a fresh 256 MiB arena, pre-faulted up to the high-water mark that the warm-up run of the same trace reached. The prefault writes 0 at offset 64 of each page, which is zero payload in a fresh heap in every phase, so no allocator state changes. libmalloc gets its natural warm state from its own warm-up run.
5. **Touch on allocation.** After each successful malloc/realloc the harness writes the first 8 bytes, as a real caller would. It does not touch whole blocks: that would mostly measure page faults and memset bandwidth rather than the allocator.
6. **Controlled libmalloc baseline.** libmalloc serves small requests (≤ 256 B) from its **Nano zone**, which `MallocNanoZone=0` disables. The system allocator runs twice: `system` (Nano on, the macOS default) and `system_nano_off` (control). The scripts set the variable explicitly on every run, because this development environment (launched from VS Code) already exports `MallocNanoZone=0`. It is not set in any shell rc file or `launchctl`, so it is inherited from the parent process. The first smoke run silently measured the control configuration as if it were the default; the harness's labelling caught it.
7. **Phase comparison without touching git.** `run_phases.sh` extracts each phase commit's `src/` and `include/` with `git archive` and compiles the *current* harness against it with the same flags as the CMake Release build (`-O3 -DNDEBUG -arch arm64`). The harness uses only API every phase has, and detects `Heap::realloc` at compile time (SFINAE on a dependent heap type), so the realloc workload runs only for Phase 4 onwards. **Phase 2 is excluded by default** (see Consequences).
8. **realloc policy comparison** (for ADR-007) runs in the harness on the same `Heap`: `heap` (in-place, ADR-007), `heap_naive_realloc` (always malloc + copy + free) and `heap_growonly_realloc` (move on shrink).
9. **Build guard.** `bench_timing` refuses to run under Rosetta translation (`sysctl.proc_translated`), and CMake forces `arm64` on Apple Silicon. See Incidents.
10. **Comparison rule.** Compare only within one run (one session). Allocator vs. allocator rows come from the same `run_all.sh` invocation; phase vs. phase rows from the same `run_phases.sh` invocation. Across separate runs the same binary drifted by up to ~10% (see Consequences), so a cross-run difference under 10% is not treated as real.
11. **Run protocol:** AC power, Low Power Mode off, other load minimised, and the machine state recorded at the start and end of every run.

### Workloads
| Name | Ops | Shape | Purpose |
|---|---|---|---|
| `random_16B_64KiB` | 1.0 M | log-uniform 16 B–64 KiB, ~2,000 live, random frees | general mix |
| `app_like` | 1.0 M | 95% 16–256 B, 5% 4–256 KiB, ~3,000 live | typical application |
| `adversarial_holes` | 0.42 M | see below | worst case for LIFO first-fit |
| `realloc_growth` | 0.51 M | buffers doubling to 64 KiB, plus 16–256 B noise | realloc policy (ADR-007) |

**`adversarial_holes`, and why the first design was wrong.** The first version followed the plan literally: alternate small and large blocks, free the large ones so they cannot coalesce, then request blocks larger than any hole. The heap did it faster than libmalloc, so it was not adversarial. The reason: the first large request does walk all 2,000 holes, but it then splits the arena tail, and `place()` pushes the remainder to the **head** of the LIFO list. Every later request finds the remainder immediately, so the walk cost is amortised over the whole round. **LIFO + remainder-at-head makes repeated non-fitting requests cheap.** The redesigned workload defeats that: 1,000 exact-fit 4,000 B blocks are freed *first*, then 2,000 non-fitting 1–2 KiB holes, all pinned apart by live 32 B blocks. Each 4,000 B request walks every hole and then takes a fit block **exactly**, so no remainder is pushed, and the next request walks all the holes again.

## Alternatives considered
1. **Google Benchmark.** Good statistics and a familiar output format, but its per-iteration model fits poorly with replaying a stateful trace, and the other portfolio projects use custom wall-clock harnesses. Rejected for consistency (plan §4).
2. **Generating ops inline with the RNG.** Simpler, but RNG time would be included and the op sequence would depend on allocation outcomes (failures). Rejected.
3. **Per-call timing with `mach_absolute_time`.** About 41.7 ns resolution, so most calls would read as 0 or 1 tick. Rejected; batch percentiles are reported instead.
4. **Reusing one `Heap` across reps.** Avoids page faults naturally, but Phases 1–2 never return to a pristine state (no coalescing), so later reps would measure a different heap than the first. Rejected in favour of fresh heap + prefault.
5. **Touching whole blocks.** More realistic for some programs, but on 64–256 KiB blocks it is dominated by memory bandwidth and faults, which hides the allocator. Rejected (Decision 5); noted as a limitation.
6. **git worktrees for old phases.** Works, but modifies `.git` metadata. `git archive` is read-only. Rejected.
7. **Max − min spread as the noise criterion.** The original plan. Rejected after the data showed it measures outlier reps, not the stability of the median (Decision 3).

## Results (native arm64, 2026-10-25)

> These are **pre-hardening** numbers (commit `c27e2d8`). After ADR-012 the heap does more validation per operation; current numbers vs. libmalloc are in [ADR-012](012-use-after-free-hardening.md) ("Final numbers vs. libmalloc"). The methodology and the phase-to-phase comparison are unaffected.

### This allocator vs. libmalloc (one run, 00:54; median ns/op, IQR all ≤ 4.3%)
| Workload | libmalloc (Nano on, default) | libmalloc, Nano off | **This heap** | Heap vs. default libmalloc | Batch p99, libmalloc → heap |
|---|---|---|---|---|---|
| random 16 B–64 KiB | 28.44 | 29.89 | **16.18** | **1.76× faster** | 33.9 → 40.5 ns |
| app-like | 18.69 | 21.92 | **16.74** | **1.12× faster** | 23.0 → 42.2 ns |
| adversarial holes | 12.69 | 14.23 | **673.62** | **53× slower** | 34.3 → 8,167 ns |
| realloc growth | 37.75 | 43.49 | **27.71** | **1.36× faster** | 59.6 → 48.0 ns |

What this does and doesn't show:
- **On typical mixes the heap is faster in the median but has a worse tail.** p99 batch cost is higher than libmalloc's on random and app-like (42 vs. 23 ns on app-like), the signature of occasional long first-fit walks that libmalloc's size-segregated structures don't do.
- **The adversarial workload is bimodal:** the heap's *median batch* is 6.7 ns (cheaper than libmalloc's 12.3), but batches containing hole walks cost ~8 µs/op. One O(free-list) search per 4,000 B request dominates the total. libmalloc is flat because it never looks at blocks of the wrong size class. This is the evidence for size-segregated free lists (ADR-011) and the test case for best-fit (ADR-009).
- **The speed advantage is partly not design.** libmalloc is thread-safe (locking, per-CPU magazines) and runs extra hardening checks; this heap is single-threaded and does none of that. A fair claim is "a single-threaded first-fit heap with O(1) free is cheaper per op than a general-purpose thread-safe allocator on these workloads", not "faster than malloc". The profiler (ADR-010) is needed to say how much of the gap is which.
- **Nano zone control:** turning the Nano zone off costs libmalloc 5% (random), 17% (app-like), 12% (adversarial), 15% (realloc). The largest effect is on the small-object workload, as expected for a ≤ 256 B fast path. This confirms that `MallocNanoZone=0` works on macOS 26.5.2 and closes the open item from plan §4. Even with Nano off, the heap's median lead on app-like stays (16.74 vs. 21.92).

### Phase vs. phase (one run, `run_phases.sh -O3`; median ns/op)
| Workload | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
|---|---|---|---|---|
| random 16 B–64 KiB | 15.36 (p99 96.2) | **not measurable** | 17.58 (p99 49.1) | 17.80 (p99 53.5, noisy: IQR 12.6%) |
| app-like | 26.45, **436 OOM/rep, not comparable** | not measurable | 18.07 | 17.47 |
| adversarial holes | 2,008.7 | not measurable | 709.2 | 694.1 |
| realloc growth | – | – | – | 30.37 |

Analysis per phase is in the ADR each phase belongs to (ADR-005, 006, 007).

## Consequences
- The numbers measure allocator bookkeeping plus one cache line of payload per allocation. They are not end-to-end application speed.
- Single-call tail latency is not observable. Batch p99 shows long stalls (e.g. a 2,000-hole walk) but averages them over 1,024 ops.
- libmalloc is thread-safe and pays for it, while this allocator is single-threaded. Stated next to every comparison.
- libmalloc can return memory to the OS during a run; this allocator never does (ADR-003). This affects page-fault behaviour, not footprint numbers, which come from the probe.
- **Phase 2 is not timeable.** A run was killed after 80 minutes still inside the first workload's first rep. Without coalescing, the free list grows with every split and first-fit walks it on every allocation, so run time grows roughly quadratically in the op count. That is the time-domain counterpart of ADR-005's 107,655-piece free list, and is recorded as the Phase 2 timing result.
- **Cross-run drift up to ~10%.** The same allocator code with the same flags (`-O3`) measured 16.18 / 16.74 / 27.71 / 673.6 ns/op (random / app-like / realloc / adversarial) in run 1 as "current", and 17.80 / 17.47 / 30.37 / 694.1 in run 2 as "Phase 4", about 10 minutes later. That is +10%, +4%, +10% and +3%. Hence Decision 10.
- **Build-flag sensitivity.** `adversarial_holes` on identical Phase 4 code measured 739.6 ns/op at `-O2` vs. 673.6 at `-O3` (IQR 2–3% each). The probe shows identical memory behaviour, so it is code generation in the search loop. Phase builds now use the CMake flags (Decision 7); the remaining Phase 4 vs. current difference is 694 vs. 674 (3%), inside cross-run drift. (The `-O2` comparison was made within one session, so it is not drift.)

## Incidents during measurement (kept for the record)
1. **Inherited `MallocNanoZone=0`** (Decision 6): would have mislabelled the control as the default. Caught by the harness labelling the run `system_nano_off`.
2. **x86_64 builds under Rosetta.** `/usr/local/bin/cmake` is an x86_64 binary, so every CMake build targeted x86_64 and ran translated. That includes the first "final" run after the restart, where *both* allocators ran under Rosetta. Caught because Phase 4 (built natively by the phase script) and the current tree (same code, built by CMake) timed 1.3–1.8× apart. Those results were discarded; CMake now forces arm64 and the harness refuses to run translated (Decision 9). Details in [environment.md](../environment.md). Tests and footprint numbers were re-run natively: 78/78 pass (UBSan; ASan + UBSan), and probe output is byte-identical.
3. **Noisy machine.** The first attempts ran with `BTLEServer` pinned at ~98% of a core, `cloudd` at ~91% and Docker running. Fixed by a restart. The final runs had no other process above ~26% of a core (the Claude Code extension), with load average ~3.4–4 (recorded).
4. **Preliminary numbers retracted.** An earlier draft of this ADR quoted adversarial ~790 ns/op and a Nano effect of ~24 vs. ~45 ns/op on app-like. Those came from the Rosetta builds. The native Nano effect is much smaller (18.7 vs. 21.9).

## Validation
- `adversarial_holes` produces the intended signature natively: heap 673.6 ns/op with batch p99 8.2 µs, libmalloc 12.7 ns/op with p99 34 ns. That is an O(free-list) search against size-segregated bins.
- Both binaries in every result set were checked to be arm64 (`file`), and the harness exits with code 3 when built with `-arch x86_64` (tested).
- Across the two final runs (24 rows): IQR ≤ 5% on 19 rows, ≤ 10% on 22, and 2 rows are marked noisy in [timing-summary.md](../results/timing-summary.md) (`heap_growonly_realloc` 12.2%, Phase 4 random 12.6%). Neither noisy row is used for a conclusion on its own.
