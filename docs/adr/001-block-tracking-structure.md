# ADR-001: Block tracking structure — free list vs. alternatives

- Status: Accepted (implemented in Phase 1, see ADR-002)
- Date: 2026-10-24
- Environment: see [../environment.md](../environment.md)
- Comparison baseline: macOS libmalloc (not glibc)

## Context
The allocator manages one fixed mmap'd arena and must (a) find a free block for a request, (b) mark blocks free/allocated, (c) merge adjacent free blocks, with no bookkeeping memory outside the arena. Requirements: 16-byte alignment, O(1) free including coalescing, minimal per-block overhead. Header layout, coalescing and fit policy all depend on this decision.

## Decision
Intrusive, explicit, doubly-linked free list, LIFO insertion, next/prev stored in the payload of free blocks. 8 B header (size | alloc bit | prev-alloc bit), footer on free blocks only, 32 B minimum block. Layout details in ADR-002.

## Alternatives considered
1. **Implicit free list** (walk all blocks): simplest, but allocation is O(all blocks). Rejected as final design. It was considered as a measured reference point but not built: the time went to the footprint probe instead (ADR-004), which gives a per-phase before/after that is more useful than one extra baseline.
2. **Bitmap over fixed-size granules:** O(1) state lookup, but needs a side structure (violates "no separate bookkeeping"), finding N free granules is a scan, and granule size trades internal fragmentation against bitmap size.
3. **Singly-linked free list:** smaller min block, but unlinking during coalescing is O(n). Rejected: coalescing is the key feature.
4. **Address-ordered free list:** lower fragmentation, but O(n) insert on free. Deferred as a follow-up ADR candidate.
5. **Segregated size-class lists:** likely answer to O(n) search; out of Tier 1 scope unless profiling motivates it (ADR-011).
6. **Buddy system / balanced tree:** rejected; power-of-two internal fragmentation (buddy) or complexity (tree) with no Tier 1 benefit.

## Consequences
- Pros: O(1) free + coalesce; no external metadata; fit policy isolated in `find_fit()`.
- Cons: 32 B minimum block (quantify waste for tiny requests); O(free blocks) search; LIFO order may fragment more than address order.
- **Measured impact:** footprint numbers per phase are in ADR-005 onward ([results/](../results/)). Timing (ns/op vs. libmalloc) and profiler explanation: TBD in Phase 5/7. The implicit-list reference point was not built; the comparison will be against libmalloc and between phases instead.
- Limitation: baseline is libmalloc; Nano-zone control result (`MallocNanoZone=0`) TBD, or "no control possible".
- Follow-ups: ADR-002 header layout, ADR-005 splitting, ADR-006 coalescing, ADR-009 fit policy.

## Validation
- Hypothesis: allocation cost is dominated by free-list search length, which grows with fragmentation; O(1) free and coalesce keep `free` flat across workloads.
- Result: TBD in Phase 5/7 (timing per workload, plus free-list length over time and a Time Profiler breakdown of `find_fit` vs. the rest).
