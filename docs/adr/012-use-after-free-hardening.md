# ADR-012: Use-after-free hardening — validated links, safe unlinking, fail-stop

- Status: Accepted
- Date: 2026-10-25
- Environment: see [../environment.md](../environment.md); native arm64, `-O3`. Machine state: [results/hardening/timing-machine-state.txt](../results/hardening/timing-machine-state.txt)
- Before: commit `128bcc3` (phase-6-best-fit). Raw data: [results/hardening/](../results/hardening/), one folder per design iteration plus `final-vs-system/`
- Code: `in_heap`, `plausible_free`, `unlinkable`, `find_fit`, `release` in [src/heap.cpp](../../src/heap.cpp); `FitResult` and policies in [src/heap.h](../../src/heap.h); C API in [src/myalloc.cpp](../../src/myalloc.cpp)
- Numbered 012 because 010 (profiling) and 011 (segregated lists) were already reserved in the plan; this decision came before both.

## Context
The project requirements say: *"use-after-free isn't expected to be caught but shouldn't crash the allocator's own bookkeeping."* ADR-002 and ADR-004 had quietly downgraded that to a "documented limitation". The requirement was not met:

```
free(a); memset(a, 0x41, 16); malloc(5000);   // a is the free-list head and too small
→ segmentation fault (exit 139)
```

The search followed `a`'s overwritten `next` link to `0x4141414141414141`.

**What a use-after-free write can reach.** A program holding a dangling pointer `p` writes into `[p, p + old_size)`, the old *payload*. In a free block that region holds the `next` and `prev` links (first 16 bytes) and the footer (last 8 bytes). The block's own header is *before* `p`, so a use-after-free write through `p` doesn't reach it. (If the memory has since been re-split, headers of *new* blocks can lie inside the old range. The fuzz test covers that case.)

## Decision
**Guarantee:** no sequence of use-after-free writes makes the allocator read or write outside its arena, or loop without bound. Corruption is detected on a best-effort basis. Once detected, the heap stops (fail-stop): it never continues on metadata it knows is bad.

1. **Search (`find_fit`): every node read from a link must be `in_heap` and not marked allocated, and the walk may not visit more nodes than `free_blocks`.**
   - `in_heap` means `first_ ≤ h < epilogue_`, a 16-byte block offset. Then its header and links (`h … h+24`) are inside the arena.
   - The visited bound turns any cycle into a detected corruption instead of a hang.
   - Nothing else is checked per node (see Consequences for why).
2. **Safe unlinking (`unlinkable`), before any link is rewritten:**
   - the block and both neighbours must pass `plausible_free` (in heap, free bit, sane size);
   - the neighbours must point back at it (`prev->next == h`, `next->prev == h`, or `free_head_ == h`).

   This is the same idea as glibc's "corrupted double-linked list" check.
3. **Backward merge (`release`):** the predecessor's footer (in its payload, so writable through a dangling pointer) must be ≥ 32, a multiple of 16, and must not point before the heap start. The predecessor's header must agree with it. Both neighbours are validated **before** anything is modified, so a detected corruption leaves the heap unchanged.
4. **Fail-stop:**
   - On any failed check, the heap sets a sticky `corrupted()` flag.
   - From then on `malloc`/`realloc` return `NULL`, and `free`/`realloc` return `FreeStatus::HeapCorrupted`.
   - The C API aborts with `"<fn>: heap corruption detected (free-list metadata overwritten)"`, as glibc and libmalloc do, rather than returning `NULL` that would look like out-of-memory.
5. `free_summary()` and `check()` are bounded and validated too, so tests and probes are safe after corruption.

## Alternatives considered
1. **Leave it as a documented limitation.** Violates an explicit requirement, as the segfault shows. Rejected.
2. **Recover instead of stopping** (truncate the free list at the bad node, leak what's behind it, continue). Literally "doesn't crash", but it keeps running on a heap known to be damaged, where the next undetected inconsistency could return overlapping blocks. glibc and libmalloc both abort on detected free-list corruption. Rejected.
3. **Pointer mangling or checksums** (glibc safe-linking since 2.32 XORs `next` with its own address; libmalloc reports *"incorrect checksum for freed object — object was probably modified after being freed"*). Catches more random overwrites and resists deliberate forging, at the cost of an encode/decode per link. Deferred: the bounds checks already give the memory-safety guarantee; this would improve *detection*, which the requirement doesn't ask for.
4. **Out-of-band free-list metadata** (keep links outside the blocks). Makes use-after-free writes unable to reach metadata at all, but contradicts ADR-001's intrusive design and its "no separate bookkeeping" constraint. Rejected.
5. **Full plausibility check on every search node** (iteration v2 below). Measured too expensive for best-fit (+50% on random sizes). Rejected in favour of the minimal per-node check.
6. **Footer check in the plausibility test** (iteration v1). Measured: a likely cache miss per node (+38% on the memory-bound adversarial workload). Not needed for safety. Rejected.
7. **Back-link check during the search.** A mutation test showed it is redundant: removing it failed no test. A forged link to a genuine free block is harmless to follow, and the chosen block is checked by safe unlinking anyway. Removed.

## Consequences

### The design was driven by measurement (cost vs. unhardened `128bcc3`, same-run medians)
| Iteration (per-node check in the search) | FF random | FF app-like | BF random | BF app-like | FF adversarial | Runs |
|---|---|---|---|---|---|---|
| v1: full check incl. footer | +31%, +33% | +48%, +29% | +71%, +66% | +78%, +78% | +38%, +36% | 2 |
| v2: full check, no footer | +23%, +25%, +35% | +26%, (+9% noisy), +23% | +50%, +53%, +50% | +67%, +58%, +69% | −1%, +0%, +5% | 3 |
| v3: `in_heap` only | +29%, +22% | +28%, +28% | +7%, +5% | +38%, +33% | +7%, +0% | 2 |
| **v4: `in_heap` + free bit (final)** | **+29%, +28%** | **+27%, +29%** | **+7%, +9%** | **+33%, +34%** | **+0%, +3%** | 2 |

What each step showed:
- **v1 → v2:** the adversarial overhead (2,000 cold nodes per walk) vanished when the footer read was dropped. That supports the hypothesis that the footer, on another cache line, cost one miss per node.
- **v2 → v3:** best-fit random fell from +50% to +7%. Best-fit visits ~100 nodes per malloc, so the per-node check was its cost.
- **v3 vs. v4:** indistinguishable. The free-bit test reuses the header load the search does anyway, so v4 keeps it. It restores detection of links into allocated blocks: under v3 that case gave a silent false `NULL`, and two tests caught it.
- **First-fit's ~+25–29% (~4–5 ns/op) is unchanged from v2 to v4,** while first-fit visits only ~2–3 nodes per malloc. So it is a **fixed per-operation** cost, not search cost. The most likely source is safe unlinking and the `release` checks (three plausibility tests and two back-link loads per unlink, up to two unlinks per free). That is not yet verified: it is a named target for the Instruments profile (ADR-010). Tuning stopped here rather than continuing on guesses.

### Final numbers vs. libmalloc (one run, IQR ≤ 10% on every row; [summary](../results/hardening/final-vs-system/timing-summary.md))
| Workload | libmalloc (default) | libmalloc, Nano off | **Heap (first-fit, hardened)** | vs. default libmalloc |
|---|---|---|---|---|
| random 16 B–64 KiB | 29.15 | 28.21 | **20.52** | **1.42× faster** (was 1.76× before hardening) |
| app-like | 18.65 | 21.17 | **21.28** | **1.14× slower** (was 1.12× faster); level with Nano off |
| adversarial holes | 12.38 | 13.55 | 650.07 | 53× slower (unchanged) |
| realloc growth | 38.00 | 42.03 | **33.11** | **1.15× faster** (was 1.36×) |

- **Hardening flips app-like:** the hardened heap is now 14% slower than libmalloc's default on small objects. It is level with libmalloc when the Nano zone is off (21.28 vs. 21.17), which suggests libmalloc's lead there comes from its small-object fast path, the role a Tier 2 slab allocator would play. That is a hypothesis from one comparison, not a measured cause.
- **The comparison is now closer to like-for-like on integrity checking.** libmalloc also detects free-list overwrites (its "incorrect checksum for freed object" abort), so the pre-hardening numbers flattered this allocator by skipping work libmalloc does.
- **realloc:** in-place is still faster than naive (33.11 vs. 37.66), so ADR-007's conclusion stands.
- **Best-fit after hardening:** 240 / 68 ns/op on random / app-like (was 207 / 51), so ADR-009's ranking is unchanged.
- **Footprint is unaffected:** probe output is byte-identical to Phase 6 for both policies and the realloc probe. The hardening changes no placement decision.

### Limits of the guarantee
- **Detection is best-effort, safety is not.** Writes that miss links and footers aren't detected and don't matter. A forged link that stays in the heap and points at a free-marked location can be followed without harm. In the worst case it hides some free blocks from a search (a false `NULL`) until safe unlinking or a later check catches it.
- **Not an attack boundary.** A deliberate attacker who knows the layout could forge a consistent-looking neighbour pair, which safe unlinking alone can't detect. Alternative 3 (pointer mangling) is the standard mitigation, and it is out of scope here.
- Writes through a dangling pointer into memory that has since been *reallocated* corrupt the new owner's data, or a new block's header. This allocator can't prevent that, and no allocator can without changing the API. The fuzz test shows it causes either a rejected free or a detected corruption, never a crash.

## Validation
- **The failing case from Context** now returns `NULL` and sets `corrupted()`. Through the C API it aborts with the diagnostic (death test).
- **New suite** [tests/test_hardening.cpp](../../tests/test_hardening.cpp) (13 tests, run under both fit policies). Each targeted test overwrites one kind of metadata through a dangling pointer, checks that the corruption is detected, and checks that every API call afterwards is refused and safe:
  - `next` link set to garbage, to an allocated block, to a stack address outside the arena, and to a misaligned address inside the heap;
  - broken back-links, on the chosen block and on its neighbour;
  - a self-loop, and a two-node cycle (both must terminate);
  - overwritten footers (0, 8, 2⁴⁰, and a plausible wrong size) on a backward merge;
  - corruption met during an in-place realloc;
  - writes that miss metadata are *not* flagged;
  - the C API aborts with the diagnostic.
- **The requirement itself, as a fuzz test:** 300 seeds × 3,000 random alloc/free/realloc ops, with 5% of ops writing 1–4 random 8-byte values anywhere in a dangling block's old range. No crash and no hang under either policy. Outcomes (first-fit / best-fit): corruption detected 228 / 228; bad pointer rejected 67 / 70; ran to completion with harmless writes 5 / 2. Also clean under ASan + UBSan.
- **Mutation checks** (2026-10-25, source restored each time): each remaining defence is necessary.

  | Defence removed | Result |
  |---|---|
  | per-node search check | segfault (UBSan `DEADLYSIGNAL`) |
  | footer bounds check in `release` | segfault reading a wild predecessor |
  | visited bound | `SelfLoopIsDetected` hangs until killed (20 s alarm) |
  | back-link check in the search | **no test fails**, so removed as redundant (Alternative 7) |
- 198/198 tests pass under UBSan and under ASan + UBSan (99 tests × 2 policies).
