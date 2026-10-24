# ADR-005: Splitting oversized free blocks

- Status: Accepted
- Date: 2026-10-24
- Environment: see [../environment.md](../environment.md)
- Before: commit `9baf140` (phase-1-baseline). Raw data: [footprint-phase1-baseline.csv](../results/footprint-phase1-baseline.csv)
- After: this change. Raw data: [footprint-phase2-splitting.csv](../results/footprint-phase2-splitting.csv)

## Context
In the Phase 1 baseline (ADR-002), a freed block is handed out whole to any request it can satisfy. A 16 B request can consume a freed 4 KiB block, and those 4 KiB stay unusable until the 16 B is freed. Fresh memory came from a bump pointer at the arena tail, because without splitting the arena could not start as one large free block.

The footprint probe ([ADR-004](004-verification-strategy.md)) showed how much this costs:

| Workload | Internal overhead (peak in-use ÷ peak requested) | OOM failures |
|---|---|---|
| random 16 B–4 KiB (16 MiB arena) | **3.83×** | 0 |
| app-like, 95% small / 5% large (32 MiB arena) | **7.00×** | **1,035** |
| phase shift: large phase then small phase (16 MiB) | 1.69× | 0 |

On app-like, the heap ran out of a 32 MiB arena while only 4.7 MB was requested.

## Decision
1. **The arena starts as one free block** spanning everything between the first header (`base + 8`) and a new **epilogue**: a zero-size header marked allocated at `end - 8`. The Phase 1 bump pointer is removed, so free-list blocks are now the only memory source.
2. **Split when the remainder can be a block of its own:** if `have - need >= kMinBlock` (32 B), the front `need` bytes become the allocated block. The tail becomes a new free block (header + footer) pushed to the head of the free list. Otherwise the whole block is handed out, with at most 16 B slack.
3. **Allocate from the front** of the free block.

The epilogue also prepares Phase 3: a forward-coalescing walk stops at it, because it reads as allocated.

## Alternatives considered
1. **Allocate from the back of the free block.** The free block keeps its header address and list position; only its size shrinks, which avoids an unlink/push pair per split and is likely slightly faster. Rejected for now: front allocation makes consecutive allocations ascending in address (better spatial locality, and a meaningful `high_water` footprint metric). **Open question for the Phase 5 timing benchmarks.** If list manipulation shows up in profiles, this is the first thing to try.
2. **Larger split threshold (e.g. only split if the remainder ≥ 64 or 128 B).** Avoids creating small slivers that only tiny requests can use. Rejected for this phase: slivers are a fragmentation problem, and the fix for fragmentation is coalescing (ADR-006), which re-absorbs them. Changing two variables at once would make the next ADR's numbers unattributable. Revisit with data after ADR-006.
3. **Keep the bump pointer and split only reused blocks.** Two sources of memory and two code paths, with no benefit once splitting exists. Rejected.
4. **Remainder keeps the original block's list position (replace in place) instead of pushing at head.** Preserves list order. Rejected: the LIFO policy (ADR-001) already defines order by recency, and a freshly split remainder is the most recently touched block.

## Consequences

### Measured (footprint probe; same seeds, same arena sizes)

| Workload | Overhead before → after | OOM before → after | Free blocks at end | Free bytes at end | Largest free block at end |
|---|---|---|---|---|---|
| random 16 B–4 KiB | 3.83× → **1.02×** | 0 → **9,293** | 82,197 | 16.2 MB | **1,232 B** |
| app-like | 7.00× → **1.01×** | 1,035 → **549** | 80,829 | 32.1 MB | 25,568 B |
| phase shift | 1.69× → **1.00×** | 0 → **226** | 107,655 | **16.8 MB (entire arena)** | 7,200 B |

(The end-of-run columns did not exist in the Phase 1 probe, so there is no before value. `ops` is lower on phase shift after the change because failed allocations are not later freed.)

### What the numbers mean
- **Internal waste is solved.** Overhead is now 1.00–1.02×: only the 8 B header and 16 B rounding remain.
- **Hypothesis "splitting reduces OOM failures": refuted on 2 of 3 workloads.** OOM fell on app-like but rose from 0 to 9,293 on random sizes and from 0 to 226 on phase shift.
- **Mechanism (external fragmentation).** Every split makes one block into two, and with no coalescing, nothing ever turns two blocks back into one. The block count only grows. At the end of the random workload there is 16.2 MB free but in 82,197 pieces, the largest being 1,232 B, so any request above ~1.2 KiB fails despite the arena being almost empty. In phase shift, the *whole* arena is free at the end, shredded into 107,655 pieces.
- **Why Phase 1 didn't have this problem:** reusing whole blocks never created new boundaries, and the bump pointer always had a large contiguous tail. Phase 1 wasted memory *inside* blocks. Phase 2 wastes it *between* blocks.
- `high_water` now reaches the end of the arena in all three workloads, for the same reason: once the front is shredded, only the untouched tail can serve larger requests.

This is the expected textbook result, and it is why splitting and coalescing are always used together. It is recorded here separately so ADR-006 has a clean before/after. **Resolved by [ADR-006](006-coalescing.md):** OOM back to 0 on all workloads.

### Other consequences
- Allocation now always unlinks a block and may push a remainder: one more list operation per split allocation. Timing impact is unmeasured until Phase 5. Rerun the timing benchmarks on this commit then.
- First-fit with LIFO now walks past many small slivers before finding a large block, so search cost will rise with fragmentation. To quantify in Phase 5/7.
- Side-effect visible in the test suite: `Stress.SmallSizes` went from 1.6 s to 13.7 s (Debug + UBSan). The workload didn't change; `check()` runs after every operation and is O(blocks), and the block count now only grows. The stress workloads are deliberately left unchanged, so ADR-006 can confirm that coalescing brings this back down.
- The epilogue makes `free` reject a pointer whose header would sit at or after the epilogue. Interior pointers into free memory still can't be detected (ADR-004).

## Validation
- New unit suite [tests/test_split.cpp](../../tests/test_split.cpp) (10 tests): fresh arena is one free block; split arithmetic; byte conservation (`in_use + free == usable`); remainder of exactly 32 B is split; remainder of 16 B is not; exact fit leaves no remainder; the whole arena can be allocated in one block, and one byte more fails; and the no-coalescing baseline that ADR-006 will flip.
- Existing unit, stress and integration tests updated where expectations changed (free-block counts now include the remainder). 40/40 pass under UBSan.
