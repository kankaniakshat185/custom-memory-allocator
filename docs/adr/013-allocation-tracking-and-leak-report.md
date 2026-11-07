# ADR-013: Allocation tracking and leak reporting

- Status: Accepted
- Date: 2026-11-07
- Environment: see [../environment.md](../environment.md)
- Plan: post-v1.0 task list, Phase 12 (Tier 2 item "basic leak detection / allocation tracking")
- Code: `my_alloc_stats`, `my_report_leaks` and the at-exit hook in [src/myalloc.cpp](../../src/myalloc.cpp); `BasicHeap::for_each_live` in [src/heap.h](../../src/heap.h); API in [include/myalloc/myalloc.h](../../include/myalloc/myalloc.h)

## Context
The original spec's Tier 2 asked for "basic leak detection / allocation tracking (total bytes outstanding, peak usage) exposed via a debug reporting function". Most of the bookkeeping already existed: `HeapStats` has kept bytes in use, peak, live blocks and free blocks since Phase 1 for the tests and probes. But none of it was reachable from the C API, and nothing could list *which* blocks are still live.

Constraints:
- **Nothing may slow `malloc`/`free`** or change any placement decision. The footprint probe's byte-identical check in CI enforces the second.
- **No per-block memory may be added.**
- **Robustness:** a report is most useful when something has gone wrong, so it must not crash on a damaged heap (ADR-012).

## Decision
1. **`my_alloc_stats(&s)`, O(1).** It reads only counters that are maintained anyway:

   | Field | Meaning |
   |---|---|
   | `arena_bytes` | bytes the heap manages |
   | `bytes_in_use` | allocated blocks, including their headers |
   | `payload_in_use` | usable bytes in allocated blocks |
   | `peak_bytes_in_use` | high-water of `bytes_in_use` |
   | `live_blocks` | allocations not yet freed |
   | `free_bytes` | free space |
   | `free_blocks` | separate free blocks: a cheap fragmentation indicator |
   | `high_water` | highest arena offset ever handed out |
   | `corrupted` | nonzero once corruption was detected (ADR-012) |

   Two of these are derived without walking anything. `payload_in_use = bytes_in_use − 8 × live_blocks`. `free_bytes = arena_bytes − bytes_in_use`, because every byte between the first header and the epilogue belongs to exactly one block, free or allocated (the invariant `check()` verifies).
2. **`my_report_leaks(FILE*)`, O(blocks).** It walks the heap in address order using the new `BasicHeap::for_each_live(f)`, prints each live block (address, usable size) and the totals, and returns the count.
3. **The walk is safe on a damaged heap.** It reads headers only. It stops at the first header whose size is below the 32 B minimum or runs past the epilogue, and the report then says the list is incomplete. A size-0 header can't make it loop, and an oversized one can't send it out of the arena.
4. **Leak report at exit is opt-in:** `MYALLOC_LEAK_REPORT=1` prints the report to stderr at exit if any block is still live.
   - **Registration order:** the hook is registered with `atexit` right after the default heap is constructed, so it runs *before* the heap's destructor.
   - **When the variable is read:** at exit, not at start-up, so it can be set after the heap was created.
   - **Precedent:** this follows the convention of environment-controlled allocator diagnostics (glibc's `MALLOC_CHECK_`, macOS's `MallocStackLogging`).
5. **Sizes are reported as usable bytes, not requested bytes.** The header has no room for the requested size (four flag bits only). The reported size is `block size − 8`, at least the request and at most 39 bytes more, the same notion as `malloc_usable_size`. The API comment says so.

## Alternatives considered
1. **Store each allocation's requested size.** That gives exact "bytes outstanding as requested", but costs 8 B per block: the same per-allocation overhead ADR-006 measured as +8.3% on small objects and rejected for footers. Rejected. Usable size is what other allocators report, too.
2. **Record a call stack per allocation**, which is what makes a leak report actionable. It costs a backtrace (microseconds) on every malloc and a side table of stacks, which breaks the "no slowdown, no extra memory" constraint. macOS's `leaks` tool and `MallocStackLogging` already provide this for the system allocator. Deferred, as a possible debug-build-only option.
3. **An always-on report at exit.** It would print noise in every test binary and every program that intentionally leaves blocks live until exit (common and legitimate). Rejected for opt-in.
4. **Read the variable once at initialisation.** It's simpler, but the heap initialises on the first `my_malloc`, often before a program or test has set its environment, and forked death-test children inherit an already-initialised heap. Rejected; reading at exit costs one `getenv` per process.
5. **Make `my_alloc_stats` exact by walking the heap.** All the O(1) fields are already exact. Only "largest free block" would need a walk, and that is already available to C++ callers through `free_summary()`. Rejected for the C API, to keep the stats call constant-time.

## Consequences
- **Zero cost on the hot path:** no counter or field was added to `malloc`/`free`. The footprint probe is byte-identical (CI check), and the timing code paths are unchanged.
- The C API gained two functions and a struct. The header now includes `<stdio.h>` for `FILE*`. It compiles as C11 with `-Wall -Wextra`, verified with a C program linked against the library.
- A report taken after a heap-damaging bug (for example a buffer overflow into a header) is truncated at the damage and labelled as incomplete, not wrong.
- Like the rest of the allocator, these functions are not thread-safe (Phase 16).

## Validation
- New suite [tests/test_tracking.cpp](../../tests/test_tracking.cpp) (9 tests, both fit policies):
  - **the walk:** visits exactly the live blocks, in address order, with the right usable sizes; usable size is at least the request and below request + 40 for sizes 0–4,095; nothing is live after freeing 200 blocks, and the walk is then complete; a zeroed header stops the walk safely after the blocks before it;
  - **the C API:** stats deltas for two allocations, and back to baseline after freeing; peak is a high-water mark; free + in-use = arena; the arena size matches the documented 256 MiB; `my_report_leaks` prints the exact `address  size` line for a live block and returns the same count as the stats;
  - **the exit report:** in death-test children, `MYALLOC_LEAK_REPORT=1` produces the report at exit, and leaving it unset produces no output at all.
- **Mutation check** (source restored): without the walk's minimum-size bound, `WalkStopsSafelyAtDamagedHeader` loops forever (killed by a 20 s alarm). The bound is necessary.
- **End-to-end:** a C program that leaks a `my_malloc(100)` and a `my_calloc(10, 24)` and frees a 5,000 B block reports two blocks of 104 and 248 usable bytes (352 total), with `bytes_in_use` 368 and `peak_bytes_in_use` 5,376, matching the block arithmetic by hand.
- 216/216 tests pass under UBSan and under ASan + UBSan (108 × 2 policies). Footprint is byte-identical. Formatting is clean.
