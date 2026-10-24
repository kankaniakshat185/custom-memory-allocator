# ADR-007: realloc (in place where possible) and calloc

- Status: **Accepted provisionally.** Correctness is final. Timing measured (in-place 12% faster than naive). The in-place *policy* is to be re-evaluated after ADR-009 (fit policy); see "Revisit criteria".
- Date: 2026-10-24
- Environment: see [../environment.md](../environment.md)
- Before: commit `0ad7359` (phase-3-coalescing), which had no realloc/calloc
- Raw data: [realloc-phase4.csv](../results/realloc-phase4.csv), [footprint-phase4-realloc.csv](../results/footprint-phase4-realloc.csv)
- Code: `Heap::realloc`, `Heap::calloc`, `place()`, `release()` in [src/heap.cpp](../../src/heap.cpp); C API in [include/myalloc/myalloc.h](../../include/myalloc/myalloc.h)

## Context
Tier 1 requires `my_realloc`, and `my_calloc` is the trivial companion. The baseline every allocator must beat is the naive implementation: `malloc(n)`, copy, `free(p)`. That is always correct, but it copies the whole payload on every call, and growth patterns (vectors, string builders) call realloc constantly.

Coalescing (ADR-006) makes something better possible: the block after `p` is often free, and boundary tags make that O(1) to check. The questions are which in-place cases to support, the edge-case semantics, and whether in-place resizing is actually a win.

## Decision

### realloc
1. `realloc(NULL, n)` → `malloc(n)`.
2. Validate `p` exactly as `free` does (shared `validate()`). An invalid pointer or a freed block returns `NULL` with a status. The C wrapper aborts with a diagnostic, like `my_free`.
3. **Shrink or same size: always in place.** If the freed tail is ≥ 32 B, split it off and pass it through `release()`, the same path `free` uses, so it coalesces with a free successor. Otherwise keep the whole block.
4. **Grow: in place if the successor is free and `have + successor ≥ need`.** Absorb it with `place()`, the same split logic `malloc` uses.
5. **Otherwise move:** `malloc` first, copy the old payload, then `free`. Allocating before freeing means a failure returns `NULL` and leaves `p` valid and unchanged (C semantics).
6. **`realloc(p, 0)` shrinks `p` to the minimum block and returns it.** It never frees and never returns `NULL`.
7. `n > arena size` fails early, which also guards the `n + 8` overflow in `block_size_for`.

### calloc
- `count * size` with `__builtin_mul_overflow`. Overflow → `NULL`.
- Zero exactly `n` bytes with `memset`, always.

### Supporting refactor (no behaviour change)
`malloc`, `free`, shrink and grow now share two helpers:
- `place(h, total, need)` allocates from free space and returns any remainder;
- `release(h)` returns a block to the free list and coalesces it.

Prev-allocated bit maintenance now exists in exactly two places instead of four. **Verified behaviour-preserving:** the original three footprint workloads produce byte-identical output before and after the refactor ([footprint-phase4-realloc.csv](../results/footprint-phase4-realloc.csv) vs. [footprint-phase3-coalescing.csv](../results/footprint-phase3-coalescing.csv)).

## Alternatives considered
1. **Naive realloc (always move).** The control in the experiment below. Simplest, and it never pins memory in place. Not rejected outright: on the buffer-growth workload it had the *best* footprint. See Consequences.
2. **Also grow backwards into a free predecessor** (merge with the previous block, `memmove` the payload down). Saves the allocation but still copies, and adds a third in-place case to verify. Deferred: the forward-grow results below need to be understood first.
3. **Free first, then malloc** (so the old block's space can be reused by the new allocation). Lets a block move into a merged region that includes itself, but the free overwrites the first 16 payload bytes with free-list links, and the footer, before the copy. And if malloc then fails, `p` is gone. Rejected: violates the "failure leaves `p` intact" contract.
4. **`realloc(p, 0)` frees `p` and returns `NULL`** (glibc's behaviour). Then `NULL` means both "freed" and "failed", and callers can't tell them apart. C23 makes `realloc(p, 0)` undefined behaviour anyway. Rejected in favour of the unambiguous choice, consistent with `malloc(0)` returning a real block (ADR-002).
5. **calloc: skip `memset` for never-touched memory** (fresh `mmap` pages are already zero). A real optimisation for large callocs, but it needs per-block "known zero" tracking that coalescing and splitting would have to maintain. Deferred: an optimisation, not a correctness item, and not supported by any measurement yet.

## Consequences

### Measured: realloc policy on a buffer-growth workload
The probe workload (`footprint_probe realloc`, seed 4, 200,000 ops) mixes growable buffers, which double from 16 B up to 64 KiB and then reset to 16 B, with small short-lived "noise" allocations of 16–256 B. Three policies run the identical op sequence:

- **naive_move:** always malloc + copy + free;
- **in_place:** this ADR;
- **grow_in_place_shrink_moves:** a control that grows in place but moves on shrink, to isolate the effect of in-place shrinking.

| Policy | Arena | Reallocs in place | Bytes copied | OOM | High-water | Utilisation | Largest free block at end |
|---|---|---|---|---|---|---|---|
| naive_move | 32 MiB | 0% | 155.0 MB | **0** | **29.2 MB** | **0.167** | 4.5 MB |
| in_place | 32 MiB | 51.7% | **70.1 MB** | 5,011 | 33.5 MB (arena full) | 0.144 | 32 KB |
| grow_in_place_shrink_moves | 32 MiB | 50.2% | 74.1 MB | 3,927 | 33.6 MB (arena full) | 0.143 | 32 KB |
| naive_move | 64 MiB | 0% | 155.0 MB | 0 | **29.2 MB** | **0.167** | 38.1 MB |
| in_place | 64 MiB | 49.3% | 89.8 MB | 0 | **62.2 MB** | 0.078 | 5.1 MB |
| grow_in_place_shrink_moves | 64 MiB | 47.7% | 88.4 MB | 0 | 54.7 MB | 0.089 | 12.5 MB |

### What the numbers mean
- **Copying is roughly halved:** −55% bytes copied at 32 MiB, −42% at 64 MiB. That was the intended benefit, and it holds.
- **Footprint gets much worse, and it is unbounded.** With room to spare (64 MiB), in-place realloc's high-water is 2.1× the naive policy's (62.2 vs. 29.2 MB). The naive policy's high-water is identical in both arenas, which means it is self-limiting. The in-place policy expands to fill the arena it is given, and when the arena is tight (32 MiB) it fails 5,011 times where naive fails 0.
- **Hypothesis "in-place shrinking causes it" (a small block left pinned at the front of what would have become a 64 KiB hole): refuted as the main cause.** The control that moves on shrink still fails 3,927 times at 32 MiB and still reaches 54.7 MB at 64 MiB. Shrinking accounts for roughly a quarter of the excess (7.5 of the 33.0 MB at 64 MiB); growing in place is the main driver.
- **Mechanism: not yet established.** Working hypothesis, untested: with LIFO first-fit, the remainder that `place()` splits off after an in-place grow goes to the free-list head. The next small "noise" allocation is then carved from directly behind the grown buffer, which blocks its next in-place grow and forces a move further toward the arena end, leaving holes the buffer can't reuse. If that is right, the problem is the *interaction* of in-place grow with LIFO first-fit, not in-place grow itself. Best-fit (ADR-009) would route noise into small holes instead, which is the test.
- **Scope of this result:** one synthetic workload, designed to be realloc-heavy. The three original workloads don't use realloc, so their numbers are unaffected (byte-identical, see above).

### Timing (ADR-008; same run as the libmalloc comparison, median ns/op, `realloc_growth`)
| Policy | ns/op | IQR | Batch p99 |
|---|---|---|---|
| **in-place (this ADR)** | **27.71** | 1.5% | 47.99 |
| grow in place, move on shrink | 28.36 | 12.2% (noisy) | 52.78 |
| naive (malloc + copy + free) | 31.58 | 0.7% | 59.16 |
| libmalloc `realloc` (Nano on) | 37.75 | 1.0% | 59.62 |

- **The copy savings do show up as time: in-place is 12% faster than naive** (27.71 vs. 31.58, both with tight IQRs), and 1.36× faster than libmalloc's realloc on this trace.
- Grow-only sits between them but is too noisy to separate from in-place.
- An earlier preliminary run suggested the opposite (naive fastest). That run was built for x86_64 and ran under Rosetta (ADR-008, Incidents), and is discarded.

### Revisit criteria (why this is provisional)
In-place realloc stays, because it is the standard design, it halves copying, and the code is verified. But the policy is re-decided once these two results exist:
1. **ADR-009 (best-fit):** rerun `footprint_probe realloc` under best-fit. If the footprint gap to naive closes, the gap was a fit-policy interaction, and in-place stays.
2. **Phase 5 timing — done:** the copy savings *do* show up as time (in-place 12% faster than naive). So the open trade-off is now purely **12% realloc speed vs. up to 2.1× footprint on this workload**, and (1) decides it: if best-fit closes the footprint gap, in-place stays without qualification.

### Other consequences
- `realloc` never fails when shrinking, and growing in place needs no free-list search. Only the move path can fail.
- Double free through realloc (realloc of a freed pointer) is detected the same way as in `free`, and aborts in the C API.
- `calloc` always pays a `memset` of `n` bytes, even on fresh zero pages (Alternative 5).

## Validation
- New unit suite [tests/test_realloc.cpp](../../tests/test_realloc.cpp) (19 tests: 15 realloc, 4 calloc):
  - **realloc paths:** null and same-size no-ops; shrink with split; shrink whose tail coalesces with a free successor; shrink too small to split; `realloc(p, 0)`; grow into a free successor (with split, and absorbing the whole successor); grow into the arena tail; move when the successor is allocated, and when it is free but too small (contents preserved, old block really freed).
  - **failure cases:** failure leaves the original intact, with byte pattern and stats unchanged; huge sizes fail without overflow; invalid and freed pointers are reported.
  - **growth pattern:** 16 B → 256 KiB doubling stays in place with nothing else allocated.
  - **calloc:** zeroes a reused dirty block; multiplication overflow fails; zero count or size gives a valid pointer; exhaustion fails.
- Stress: two new configurations with 25% realloc ops. They verify the byte pattern before realloc, the preserved prefix after, and that a failed realloc leaves the original intact. They assert that the failed-realloc path actually ran. The three original configurations are unchanged, so their timing remains comparable across phases.
- Integration ([tests/test_api_integration.cpp](../../tests/test_api_integration.cpp)): a string builder grown with `my_realloc` and shrunk to fit; `std::vector<int>` (100k `push_back`s) and `std::map<int, basic_string>` running entirely on `my_malloc` via a test-only allocator adapter, ending with zero live blocks; a 300×400 `my_calloc` matrix; death test for `my_realloc` of a freed pointer.
- **Mutation checks** (2026-10-24, source restored):
  - not releasing the shrink tail → 5 tests fail (`footer disagrees with header`, `adjacent free blocks`);
  - copying only 8 bytes on move → 3 tests fail, with 7,728 `realloc lost data` reports in the stress tests.
- 78/78 tests pass under UBSan.
