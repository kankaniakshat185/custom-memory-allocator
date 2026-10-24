# ADR-006: Coalescing with boundary tags and a prev-allocated bit

- Status: Accepted (footprint and timing measured)
- Date: 2026-10-24
- Environment: see [../environment.md](../environment.md)
- Before: commit `4c8d98e` (phase-2-splitting). Raw data: [footprint-phase2-splitting.csv](../results/footprint-phase2-splitting.csv)
- After: this change. Raw data: [footprint-phase3-coalescing.csv](../results/footprint-phase3-coalescing.csv)
- Code: `Heap::free` in [src/heap.cpp](../../src/heap.cpp), helpers in [src/block.h](../../src/block.h)

## Context
ADR-005 added splitting. It fixed internal waste but made external fragmentation unbounded: every split turns one block into two, and nothing ever turns two back into one. The Phase 2 footprint probe showed:

- random sizes: 9,293 OOM failures; at the end, 16.2 MB free in 82,197 pieces, largest 1,232 B;
- phase shift: 226 OOM failures; at the end, the *entire* arena free in 107,655 pieces;
- the test suite's per-operation heap check slowed down 8.5× (`Stress.SmallSizes` 1.6 s → 13.7 s) because the block count only grew.

Coalescing (merging a freed block with free neighbours) is the standard fix. The design question is how `free` finds its neighbours in O(1):

- The **next** block is easy: `header + size`.
- The **previous** block is the hard part: a header doesn't say where the previous block starts.

## Decision
1. **Immediate coalescing in `free`, both directions.** A freed block merges with its next neighbour if free, then with its previous neighbour if free. The result is one block, pushed to the free-list head (LIFO, unchanged).
2. **Boundary tags on free blocks only.** A free block's last 8 bytes (its footer) hold its size, so the block after it can find its start: `prev = h - footer_of_prev`.
3. **Prev-allocated bit (bit 1) in every header** tells `free` whether the previous block is free, and therefore whether a footer exists to read. Allocated blocks need no footer, so their payload runs to the end of the block.
4. **Footer holds the size only, not the flags.** Flags live only in headers, so a footer never needs updating when a neighbour's state changes.
5. **No boundary checks in the merge code.** The epilogue reads as allocated, which stops forward merges. The first block's prev-allocated bit is set at init (a "virtual prologue"), which stops backward merges. Both conditions are covered by the heap checker.
6. **Bit maintenance:** allocation without a split sets the successor's prev-allocated bit. A split gives the remainder `prev-allocated = 1`; its successor already had 0. `free` clears the successor's bit after merging.
7. **Double-free detection survives merging:** `free` clears the block's alloc bit *before* merging. If the block is absorbed into its predecessor, its stale header still reads as free, so an immediate second free returns `DoubleFree` instead of corrupting the heap.

## Alternatives considered
1. **Footer on every block (classic Knuth boundary tags).** Simpler, with no prev-allocated bit to maintain, but every allocated block pays 8 more bytes. Modelled with the same rounding rules as `block_size_for` (calculated, not from the probe):

   | Request distribution | Overhead, header only (chosen) | Overhead, footer on every block | Extra block bytes |
   |---|---|---|---|
   | log-uniform 16–256 B (app-like small objects) | 1.178× | 1.276× | **+8.3%** |
   | log-uniform 16 B–4 KiB (random workload) | 1.021× | 1.032× | +1.1% |

   Per request: 24 B → 32 vs. 48 B; 40 B → 48 vs. 64 B; 56 B → 64 vs. 80 B. Small objects dominate real programs, so the cost lands exactly where it hurts. Rejected. The prev-allocated bit costs two extra bit operations per malloc/free.
2. **Deferred coalescing (merge lazily, e.g. on allocation failure or in periodic sweeps).** glibc does a version of this for its fastbins: small chunks stay unmerged so they can be reused quickly, and are consolidated later. Pros: cheaper `free` for short-lived small objects. Cons: fragmentation between sweeps, a sweep with unbounded pause time, and a second code path to verify. Rejected for Tier 1. Worth revisiting only alongside a small-object fast path (Tier 2 slab), where it makes sense.
3. **Footer storing the full header word, flags included.** Then a free block's prev-allocated bit would live in two places that must be kept in sync. Rejected: size-only footers can never go stale on a flag change.
4. **Explicit prologue block (a real allocated block at the arena start).** The CS:APP approach. Costs at least 16 B and is one more special block for the checker. Setting the first block's prev-allocated bit does the same job for free. Rejected.
5. **Address-ordered free list, so neighbours are list neighbours.** Makes coalescing and some fit policies more natural, but insertion becomes O(free blocks). Still deferred (see ADR-001). Boundary tags give O(1) merging without it.

## Consequences

### Measured (footprint probe; same seeds and arena sizes)

| Workload | OOM P1 → P2 → **P3** | Utilisation (peak requested ÷ high-water) P1 → P2 → **P3** | High-water P1 → P2 → **P3** | Free blocks at end P2 → **P3** | Largest free at end P2 → **P3** |
|---|---|---|---|---|---|
| random 16 B–4 KiB | 0 → 9,293 → **0** | 0.242 → 0.099 → **0.615** | 7.70 → 16.78 → **3.03 MB** | 82,197 → **1,245** | 1.2 KB → **13.8 MB** |
| app-like | 1,035 → 549 → **0** | 0.142 → 0.142 → **0.310** | 33.55 → 33.55 → **15.40 MB** | 80,829 → **1,626** | 25.6 KB → **18.2 MB** |
| phase shift | 0 → 226 → **0** | 0.589 → 0.134 → **0.997** | 3.87 → 16.78 → **2.29 MB** | 107,655 → **1** | 7.2 KB → **16.8 MB** |

Internal overhead stays at ADR-005's level (1.003–1.021×). Coalescing doesn't touch it.

### What the numbers mean
- **Hypothesis 1, "coalescing eliminates the OOM failures splitting introduced": supported.** Zero failures on all three workloads, including app-like, which failed 1,035 times in the Phase 1 baseline.
- **Hypothesis 2, "freeing everything returns the arena to one block": supported.** Phase shift ends with exactly one free block covering the whole arena. The stress tests now assert this after every seed.
- **Phase shift is near-perfect (0.997 utilisation).** Its lifetimes are LIFO-like (everything freed in reverse order), so every free merges, and the next phase reuses one contiguous region.
- **App-like is the weak spot: 0.310 utilisation.** The heap reached 15.4 MB of high-water for 4.8 MB of peak live data. Hypothesis for ADR-009, not yet tested: with LIFO first-fit, small requests are carved out of whatever free block is at the list head, often a large hole left by a freed large object. That breaks the hole up, so the next large request has to go to the untouched tail. Best-fit (ADR-009) should send small requests to small holes, and that comparison is the test.
- **Test suite** (Debug + UBSan): `Stress.SmallSizes` 1.6 s (P1) → 13.7 s (P2) → **3.9 s** (P3). Full suite 4.0 s / 31 tests → 20.5 s / 40 tests → **8.5 s / 51 tests**. The per-operation checker's cost tracks the block count, which is a direct, if informal, fragmentation measure. It is not back to Phase 1's time, which is expected: Phase 1's walk never covered the uncarved tail, and the suite has more tests.

### Timing (ADR-008; one `run_phases.sh` run, `-O3`, median ns/op)
| Workload | Phase 1 (no split, no coalesce) | Phase 2 (split only) | **Phase 3 (split + coalesce)** |
|---|---|---|---|
| random 16 B–64 KiB | 15.36 (batch p99 96.2) | untimeable (> 80 min) | **17.58** (batch p99 49.1) |
| app-like | 26.45, 436 OOM/rep, not comparable | untimeable | **18.07** |
| adversarial holes | 2,008.7 | untimeable | **709.2** |

- **Coalescing is what makes the split design usable in time, not just in memory.** Phase 2 (split, no coalesce) cannot finish a 1 M-op workload. Phase 3 does it at ~18 ns/op.
- **Against Phase 1, the median costs ~2 ns/op (+14%) on random sizes, but the tail halves.** Phase 1 does less work per call: no split, no merge, and fresh memory from a bump pointer. But its batch p99 is 96 vs. 49 ns. Phase 1 also pays 3.8× internal overhead for that median (ADR-005), and fails 436 times per rep on app-like.
- **Adversarial: Phase 3 is 2.8× faster than Phase 1.** Hypothesis, not verified: Phase 1 never splits, so a 24 B pin request takes a whole 1–2 KiB hole, and it never coalesces, so freed pins and holes stay as separate list nodes from round to round, and the walks get longer. Checking it would mean logging free-list length per round under Phase 1 (`stats().free_blocks` exists there), deferred to the profiling phase (ADR-010).
- Phase 3 is still 53× slower than libmalloc on this workload (ADR-008). Coalescing does nothing for search cost when the holes are pinned apart. That is ADR-009/011's problem.

### Other consequences
- `free` is still O(1): at most two `list_remove`s, one footer read and three header writes. Its cost is included in the Phase 3 numbers above; isolating free vs. malloc cost needs the profiler (ADR-010).
- A use-after-free write into a *freed* block can now corrupt more state: the merged block's size or links span what used to be several blocks. Still out of scope (ADR-004). The checker would report it at the next operation in tests.
- Double-free of an absorbed block is detected only until that memory is reused. After reuse the stale header is overwritten by live data. Inherent without separate metadata; documented.
- Every allocation now writes a header just past the allocated block: the successor's (prev-allocated bit) or the split remainder's. For large blocks that is a different cache line (128 B on M2) from the block's own header. To be checked in the Phase 7 profile.

## Validation
- New suite [tests/test_coalesce.cpp](../../tests/test_coalesce.cpp) (12 tests): isolated free does not merge; merge with next; merge with previous; merge both directions at once; last block merges with the tail; the first block never reads before the arena; the prev-allocated bit tracks its neighbour through free and re-allocation; footers hold the size only; double free of an *absorbed* and of an *absorbing* block are both detected; freeing everything in random order restores one block (20 rounds × 300 blocks); and the flipped Phase 2 baseline: two merged 4 KB neighbours now satisfy a 6 KB request.
- Stress tests now also require exactly one free block, spanning the arena, at the end of every seed.
- Heap checker gained three invariants (ADR-004): no adjacent free blocks; every prev-allocated bit matches its predecessor; the epilogue's prev-allocated bit matches the last block.
- **Mutation checks** (2026-10-24, source restored afterwards):
  - disabling the backward merge → 14 tests fail, all with `adjacent free blocks (missed coalesce)`;
  - not clearing the successor's prev-allocated bit after a free → 23 tests fail with `prev-alloc bit disagrees with predecessor` / `epilogue prev-alloc bit wrong`.
- 51/51 tests pass under UBSan.
