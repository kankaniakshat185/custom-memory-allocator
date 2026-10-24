# ADR-003: Arena — one fixed mmap reservation, no growth

- Status: Accepted
- Date: 2026-10-24 (recorded retroactively; the decision was made and implemented in Phase 0)
- Environment: see [../environment.md](../environment.md)
- Code: [src/arena.h](../../src/arena.h), [src/arena.cpp](../../src/arena.cpp)

## Context
The allocator needs a source of raw memory to carve up. The project explicitly treats the OS primitive as given (no page-table or mmap reimplementation) and must:

- give deterministic out-of-memory behaviour so OOM handling can be tested;
- allow tests to create many independent heaps of different, small sizes;
- not pre-commit physical memory it doesn't use, so footprint comparisons with libmalloc are meaningful;
- work on macOS/arm64 (16 KiB pages) while staying POSIX-portable.

## Decision
- Each `Heap` owns one `Arena`: a single `mmap(PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON)` reservation, rounded up to a page multiple using `sysconf(_SC_PAGESIZE)` (never a hard-coded 4096).
- The arena **never grows**. When it is exhausted, `malloc` returns `nullptr`.
- The process-wide heap behind `my_malloc` reserves 256 MiB on first use.
- Physical pages are committed by the kernel lazily on first touch. Freed memory is **not** returned to the OS (no `madvise`/`munmap` until the heap is destroyed).
- `Arena::contains()` provides the bounds check used by free-pointer validation.

## Alternatives considered
1. **`sbrk`/`brk`.** The classic textbook primitive, but deprecated on macOS, and it is a single process-wide break: it would conflict with libmalloc in the same process and makes independent per-test heaps impossible. Rejected.
2. **Static buffer (`static unsigned char heap[N]`).** Simplest, but N is fixed at compile time, lives in the binary's BSS for every build, and only one heap can exist. Tests need many differently sized heaps (1 MiB, 64 KiB, one 16 KiB page). Rejected.
3. **Growable arena (map another chunk when full).** Realistic, but chunks are not contiguous: coalescing must not merge across chunk boundaries, each chunk needs its own sentinels, and the OOM point becomes non-deterministic. That is a design topic of its own and outside the 1–2 week scope. Deferred; would be its own ADR.
4. **`MAP_NORESERVE` to avoid committing swap.** Has no useful effect on macOS, and lazy commit already gives the behaviour wanted. Not used.
5. **Returning freed pages with `madvise(MADV_FREE)`.** Would lower resident footprint after large frees, but adds a policy (thresholds, hysteresis) and makes footprint numbers depend on kernel reclamation timing. Deferred until after profiling, if footprint results justify it.

## Consequences
- OOM is exact and reproducible: the same seed on the same arena size fails at the same operation. Several tests depend on this.
- `my_malloc` is capped at 256 MiB per process. Fine for every planned workload; documented in the README.
- Reserving 256 MiB of address space costs nothing physical until touched, so the default heap's footprint reflects only what the workloads use.
- Footprint comparisons with libmalloc must account for (5): libmalloc may return memory to the OS, this allocator does not. The comparison reports peak footprint, where this matters least.
- Because the arena base is page-aligned, the first block header sits at `base + 8` so payloads are 16-aligned (ADR-002).

## Validation
- Arena unit tests: page alignment, rounding up to page size, exact `contains()` bounds, zero-filled writable memory, rejecting zero-size and double `init` ([tests/test_arena.cpp](../../tests/test_arena.cpp)).
- OOM tests in [tests/test_heap_unit.cpp](../../tests/test_heap_unit.cpp) and the tiny-arena stress test rely on the fixed size.
