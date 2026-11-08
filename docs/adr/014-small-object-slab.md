# ADR-014: Small-object slab — built, measured, shipped opt-in

- Status: Accepted. **Opt-in:** the slab front end is the C++ class `myalloc::Allocator`. The C API stays on the segregated-fit heap, because the slab failed the pre-set decision rule on the realloc workload. Heap `aligned_malloc` (added for it) is part of the heap API.
- Date: 2026-11-08
- Environment: see [../environment.md](../environment.md); native arm64, `-O3`
- Raw data: [results/slab/](../results/slab/) (see its README)
- Code: [src/slab.h](../../src/slab.h), [src/allocator.h](../../src/allocator.h), `BasicHeap::aligned_malloc` in [src/heap.cpp](../../src/heap.cpp)

## Context
The original spec's Tier 2 asked for "a slab/pool allocator for small, fixed-size allocations as a fast path layered on top of the general-purpose allocator". ADR-010 and ADR-012 had pointed at a small-object fast path as the reason libmalloc won app-like; then segregated fits (ADR-011) won app-like on their own. So the question became whether a slab still adds anything.

**Headroom, measured before building:**
- **Memory:** at app-like's small-object peak, heap blocks take 1.17× the requested bytes and slab slots would take 1.09×, so ~7% less memory for small objects. The 8 B header matters little at 16–256 B.
- **Speed:** libmalloc's own small-object fast path (the Nano zone) is worth 12–15% to libmalloc on app-like (two clean runs).

**Decision rule, fixed before any result:** the slab goes into the default C API path only if app-like improves ≥ 10%, no workload regresses > 5%, and the footprint probe (including its realloc mode) gets no more than 10% worse. Otherwise it ships opt-in, with the result documented.

## Decision
1. **Size classes and layout:** 16 classes, 16–256 B in 16 B steps, in **16 KiB slabs**. Each slab has a 256 B header, then equal slots.
2. **Out-of-band metadata:** a per-slab **bitmap of free slots** lives in the header. Freed slots hold no links, so a use-after-free write into a slot *cannot* reach slab bookkeeping. That's stronger than ADR-012's "detect or survive" for the general heap. Double frees are detected exactly, by the bit.
3. **Slabs are carved from the heap (v2):**
   - **Allocation:** each slab is a 16 KiB-aligned heap block of exactly 16 KiB, from the new `BasicHeap::aligned_malloc`. The heap header is the last 8 bytes of the previous 16 KiB chunk, so adjacent slabs tile with no gaps; each slab's usable space is 16,376 bytes.
   - **Release:** an emptied slab is freed back to the heap, except the class's last partial slab, which is kept to avoid churn.
   - **Ownership:** a bit per 16 KiB chunk of the arena marks slabs, kept out of band, so `free` identifies slab pointers with one bit test.
4. **Front end (`BasicAllocator<Fit>`):**
   - **Routing:** ≤ 256 B goes to the slab, the rest (and anything the slab can't serve) to the heap.
   - **`realloc`:** in place within a slot, otherwise a move.
   - **Stats:** user-level (slab blocks replaced by the live objects in them), with an exact combined peak.
5. **Headers are validated before every use** (magic, class, capacity, live count, chunk bit). Only an overflow out of the preceding memory can reach one; a bad header stops the cache (fail-stop, as in ADR-012).
6. **Ships opt-in,** per the rule (Consequences).

## Alternatives considered
1. **Slab v1, a separate fixed slab region** (the first version built). Simple ownership (a range check) and no heap interaction. **Measured and replaced:** phase-shift utilisation fell 0.997 → 0.856 (−14%), because memory freed by large objects in the heap couldn't serve small objects in the other region. That failed the footprint criterion.
2. **Intrusive free lists of slots** (the classic pool). That puts links in freed slots, which use-after-free writes can corrupt, so it would need ADR-012-style validation. Rejected in favour of the out-of-band bitmap.
3. **Find a slab's header by masking the pointer alone, without the chunk bitmap.** Then a heap block's payload could be misread as a slab header, by chance or by forgery. Rejected; the bitmap costs 1 bit per 16 KiB (2 KiB for 256 MiB).
4. **Smaller slabs (e.g. 4 KiB).** Less per-class partial-slab waste, but more headers and more slab churn. The measured waste turned out not to be the main cost (see below), so this wasn't pursued.
5. **Make it the default anyway.** It wins app-like, but that would override a rule set before the data. Rejected.

## Consequences

### Footprint (deterministic probe, utilisation = peak live ÷ high-water)
| Workload | Segregated (default) | Slab v1, separate region | **Slab v2, from the heap** |
|---|---|---|---|
| app-like | 0.728 | 0.818 | **0.828 (+14%)** |
| random | 0.812 | 0.810 | 0.810 (±0) |
| phase shift | **0.997** | 0.856 (−14%) ❌ | 0.942 (−5.5%) ✅ |
| realloc growth, in-place (high-water) | **5.66 MB** | – | 7.90 MB (**+40%**) ❌ |
| realloc growth, naive (high-water) | 5.61 MB | – | 7.08 MB (+26%) |

### Time (one run, 15 reps, median ns/op; machine under load, compare within the run)
| Workload | libmalloc | Segregated (default) | **Slab v2** | Slab vs. segregated |
|---|---|---|---|---|
| app-like | 18.80 | 16.40 | **14.75** | **−10.1%** ✅ |
| random | 30.83 | 26.72 | 26.09 | −2.4% ✅ |
| adversarial | 12.33 | 15.11 | 10.67 | −29% (artifact, below) |
| realloc growth | 38.38 | **22.57** | 27.14 | **+20%** ❌ |

### The verdict against the rule
- **App-like improves ≥ 10%:** passed, just (−10.1%).
- **No workload regresses > 5%:** **failed** on realloc growth (+20%).
- **Footprint no more than 10% worse:** **failed** on realloc growth (+40%). The main probe passed after v2.

→ **Opt-in.** The slab is a measured win for small-object-heavy code (app-like −10% time, +14% utilisation), and it's available as `myalloc::Allocator`.

### What the measurements explain, and what they don't
- **Adversarial −29% is an artifact, not a gain.** That trace keeps its holes apart with 24 B "pin" blocks. With the slab, the pins live in slabs, so the heap's holes become neighbours and coalesce. The heap's free list averages **1.0** block per malloc instead of 358. The trace stops being adversarial; a variant with pins > 256 B would restore it.
- **The realloc-growth regression: three hypotheses tested, all refuted** ([realloc-mechanism.txt](../results/slab/realloc-mechanism.txt)):
  1. **"Slab objects can't grow in place, so reallocs copy more":** no. Moves are 74% with the slab vs. 77% without.
  2. **"Half-empty slabs waste the memory":** only partly. Slabs are 89% full at the peak (287 KiB wasted), which explains ~13% of the 2.2 MB gap.
  3. **"Taking slabs extends the arena":** no. Only 19 of 172 new slabs raised the high-water mark, causing 9% of its growth.

  So 91% of the extra high-water comes from the heap's own 0.3–64 KiB buffers landing higher in the arena. **Leading hypothesis, unverified:** long-lived 16 KiB slabs scattered through the heap break up the large free regions those buffers need. Confirming it would need a placement trace of free-region sizes over time. Not done: the decision doesn't depend on it.
- **`aligned_malloc` searches conservatively:** it requests `need + alignment + 32 B` and then aligns, rather than testing whether an already-aligned hole fits exactly. That costs search reach, but hypothesis 3 shows it isn't what drives the regression.

### Other consequences
- **`BasicHeap::aligned_malloc(alignment, n)`** is a heap API now (power-of-two alignment ≥ 16; the front gap is split off as a free block). Phase 15's aligned `operator new` can build on it.
- **Opt-in for C++ only.** The C API (`my_malloc`, tracking, leak report) is unchanged and stays on the segregated-fit heap. Wiring the slab into the C API would be a build-time switch, deliberately not added while the slab fails the rule.

## Validation
- **New suites:**
  - [tests/test_aligned.cpp](../../tests/test_aligned.cpp) (4 tests): alignment for every power of two up to 64 KiB, with front gaps and tails merging back; invalid alignments rejected; exact 16 KiB blocks tile 16 KiB apart; a stress test mixing aligned and plain allocations with `check()` after every op.
  - [tests/test_slab.cpp](../../tests/test_slab.cpp) (16 tests):
    - **basics:** every size 0–256 in its class; slabs are exact 16 KiB heap blocks; no overlap; lowest-first slot reuse;
    - **bad frees:** exact double-free detection; invalid and foreign pointers (a plain heap block is `NotMine`);
    - **memory:** an emptied slab returns its 16 KiB to the heap; the last partial slab is kept; after filling the heap with slabs and freeing them, half the arena is allocatable as one block;
    - **robustness:** `for_each_live` lists exactly the live objects; a damaged header stops the cache safely;
    - **front end:** routing, moves between slab and heap, user-level stats;
    - **fuzz:** a **use-after-free test that requires the slab *and* heap checks to pass after every operation** (50 seeds × 4,000 ops, with whole dangling slots overwritten).
- **Mutation checks** (sources restored):
  - free not re-setting the slot's bit fails 8 tests (`slab live count disagrees with bitmap`);
  - a released slab keeping its chunk bit fails 2 (`invalid slab header`).
- 384/384 runs (128 tests × 3 policies) pass under UBSan and under ASan + UBSan. The 6 footprint reference outputs are unchanged, since the C API path is untouched.
