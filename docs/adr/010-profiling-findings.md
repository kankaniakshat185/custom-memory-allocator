# ADR-010: Profiling findings — where the time goes, and why

- Status: Accepted
- Date: 2026-10-25
- Environment: see [../environment.md](../environment.md); Instruments 16.0 (`xctrace`), native arm64, `-O3 -g`
- Raw data and exact commands: [results/profiles/](../results/profiles/) (see its README)
- Tools added: [bench/scripts/profile_attrib.py](../../bench/scripts/profile_attrib.py), [bench/scripts/counters_summary.py](../../bench/scripts/counters_summary.py), [bench/freelist_length.cpp](../../bench/freelist_length.cpp)

## Context
ADR-005 to ADR-012 measured *what* happens (footprint, ns/op) and left several *why* questions open:

1. ADR-001: does the free-list search dominate, and when?
2. ADR-006: why is Phase 1 2.8× slower than Phase 3 on `adversarial_holes`?
3. ADR-012: where does hardening's fixed ~4–5 ns per operation (first-fit) go, and is it instructions or stalls?
4. ADR-011 (pending scope decision): would segregated free lists fix the measured problems?

This is the same profiling methodology as Matching Engine ADR-0022: Time Profiler for where, CPU Counters bottleneck breakdown for why.

## Decision (method)
1. **Profile the code that is benchmarked.** The same `-O3 -DNDEBUG -arch arm64` flags as the timing builds, plus `-g` (which does not change code generation). Phase commits are built from `git archive` as in ADR-008.
2. **Attribute through inlining.** At `-O3` the helpers (`in_heap`, `plausible_free`, `unlinkable`, `place`, …) are inlined into `malloc`/`free`, so Instruments' function view cannot see them. `profile_attrib.py` exports every Time Profiler sample's leaf address and symbolizes it with `atos -i`, which returns the inlined chain with source lines. Time is then aggregated by innermost function, by category and by line. Only samples inside the timed `replay()` loop count, which excludes trace generation.
3. **Comparing two builds:** both run identical traces and rep counts, so absolute sample counts per category (1 ms each) can be compared directly. Their totals match the timing benchmark's ratio, which serves as a consistency check.
4. **Why, not just where:** CPU Counters' bottleneck breakdown (per counter sample, cycle-weighted by `counters_summary.py`). Pipeline slots are classed as *useful* (retiring), *delivery*-bound (front end), *processing*-bound (back end, including memory) or *discarded* (mis-speculation). The exported per-sample `MetricTable` is used; the per-interval aggregate table sums ratios, so it is not usable as shares (found by checking that shares sum to 100%).
5. **Free-list length per malloc** (`freelist_length.cpp`, built against each phase), plus list nodes actually visited per malloc where the tree has the ADR-009 counter.

~5,000–6,000 samples per Time Profiler run; all runs on P cores (≥ 99% of cycles) on a quiet machine (load 2.0–2.7).

## Findings

### 1. On typical workloads, search is ~10% of the time; free is half of it
Current tree, first-fit, hardened. Share of samples inside `replay()`:

| Category | app-like | random |
|---|---|---|
| free: merge (`release`) | **32.7%** | **31.5%** |
| unlinking: safe-unlinking checks + relink (malloc and free) | 18.3% | 20.0% |
| harness: replay loop, op dispatch, 8-byte touch | 19.0% | 20.1% |
| **search: walk + per-node check** | **9.7%** | **8.9%** |
| malloc body + split (`place`) | 10.6% | 8.7% |
| free body + pointer validation | 6.0% | 6.6% |
| stats, list push, other | 3.7% | 4.3% |

**Why the search is so cheap** (`freelist-length.txt`): the free list holds 855 (random) to 1,610 (app-like) blocks at a typical malloc, but **LIFO first-fit visits only 1.8–2.0 of them**. The most recently freed block or split remainder at the head almost always fits. ADR-001's worry that search would dominate is wrong for first-fit on typical workloads. It holds for best-fit (57–219 nodes visited per malloc; 92.5% of best-fit's random-workload time is search) and for the adversarial case below.

### 2. Two different bottleneck regimes (CPU Counters, cycle-weighted)
| Run | Useful | Delivery | Processing (back end / memory) | Discarded (mis-speculation) |
|---|---|---|---|---|
| unhardened (Phase 6), app-like | 23.7% | 18.6% | 8.2% | **49.5%** |
| unhardened (Phase 6), random | 24.5% | 19.2% | 4.7% | **51.6%** |
| hardened (current), app-like | 35.5% | 21.2% | 6.8% | **36.6%** |
| hardened (current), random | 35.8% | 22.0% | 5.6% | **36.5%** |
| current, adversarial | 19.1% | 0.4% | **80.1%** | 0.4% |
| current best-fit, random | 39.8% | 1.6% | **47.8%** | 10.7% |
| Phase 1, adversarial | 5.3% | 0.1% | **94.2%** | 0.4% |

- **Typical workloads are bound by branch mis-speculation, not memory.** About half of all pipeline slots in the unhardened allocator are discarded. Each operation is ~50 cycles of data-dependent decisions (is the neighbour free? does this block fit? split or not?) on random sizes, and the M2's predictor cannot learn them. Caveat: the harness's `switch` on a random alloc/free sequence is also unpredictable and is included. The counter samples carry no call stacks, so the split between harness and allocator is not measured. libmalloc runs through the same harness, so comparisons between allocators are unaffected.
- **Long walks are bound by memory latency.** Adversarial and best-fit are 48–94% back-end bound, with almost no mis-speculation. The walk is a dependent pointer chase through cold nodes (each `next` must arrive before the next load can start), and its loop branch is predictable. Phase 1 is the extreme case: 94% stalled and 5% useful.

### 3. The hardening cost is extra instructions on the free path (closes ADR-012's open question)
Identical work, so absolute numbers compare directly:
- **Cycles:** app-like 14.33 G → 18.22 G (**+27%**), random 13.85 G → 18.19 G (+31%). Time Profiler samples: +27.1% / +28.3%. Timing benchmark: +28%. Three methods agree.
- **Slot-cycles on app-like** (cycles × share):

  | Slot class | Change |
  |---|---|
  | useful | **3.40 G → 6.47 G (+3.07 G)** |
  | delivery | +1.20 G |
  | processing (memory) | **1.18 → 1.24 G (flat)** |
  | discarded | **7.09 → 6.67 G (down)** |

  So the cost is **more instructions retired**, plus the front end fetching more code. It is not cache misses, and not more mispredicts.
- **Where:** of the +1,169 extra samples on app-like, **+773 are in `release`** and +236 net in unlinking. Part of the old "relink" time moved into the new checks, which now take the first touch of each neighbour's links. That puts **86% on the free path**. The search got slightly *cheaper* (585 → 530 samples): its per-node check is cheaper than the old loop's layout. The remaining ~+300 are spread over the harness, `note_extent` and the `malloc` body, whose code is unchanged, so this is inlining and layout shift, not new work.
- **Not resolved:** *which statement* inside `release` costs most. 767 of its samples carry line 0 (compiler-generated code without line info), so line attribution inside the function isn't reliable.

### 4. Phase 1's adversarial slowness is list growth (closes ADR-006's open question)
| | Phase 1 | Phase 3 / current |
|---|---|---|
| free-list length at malloc, mean (p99) | **3,039 (6,138)** | 358 (2,932) |
| mean length by tenth of the trace | **2,107 → 3,050 → … → 3,187 (grows)** | 358 in every tenth (flat) |
| search share of time | 98.4% | 97.7% |
| back-end (memory) bound | 94.2% | 80.1% |

Without coalescing, each round's holes and pins remain separate list nodes, so the list grows across rounds. Phase 3 returns to the same state every round. The hypothesis is supported. The 8.5× longer list makes Phase 1 only 2.8× slower because first-fit stops at the first fit; it doesn't walk the whole list.

### 5. Adversarial: the search is the whole cost
97.7% of samples are in the search, and 80% of slots are stalled on memory. First-fit and best-fit both visit 287 nodes per malloc on average (2,000 for each of the 1,000 large requests per round). libmalloc does the same trace at 12.4 ns/op because a size-segregated structure never visits the 1–2 KiB holes when asked for 4,000 B.

## Consequences

### What this means for ADR-011 (segregated free lists)
The profile separates what size classes would and wouldn't fix:
- **Would fix: the adversarial worst case** (97.7% search, memory-bound walks over the wrong size), **best-fit's search cost** (92.5% search), and **first-fit's app-like footprint** (ADR-009: best-fit placement gives 2.7× less memory; size classes give good-fit placement without a full walk). These are three of the measured problems.
- **Would not fix: average-case speed against libmalloc on app-like** (21.3 vs. 18.7 ns/op after hardening). Search is only ~10% of first-fit's time there, so making it O(1) can save at most ~10%. Size-class computation would also add work. The time is in the free path and in mis-speculation. The data points instead at a **small-object fast path** (the Tier 2 slab item), consistent with ADR-012's observation that the heap is level with libmalloc once libmalloc's own small-object fast path (the Nano zone) is turned off.

So: segregated lists are justified by **memory and worst-case latency**, not by average speed. That decision, and its scope, is for the plan (ADR-011).

### Other consequences
- ADR-001's hypothesis ("allocation cost is dominated by free-list search length") is **refuted for first-fit on typical workloads** (search ~9–10% of time, ~2 nodes per malloc) and **supported for best-fit and fragmented or adversarial heaps** (92–98% search).
- If anyone optimises the average case further, the first target is the free path (`release` + unlinking, ~51% of time), then branchiness generally. The memory system is not the bottleneck there.
- Tooling limits, recorded: 1 ms Time Profiler sampling (enough at ~5,000 samples per run, not for rare events); counter samples without call stacks; `-g` line attribution blurred by line-0 code. Processor Trace (exact instruction-level timing) was not used.

## Validation
- Consistency between methods: the hardening slowdown measures +27–28% (Time Profiler samples), +27–31% (CPU Counters cycles) and +28% (timing harness, ADR-012).
- Every CPU Counters row's slot shares sum to 100% (checked in the summary's "Sum" column). The first, aggregate-table version produced shares above 100%, which is how the wrong table was caught and replaced.
- The profile attribution script was checked against a known structure: on the adversarial workload, whose cost is by construction the search, it attributes 97.7–98.4% to the search category. Its first version mislabelled Phase 1's differently named search function (`find_first_fit`) as harness time; that was found and fixed before any result was used.
- All profiled binaries are arm64 and were run with `MallocNanoZone` unset (ADR-008).
