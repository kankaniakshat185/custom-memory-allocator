# ADR-011: Segregated free lists, now the default policy

- Status: Accepted. `SegregatedFit` is the default policy of `myalloc::Heap` and the C API; first-fit and best-fit remain compile-time options (`MYALLOC_FIRST_FIT`, `MYALLOC_BEST_FIT`).
- Date: 2026-11-08
- Environment: see [../environment.md](../environment.md); native arm64, `-O3`
- Raw data: [results/segfit/](../results/segfit/) (see its README; **in the timing CSV, `heap` = first-fit**). Profiles: [results/profiles/](../results/profiles/) `ff_*`/`seg_*`.
- Code: `FreeLists`, `SegregatedFit` in [src/heap.h](../../src/heap.h); bin-aware `list_push`/`unlink`/`unlinkable`/`check` in [src/heap.cpp](../../src/heap.cpp)

## Context
ADR-001 deferred segregated lists to "if profiling motivates it". ADR-009 and ADR-010 then measured three problems that a single free list cannot fix, whichever fit policy it uses:

1. **Worst case:** `adversarial_holes` ran 53× slower than libmalloc. 98% of the time went to walking 2,000 holes of the wrong size per request, memory-bound (ADR-010). First-fit and best-fit were equally bad there.
2. **Memory:** first-fit reached only 0.310 utilisation on app-like. Best-fit fixed that (0.849), but at 3–13× the time, because it scans the whole list (ADR-009).
3. **realloc bloat:** in-place realloc reached 2.1× the footprint of the naive policy under first-fit. That was an interaction with first-fit (ADR-007/009).

ADR-010 also predicted what this would *not* fix: average speed on typical workloads, where first-fit's search was already only ~10% of the time.

## Decision
1. **One mechanism for every policy.** The heap holds `FreeLists<N>`: N list heads (one per size bin) and a bitmap of non-empty bins. A policy declares `kBins` and `bin_of(size)`.
   - **First-fit and best-fit are the 1-bin case,** so their behaviour is unchanged: the footprint probe output is byte-identical (CI check).
   - **All list code is bin-aware:** push, unlink, the safe-unlinking head check, the invariant checker and `free_summary`.
2. **`SegregatedFit` bins:**
   - **63 exact bins,** one per 16 B step for blocks of 32 B–1 KiB.
   - **Power-of-two range bins above that:** [1 K, 2 K), [2 K, 4 K), … (128 bins in all, 2 bitmap words).
3. **Search:**
   - Scan the request's own bin first-fit. In an exact bin, the first block always fits; in a range bin, the walk may continue.
   - If nothing fits, `ctz` on the bitmap finds the next non-empty bin, and its first block is taken. Every block in a larger bin is larger than the request, so **the search never visits a block of a smaller size class**.
4. **Hardening is unchanged in substance (ADR-012).** Every node visited is still validated, walks are bounded, and unlinking is safe. The "is this the list head?" check now looks up the head of the bin that the block's own header size maps to; a use-after-free write can't reach headers.
5. **The checker gains two invariants:** every free block sits in the bin its size maps to, and the bitmap matches exactly the non-empty bins.
6. **Default policy: segregated fits,** decided from the measurements below, which were reviewed before switching.

## Alternatives considered
1. **Power-of-two bins only (no exact small bins).** Simpler, but small requests would walk mixed-size bins. Exact bins are what make small-object reuse an exact match (Consequences: 6.7× fewer split/merge cycles on app-like). Rejected.
2. **Two-level bins (TLSF-style): several sub-bins per power of two.** These would shorten the remaining range-bin walks, which are the one measured regression (random sizes, below). **Deferred, with a measured target:** the search is 30% of segregated fit's time on random sizes, against 9% for first-fit.
3. **Best-fit within range bins.** Better placement inside a bin, but it scans the whole bin every time, which is the exact cost ADR-009 measured for best-fit. Rejected.
4. **Size-ordered trees for large blocks (as in dlmalloc).** O(log n) best fit, but a second data structure with its own hardening and invariants. Rejected for this scope.
5. **Rounding requests up to size classes (a slab-style allocator).** That is the next phase (Phase 14, small-object slab), not a replacement for general free lists.
6. **Keep first-fit as the default.** Smallest change, but it keeps the 42× worst-case gap and 2.3× more memory on app-like. Rejected on the data.

## Consequences

### Memory (deterministic footprint probe; utilisation = peak live ÷ high-water)
| Workload | First-fit | Best-fit | **Segregated** |
|---|---|---|---|
| random 16 B–4 KiB | 0.615 | 0.913 | **0.812** |
| app-like | 0.310 | 0.849 | **0.728** (2.3× less memory than first-fit) |
| phase shift | 0.997 | 0.997 | **0.997** |

**realloc (ADR-007 re-check, 64 MiB arena):** in-place high-water is **5.66 MB vs. 5.61 MB for naive (+0.8%)**, against 2.1× under first-fit. It copies 50% fewer bytes than naive, and there is zero OOM. The bloat is gone, as it was under best-fit.

### Search work (free-list nodes visited per malloc, timing traces)
| Workload | First-fit | Best-fit | **Segregated** |
|---|---|---|---|
| adversarial | 286.7 | 286.7 | **7.5** |
| app-like | 2.0 | 57.2 | **1.2** |
| random 16 B–64 KiB | 1.8 | 219.3 | **5.7** |

### Time (one `run_all.sh` run, 15 reps, median ns/op; the machine was under load, so compare within the run only; a clean re-run is scheduled in Phase 17)
| Workload | libmalloc | First-fit | Best-fit | **Segregated** | Segregated vs. first-fit | Segregated vs. libmalloc |
|---|---|---|---|---|---|---|
| random | 34.44 | **24.68** | 259.68 | 26.80 | **+8.6% (slower)** | 1.29× faster |
| app-like | 20.57 | 23.73 (noisy, IQR 12%) | 72.16 | **16.96** | −29% | **1.21× faster** |
| adversarial | **13.07** | 693.55 | 726.47 (noisy) | 16.49 | **42× faster** | 1.26× slower (was 53×) |
| realloc growth | 43.00 | 41.14 | 134.30 | **22.77** | 1.81× faster | **1.89× faster** |

### Why: profiles and counts
- **Adversarial.** Requests for 4,000 B never look at the 1–2 KiB holes, which sit in a lower bin, so 287 nodes per malloc drop to 7.5. This closes ADR-010's headline problem: the worst case is now within 1.3× of libmalloc.
- **App-like: faster because frees got cheaper, not searches.**
  - **Profile** (same binary, 200 reps each): total samples −22.5%. The merge path (`release`) fell **1,575 → 359 samples**, while the search was unchanged (524 → 575).
  - **Why `release` got cheaper:** splits per malloc and merges per free fell from **0.94 to 0.14** ([merge-counts.txt](../results/segfit/merge-counts.txt), a scratch-instrumented copy).
  - **Mechanism:** with exact bins, a freed small block is reused at exactly the size requested, so it is neither split on reuse nor merged on free. First-fit carves small requests from whatever block sits at the list head, so nearly every malloc splits and nearly every free merges.
  - **Against ADR-010's expectation:** ADR-010 predicted segregated lists would not help average speed, because search was only ~10%. That prediction was **wrong**. The gain comes from the free path, which ADR-010 had identified as the cost (~51%), through a mechanism it didn't anticipate.
- **Random: slower because range bins are walked.** Search samples rose **427 → 1,832**, more than the whole difference (+1,151). About 60% of these requests exceed 1 KiB and land in power-of-two range bins of mixed sizes, which are walked first-fit (5.7 nodes vs. 1.8). Splits and merges fell 1.00 → 0.54, which offsets part of it (`release` 1,533 → 971). Alternative 2 (two-level bins) is the targeted fix.

### Other consequences
- **Default switched.** The C API, `myalloc::Heap` and the main test binary now use segregated fits. The test binaries are `myalloc_tests` (segregated), `myalloc_tests_firstfit` and `myalloc_tests_bestfit`, all in CI. The harness label `heap` means "this tree's default policy": first-fit for Phase 1–12 commits, segregated fits from here on. `heap_firstfit` is explicit.
- **Footprint regression check:** now covers all three policies (6 reference outputs).
- **Size of the bin array:** each heap carries 128 list heads plus 2 bitmap words (~1 KiB), on the heap object, not in the arena.
- **The README and charts still lead with first-fit numbers** from the v1.0 final run. They're refreshed in Phase 17 together with the clean timing re-run.

## Validation
- **The suite runs under all three policies:** 108 tests × 3 = **324 runs**, all passing under UBSan and under ASan + UBSan, and each binary passes when run directly. On Linux (Ubuntu 24.04 container, GCC 13 and Clang 18) all 324 pass as well, and **all six footprint reference outputs are byte-identical on Linux too**.
- **Hardening tests made policy-agnostic.** Eight use-after-free tests originally planted the corrupted link where *first-fit's* walk would read it. Under segregated fits that block is in a different bin and never visited, so the tests failed by layout, not by an allocator bug. They now use two holes in the same size class (1,100 B and 1,500 B, both in the 1–2 KiB range bin) and a 1,400 B request that must walk past the first, so the corrupted link is read under every policy. The use-after-free fuzz test under segregated fits: 232 corruptions detected, 66 rejected, 2 harmless, no crash or hang.
- **Mutation checks** (2026-11-08, sources restored):
  - starting the bitmap search at the request's own bin instead of the next one fails 5 tests (an undersized block is handed out: `footer disagrees with header`);
  - not clearing an emptied bin's bit fails 30 tests (`non-empty-bin bitmap disagrees with bin`).
- **First-fit and best-fit unchanged:** after the move to bins, their footprint and realloc probe outputs are byte-identical to Phase 6, checked by CI.
