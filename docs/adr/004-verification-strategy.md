# ADR-004: Verification strategy — heap invariant checker, layered tests, footprint probe

- Status: Accepted
- Date: 2026-10-24 (covers the approach used since Phase 1; updated as phases add tests)
- Environment: see [../environment.md](../environment.md)
- Code: `Heap::check()` in [src/heap.cpp](../../src/heap.cpp), [tests/](../../tests/), [bench/footprint_probe.cpp](../../bench/footprint_probe.cpp)

## Context
Allocator bugs are rarely visible where they happen: a wrong size written into a header corrupts the heap silently, and the crash appears thousands of operations later in unrelated code. The project needs a way to catch corruption at the operation that causes it.

The usual tool, AddressSanitizer, is not available. ASan hangs at startup on this machine ([environment.md](../environment.md)). And even when it works, ASan cannot see overflows *within* the mmap'd arena: to ASan the whole arena is one valid region, so a write from one block into its neighbour's header is invisible unless the allocator poisons memory manually.

Separately, the ADRs need before/after numbers for each design change. Timing benchmarks arrive in Phase 5, but fragmentation and utilisation are deterministic and can be measured from Phase 1 on.

## Decision

### 1. `Heap::check()`: a full structural invariant checker
Walks every block from the first header to the epilogue, then walks the free list, and verifies:

| Invariant | Catches |
|---|---|
| Epilogue header is `size 0, allocated` | Overruns off the end of the heap |
| Every block size ≥ 32 and a multiple of 16 | Corrupted headers, bad split arithmetic |
| No block overruns the epilogue | Bad sizes, bad merges |
| Every payload is 16-byte aligned | Alignment bugs |
| Free block footer == header | Stale footers after split/merge |
| Allocated count and bytes match `stats()` | Bookkeeping drift |
| Free list: every node inside the heap, marked free, `prev` links consistent | List corruption |
| Free list length == free blocks found by the walk == `stats().free_blocks` | Leaked or doubly-listed blocks, cycles (walk is bounded) |

Each phase extends the checker with the invariants it introduces. Added in Phase 3 (coalescing, ADR-006):

| Invariant | Catches |
|---|---|
| No two adjacent free blocks | Missed coalesce in either direction |
| Every header's prev-allocated bit matches the previous block's state | Stale bit, so `free` would read a non-existent footer or skip a merge |
| Epilogue's prev-allocated bit matches the last block | Stale bit at the heap end |
| Footer == block size (flags live only in headers) | Stale footer after split/merge |

It returns `false` plus a static message instead of asserting, so tests can report *which* invariant broke at *which* operation.

### 2. Layered tests

| Layer | Files | Purpose |
|---|---|---|
| Unit | `test_arena.cpp`, `test_heap_unit.cpp`, `test_split.cpp`, `test_coalesce.cpp`, `test_alignment.cpp` | One behaviour per test on a fresh, small `Heap`. Exact assertions on addresses, block sizes, free-list counts. |
| Randomised stress | `test_stress.cpp` | Seeded alloc/free sequences with `check()` after **every** operation. Each block is filled with a per-block byte pattern that is verified before free, so overlapping blocks are caught even if the headers look valid. One configuration uses a 64 KiB arena to keep the OOM path constantly exercised, and asserts that OOM actually occurred. Since Phase 3, each seed must also end with exactly one free block spanning the arena. |
| Integration | `test_api_integration.cpp` | The public C API on the real 256 MiB process heap, used like a program would: linked lists, strings, a 32 MiB block, and death tests that a double or invalid free aborts with a diagnostic. |

`Heap::free` returns a status (`Ok`, `InvalidPointer`, `DoubleFree`) so error handling is unit-testable without death tests. Only the C wrapper aborts, and that path is covered by integration death tests.

### 3. Footprint probe (deterministic measurement, not a test)
`bench/footprint_probe` replays three seeded workloads against a fresh `Heap` and reports internal overhead, arena utilisation, OOM count and the end-of-run free-list shape. Its CSV output is committed per phase in [results/](../results/) and quoted in ADRs. It measures *memory*, not time, so it is immune to timer granularity and scheduling noise.

### 4. Build configuration
Tests build in Debug with UBSan on. `check()` is never called from benchmark builds.

## Alternatives considered
1. **Rely on ASan with manual poisoning (`ASAN_POISON_MEMORY_REGION` on headers and free payloads).** The best tooling for in-arena overflows, but ASan does not run on this machine. Revisit if a working toolchain becomes available. The checker remains necessary regardless, since poisoning finds bad *accesses*, not bad *bookkeeping*.
2. **Assertions inside the allocator only.** Cheap, but local: an assertion in `free` cannot know that the free list now has a cycle elsewhere. Rejected as the main tool. The global walk is what finds non-local damage.
3. **Check only at the end of each test.** Much faster, but when it fails, the corrupting operation is thousands of steps back. Rejected for stress tests. Per-operation checking makes failures point at the exact operation and seed.
4. **Coverage-guided fuzzing (libFuzzer).** Valuable, but Apple clang does not ship the libFuzzer runtime, and seeded random stress already gives reproducible coverage of the same state space for this allocator's size. Deferred.
5. **Property-based testing library (e.g. RapidCheck).** Adds input shrinking, but another dependency for little gain over seeded loops with operation-indexed failure messages. Rejected.

## Consequences
- `check()` is O(heap blocks + free blocks), so per-operation stress tests are effectively quadratic. Workloads were sized to keep the full suite under ~5 s, which held in Phase 1 (~4 s). In Phase 2 the suite took ~20 s because fragmentation without coalescing inflated the block count (ADR-005). The workloads were not shrunk to hide that, and coalescing brought it back to ~8.5 s for 51 tests (ADR-006).
- `check()` reads only headers and footers, so it cannot detect a write into a live block's payload. That is what the fill patterns are for.
- Use-after-free writes into a freed block's payload clobber its free-list links. This is not detected in general, by design (out of scope). A unit test checks that neighbouring live blocks survive it.
- Freeing an *interior* pointer (into the middle of a block) cannot be detected in general: the 8 bytes before it are arbitrary payload data. Pointers outside the arena or misaligned are rejected.

## Validation
- Test count: 31 at the end of Phase 1, 40 at the end of Phase 2, 51 at the end of Phase 3 (all passing under UBSan).
- Mutation check (2026-10-24): deleting the `footer(h)->word = sz;` line in `Heap::free` makes the unit tests fail with `heap_check: footer disagrees with header` (e.g. `HeapUnit.AllocReturnsUsableMemory`, `HeapUnit.FirstFitSkipsTooSmallBlocks`). Restoring the line makes them pass again. Repeat this kind of check when adding invariants: a checker that never fails proves nothing.
- Phase 3 mutation checks (disabling the backward merge; not clearing the successor's prev-allocated bit) are recorded in [ADR-006](006-coalescing.md#validation). Each is caught by 14 or more tests with the specific invariant message.
