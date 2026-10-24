# ADR-001: Block tracking structure — free list vs. alternatives

- Status: Proposed
- Date: 2026-10-24
- Environment: see [../environment.md](../environment.md)
- Comparison baseline: macOS libmalloc (not glibc)

## Context
The allocator manages one fixed mmap'd arena and must (a) find a free block for a request, (b) mark blocks free/allocated, (c) merge adjacent free blocks, with no bookkeeping memory outside the arena. Requirements: 16-byte alignment, O(1) free including coalescing, minimal per-block overhead. Header layout, coalescing and fit policy all depend on this decision.

## Decision
Intrusive, explicit, doubly-linked free list, LIFO insertion, next/prev stored in the payload of free blocks. 8 B header (size | alloc bit | prev-alloc bit), footer on free blocks only, 32 B minimum block. Layout details in ADR-002.

## Alternatives considered
1. **Implicit free list** (walk all blocks): simplest, but allocation is O(all blocks). Rejected as final design; kept as a measured Phase 1 reference point if cheap. [Result: TBD]
2. **Bitmap over fixed-size granules:** O(1) state lookup, but needs a side structure (violates "no separate bookkeeping"), finding N free granules is a scan, and granule size trades internal fragmentation against bitmap size.
3. **Singly-linked free list:** smaller min block, but unlinking during coalescing is O(n). Rejected: coalescing is the key feature.
4. **Address-ordered free list:** lower fragmentation, but O(n) insert on free. Deferred as a follow-up ADR candidate.
5. **Segregated size-class lists:** likely answer to O(n) search; out of Tier 1 scope unless profiling motivates it (ADR-008).
6. **Buddy system / balanced tree:** rejected; power-of-two internal fragmentation (buddy) or complexity (tree) with no Tier 1 benefit.

## Consequences
- Pros: O(1) free + coalesce; no external metadata; fit policy isolated in `find_fit()`.
- Cons: 32 B minimum block (quantify waste for tiny requests); O(free blocks) search; LIFO order may fragment more than address order.
- **Measured impact:** TBD (ns/op and footprint ratio vs. implicit-list reference and libmalloc per workload, with profiler-backed explanation).
- Limitation: baseline is libmalloc; Nano-zone control result (`MallocNanoZone=0`) TBD, or "no control possible".
- Follow-ups: ADR-002 header layout, ADR-004 coalescing, ADR-006 fit policy.

## Validation
- Hypothesis: TBD (e.g. explicit list cuts alloc latency by ≥Nx vs. implicit list on the app-like workload)
- Result: TBD
