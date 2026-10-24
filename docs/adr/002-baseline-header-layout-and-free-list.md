# ADR-002: Baseline allocator — header layout and explicit free list

- Status: Accepted (implementation complete; benchmark numbers pending Phase 5)
- Date: 2026-10-24
- Environment: see [../environment.md](../environment.md)
- Comparison baseline: macOS libmalloc (not glibc)

## Context
ADR-001 chose an intrusive explicit free list. This ADR fixes the concrete block layout and the Phase 1 baseline behaviour that later ADRs (splitting, coalescing, fit policy) are measured against.

## Decision
- **Alignment:** 16 B. Block starts are 8 mod 16, so `payload = header + 8` is 16-aligned. Arena base is page-aligned; first header sits at `base + 8`.
- **Header:** 8 B word = `size | flags`. Sizes are multiples of 16, so the low 4 bits are flags. Bit 0 = allocated. Bit 1 (prev-allocated) is reserved and first maintained in Phase 3.
- **Free block:** `[header][next][prev] … [footer]`, footer = copy of the header word. Minimum block 32 B. Allocated blocks have no footer.
- **Block size for a request n:** `max(32, align_up(n + 8, 16))`. `malloc(0)` returns a real minimum block.
- **Free list:** doubly linked, LIFO insert, head pointer only.
- **Fit policy:** first-fit from the list head.
- **Fresh memory:** when no free block fits, carve from a bump pointer (`top_`) at the uncarved tail of the arena. This is a Phase 1 simplification: without splitting, the arena cannot start life as one big free block. Phase 2 replaces it.
- **No splitting, no coalescing** in this phase. A freed block is reused whole by any request it fits.
- **Free validation:** pointer must be in the carved region, 16-aligned, with allocated bit set and a sane size. `Heap::free` returns `Ok | InvalidPointer | DoubleFree`; the C wrapper `my_free` prints a diagnostic and aborts.
- **Correctness tooling:** `Heap::check()` walks every block and the free list (sizes, alignment, footer == header, list/walk agreement, counters). Tests call it after every operation in the stress tests.

## Alternatives considered
- **Footer on every block:** simpler backward coalescing but +8 B per allocation. Deferred to ADR-004 where the prev-alloc-bit alternative is justified with numbers.
- **Separate `Heap::free` abort vs. status:** returning a status keeps the core testable without death tests; only the C wrapper aborts. Rejected: aborting inside `Heap`.
- **Starting the arena as one big free block now:** requires splitting, which is the subject of ADR-003. Rejected for Phase 1 to keep each ADR's before/after attributable to one change.
- **`malloc(0)` returns NULL:** legal, but then `free(NULL)` and a failed allocation are indistinguishable to callers. Rejected.

## Consequences
- Known weaknesses, deliberately left for later ADRs: internal waste (a 16 B request can consume a large freed block), and external fragmentation (no coalescing). The stress tests do not assert on these.
- Use-after-free writes clobber a freed block's `next`/`prev`; this can corrupt the free list. Documented limitation; neighbouring live blocks are unaffected (tested).
- Single-threaded; `default_heap()` lazy initialisation is not thread-safe.
- **Measured impact:** TBD. Phase 5 will benchmark this commit (tag `phase-1-baseline`) against libmalloc so ADR-003/004 have a true before/after.

## Validation
- 31 tests pass under UBSan (unit, alignment, randomised stress with `check()` after every op, API integration). See [../testing.md](../testing.md).
