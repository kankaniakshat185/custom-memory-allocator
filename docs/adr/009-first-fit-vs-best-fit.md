# ADR-009: Fit policy — first-fit vs. best-fit, chosen at compile time

- Status: Accepted. First-fit stays the default; best-fit available via `MYALLOC_BEST_FIT` / `myalloc::BestFitHeap`.
- Date: 2026-10-25
- Environment: see [../environment.md](../environment.md); native arm64, `-O3`. Machine state: [results/phase6/timing-machine-state.txt](../results/phase6/timing-machine-state.txt)
- Raw data: [results/phase6/](../results/phase6/) (footprint, realloc probe and timing for both policies, plus [timing-summary.md](../results/phase6/timing-summary.md))
- Code: `FirstFit`, `BestFit`, `BasicHeap<Fit>` in [src/heap.h](../../src/heap.h)

## Context
Two open questions from earlier ADRs depend on the fit policy:
- **ADR-006:** app-like utilisation was only 0.310 under first-fit (15.4 MB of high-water for 4.8 MB of peak live data). The hypothesis was that LIFO first-fit carves small requests out of large holes.
- **ADR-007:** in-place realloc reached 2.1× the naive policy's footprint under first-fit, and grew without bound. The hypothesis was an interaction with the fit policy.

The plan also called for a first-fit vs. best-fit comparison in the variant style of the other portfolio projects.

## Decision
1. **The policy is a compile-time template parameter:** `BasicHeap<FirstFit>` and `BasicHeap<BestFit>`, explicitly instantiated in `heap.cpp`. No runtime branch in the search loop. `myalloc::Heap` aliases the default (first-fit, or best-fit when built with `MYALLOC_BEST_FIT`).
2. **Best-fit:** scan the whole free list for the smallest block ≥ `need`, stopping early on an exact match. Ties go to the first in list order (LIFO = most recently freed).
3. **Search-cost counter:** both policies add the number of list nodes they visit to `stats().search_steps`. This is the "free-list length" metric the plan called for, measured as work done rather than sampled length.
4. **Default stays first-fit.** Neither policy dominates (Consequences): best-fit buys 2.7× less memory on app-like at 3× the time, and is 13× slower on random sizes, 7× slower than libmalloc there. A default that loses badly to libmalloc on speed on a general workload is the worse failure for a general-purpose allocator. The memory problem first-fit leaves is the one segregated fits (ADR-011) is designed to fix without that speed cost.
5. **Both policies are tested:** the whole suite builds twice (`myalloc_tests`, `myalloc_tests_bestfit`), the C API included.

## Alternatives considered
1. **Runtime policy selection (function pointer or enum switch).** Simpler to benchmark from one binary, but it puts a branch or an indirect call in the hottest loop and makes the cost measurements less clean. Rejected; plan §3 specified compile-time.
2. **Next-fit (resume the search where the last one ended).** Cheaper searches than first-fit on some workloads, but known from the literature (Wilson et al., 1995) to fragment *worse* than first-fit, which is the opposite of what the open questions need. Rejected.
3. **Address-ordered first-fit.** Johnstone & Wilson (1998, "The Memory Fragmentation Problem: Solved?") report fragmentation close to best-fit for it. But insertion into an address-ordered list is O(free blocks) on every `free`, turning ADR-006's O(1) free into O(n). Rejected for now; a tree or segregated structure would be needed to make it practical.
4. **Best-fit with bounded search (stop after k candidates).** A tunable middle ground, but it adds a magic constant whose best value is workload-specific, and it is a weaker version of what segregated fits does structurally. Rejected.
5. **Make best-fit the default.** Defensible on memory: 0.849 vs. 0.310 utilisation on app-like. Rejected on speed: 207 vs. 16 ns/op on random sizes, and 207 vs. 29 for libmalloc. The memory problem is better solved structurally (ADR-011) than by paying a full-list scan per allocation.

## Consequences

### Memory (footprint probe, deterministic)
| Workload | Utilisation FF → **BF** | High-water FF → **BF** | Free blocks at end FF → BF | **Avg list nodes visited per malloc** FF → BF |
|---|---|---|---|---|
| random 16 B–4 KiB | 0.615 → **0.913** | 3.03 → **2.04 MB** | 1,245 → 255 | 3.1 → **109.0** |
| app-like | 0.310 → **0.849** | 15.40 → **5.62 MB** | 1,626 → 184 | 2.4 → **49.6** |
| phase shift | 0.997 → 0.997 | 2.29 → 2.29 MB | 1 → 1 | 1.0 → 1.0 |

Internal overhead is unchanged (1.01–1.02×). Both policies split the same way; only placement differs.

### realloc (probe, `realloc_growth`, 64 MiB arena)
| realloc policy | High-water, first-fit | High-water, **best-fit** | Bytes copied, best-fit |
|---|---|---|---|
| naive (always move) | 29.19 MB | 5.60 MB | 155.0 MB |
| **in-place (ADR-007)** | **62.17 MB (2.1× naive)** | **5.67 MB (+1% vs. naive)** | 70.4 MB (−55%) |
| grow-only | 54.71 MB | 5.61 MB | 61.7 MB |

Under best-fit, in-place realloc's footprint is within 1% of naive, identical in the 32 and 64 MiB arenas (so bounded), with zero OOM. It still copies 55% fewer bytes. Its in-place hit rate falls from ~50% to 28%, because best-fit packs blocks more tightly, so a buffer's successor is less often free.

### Time (one run, median ns/op, IQR ≤ 4.9% on every row)
| Workload | libmalloc (default) | **First-fit** | **Best-fit** | BF ÷ FF | Batch p99 FF → BF |
|---|---|---|---|---|---|
| random 16 B–64 KiB | 28.66 | **16.14** | 206.95 | **12.8×** | 26.7 → 304.6 ns |
| app-like | 19.02 | **17.04** | 50.61 | **3.0×** | 27.4 → 69.8 ns |
| adversarial holes | 12.34 | 629.69 | 598.84 | 0.95× | 7.6 → 7.3 µs |
| realloc growth | 37.73 | **26.21** | 96.87 | 3.7× | 41.2 → 316.2 ns |

### What the numbers mean
- **ADR-006 hypothesis, "LIFO first-fit wastes memory by carving small requests out of large holes": supported.** Changing *only* the placement policy takes app-like utilisation from 0.310 to 0.849 and cuts the free-block count 9×.
- **ADR-007 hypothesis, "the realloc bloat is a fit-policy interaction, not a property of in-place realloc": supported.** Under best-fit the in-place vs. naive footprint gap shrinks from 2.1× to 1.01×. In-place realloc stays, unconditionally (ADR-007 updated). The exact mechanism under first-fit is still not confirmed, but the decision no longer depends on it.
- **The cost is search work, and it is measured, not inferred.** Best-fit visits 35× more list nodes per malloc on random sizes and 21× more on app-like (probe), and is 12.8× and 3.0× slower in the timing run. The probe workloads are smaller than the timing traces (4 KiB vs. 64 KiB maximum on random), so the step ratios and time ratios are not expected to match exactly, only to rank the same way. They do.
- **Adversarial: identical for both policies (630 vs. 599 ns/op).** By construction, each request walks all 2,000 non-fitting holes and then finds an exact 4,000 B match. First-fit takes it as the first fit, and best-fit stops on the exact match. Neither policy can avoid looking at blocks of the wrong size. Only a size-indexed structure can, and that is what libmalloc does (12.3 ns/op).
- **Neither policy dominates.** First-fit is fast but up to 2.7× hungrier; best-fit is compact but 3–13× slower, and 7× slower than libmalloc on random sizes. This is the classic trade-off from the literature, reproduced on this allocator with measured search cost.
- **The search counter's own cost:** this run's first-fit times (16.14 / 17.04 / 26.21) are within cross-run drift of Phase 5's counter-free build (16.18 / 16.74 / 27.71), so its cost is below what this setup can resolve (ADR-008 Decision 10).

### What it implies
Segregated size-class free lists (ADR-011) are the standard way to get best-fit-like placement without a full-list scan: a request only searches lists of blocks big enough for it. The measured evidence for them is now concrete: the 2.7× footprint gap under first-fit, the 13× time gap under best-fit, and the 51× adversarial gap to libmalloc that *neither* policy can close. Building them is a scope decision (stretch tier). See ADR-011's entry in the index.

## Validation
- New unit suite [tests/test_fit_policy.cpp](../../tests/test_fit_policy.cpp) (8 tests): first-fit takes the first listed fit; best-fit takes the smallest fit across several holes (and prefers a medium hole over the large tail); best-fit stops at an exact match after one step; best-fit scans the whole list without one (exact step count); first-fit stops at the first fit (exact step count); both return `NULL` when nothing fits; best-fit splits the block it chose; and a guard that the best-fit binary really has best-fit as its default (`Heap` and `default_heap()`).
- **The whole suite runs under both policies:** 86 tests × 2 binaries = 172, all passing (UBSan). No existing test needed a first-fit exemption: their expectations hold under best-fit too.
- **Mutation check** (2026-10-25, source restored): turning best-fit into worst-fit (`sz > best_size`) fails 6 tests in the best-fit binary, including 3 split tests and 1 first-fit-named test whose layouts happen to discriminate.
- First-fit footprint and realloc probe output after the templating refactor is byte-identical to Phase 4 on all pre-existing columns.
