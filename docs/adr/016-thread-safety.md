# ADR-016: Thread safety: per-thread arenas, not one global lock

- Status: Accepted. Per-thread arenas are the C API's default. The scaling rule passed on a busy machine and is re-checked on a quiet one in Phase 17.
- Date: 2026-11-08
- Environment: see [../environment.md](../environment.md); native arm64, `-O2` Release; Apple M2 (4 performance + 4 efficiency cores)
- Raw data: [results/mt/](../results/mt/) (see its README)
- Code: [src/lock.h](../../src/lock.h), arenas and fork handling in [src/myalloc.cpp](../../src/myalloc.cpp), [bench/mt_bench.cpp](../../bench/mt_bench.cpp), [tests/test_threads.cpp](../../tests/test_threads.cpp)

## Context
Until this phase, the C API was single-threaded: one `Heap`, no synchronisation. Since ADR-015 that also covered every `new` in a program that links `myalloc_new`. Plan §1 makes thread safety the last Tier 2 item, in two steps:

1. A global lock, measured.
2. Per-thread caches or arenas, only if the data asks for them.

`BasicHeap` stays single-threaded. It is the building block, and code that owns one heap per thread should pay nothing for synchronisation.

## Decision
1. **A multithreaded harness** ([mt_bench.cpp](../../bench/mt_bench.cpp)).
   - **Traces:** N threads each replay their own seeded trace (`app_like` or `random`, seed 1000 + thread index; the generators gained an optional seed, with the published defaults unchanged).
   - **Timing:** the threads start at a barrier, and the clock stops when the last one finishes.
   - **Threads persist across reps:** they stay alive across a configuration's reps, as long-lived threads would. An earlier version started fresh threads per rep, so each one was assigned a new, never-touched arena and paid page faults the single-arena variants did not.
2. **Step 1, the global lock, measured and rejected** (table below). Every lock tried, whether `std::mutex`, `os_unfair_lock` or a spinlock, gives *negative* scaling: 2 threads deliver less total throughput than 1.
3. **Step 2: per-thread arenas.**
   - **Arenas:** up to 8 (`MYALLOC_ARENAS`), each a whole 256 MiB `Heap` with its own lock.
   - **Assignment:** a thread is assigned an arena round-robin on its first allocation, and arenas are created lazily. Arena 0 is the one a single-threaded program uses, so its behaviour, stats and tests are unchanged.
   - **Frees:** a free goes to the arena that owns the pointer, checking the caller's own arena first, then the others. Cross-thread frees work, and every validation and hardening check (ADR-012) runs under the owning arena's lock.
   - **Exhaustion:** when a thread's arena is full, the allocation falls back to the other arenas that exist. `realloc` stays in the owning arena.
   - **Stats and leak report:** they sum and walk all arenas, each under its lock. With one arena every field is exact. With several, the totals are not one atomic snapshot, and `peak_bytes_in_use` is the sum of per-arena peaks (an upper bound).
4. **The lock is `os_unfair_lock` on macOS** (what libmalloc uses: no fairness, and the kernel boosts the owner's priority, so an efficiency-core thread holding it cannot be starved), and `std::mutex` elsewhere. `MYALLOC_LOCK` selects one for measurement only.
5. **Each arena's `Heap` and lock start on their own 128 B cache lines.** Packed, arena *i*'s lock shared a line with the start of arena *i+1*'s `Heap`, and `app_like` stopped scaling at 2 threads (below).
6. **`fork()` is safe.**
   - **Fork handlers:** `pthread_atfork` handlers hold every arena lock and the creation mutex across `fork()`. The parent then releases them, and the child re-initialises them, because it cannot unlock locks recorded as owned by threads that don't exist in the child.
   - **Eager init:** arena 0 is initialised before `main()`. Lazily, a `fork()` from one thread while another was inside the one-time initialisation, before the handlers were registered, left the child blocked forever on the static-initialisation guard.
7. **ThreadSanitizer** builds (`-DMYALLOC_TSAN=ON`) run the whole suite in CI on macOS and Linux.

**Rule, fixed before step 2 was measured:** per-thread arenas become the default if all three hold:
1. single-thread cost is within 10% of the global-lock build;
2. `app_like` throughput at 4 threads is at least 2.5× the 1-thread figure;
3. the footprint outputs stay byte-identical.

## Alternatives considered
1. **Stop at a global lock.** Rejected on the data. At 2 threads, total throughput falls to 0.19× (`app_like`) and 0.28× (`random`) of one thread.
2. **A spinlock.** It degraded least under contention (24–30 Mops/s at 8 threads, against 7–9 for the sleeping locks), but it still scales negatively. It burns a core while waiting, and on a mix of performance and efficiency cores a spinning waiter can delay a descheduled owner. Rejected; contention has to be removed, not made cheaper.
3. **Per-thread caches of small blocks over a shared heap** (tcmalloc-style). They avoid the lock on most operations, but a cached block is "allocated" as far as the heap can tell. That would put it out of reach of:
   - `my_alloc_stats` and the leak report (ADR-013);
   - double-free detection (a second free of a cached block cannot be seen);
   - the heap checker.

   Marking cached blocks in their header would race with neighbouring merges under the lock. Rejected: arenas keep every existing guarantee.
4. **One arena per thread, unbounded.** No sharing at all, but every thread would reserve 256 MiB of address space, and short-lived threads would strand their memory. Rejected for a fixed set of 8, reused round-robin.
5. **Owner lookup by address arithmetic** (all arenas in one contiguous reservation, so the index is `(p - base) / size`). O(1) instead of a scan, but it means changing `Arena`/`BasicHeap::init`. The scan over at most 8 ranges, own arena first, measured +0.7–0.9% instructions over the global lock, so it isn't needed.
6. **Locking inside `BasicHeap`.** It would make every heap pay, including single-owner ones and the benchmarks of ADR-008–014. Rejected: synchronisation belongs to the shared C API layer.

## Consequences

### Scaling (Mops/s, two rounds; [results/mt/](../results/mt/))
| Trace | Variant | 1 thread | 2 | 4 | 8 |
|---|---|---|---|---|---|
| app_like | libmalloc | 43.4 (noisy) / 55.4 | 48.4 (noisy) / 84.8 | 89.6 / 117.1 (noisy) | 75.7 / 79.2 |
| app_like | **arenas** | 47.5 / 46.2 | 92.6 / 86.2 | **137.8 / 159.8** | 154.5 / 171.7 |
| app_like | global lock | 43.5 / 46.2 | 8.1 / 8.1 | 9.5 / 9.3 | 10.4 / 7.7 |
| random | libmalloc | 32.9 / 29.6 | 32.1 / 36.6 | 33.5 / 39.4 | 26.2 / 28.3 |
| random | **arenas** | 21.3 / 21.7 | 41.7 / 40.6 | **68.2 / 72.0** | 75.8 / 86.1 |
| random | global lock | 21.5 / 21.8 | 6.0 / 6.1 | 4.3 / 4.1 | 3.0 / 2.7 |

- **Arenas scale nearly linearly to 2 threads and keep scaling at 4.** At 4 threads they reach 2.9–3.5× the 1-thread figure. At 8 threads they still gain, though less, as efficiency cores join.
- **libmalloc reaches about 2.1× at 4 threads on `app_like` and barely scales on `random`** (1.0–1.3×). So at 4 threads the arenas are 1.4–1.5× (`app_like`) and 1.8–2.0× (`random`) ahead of libmalloc.
  - **Why libmalloc doesn't scale on `random` is not established.** `random` includes requests up to 64 KiB, outside libmalloc's per-CPU small-object magazines, which may share state. This was not profiled.
- **Single-threaded, libmalloc is ahead on `random`** (about 31 vs. 21.5 Mops/s). Even with no lock, the C API reaches only about 27.
  - **This contradicts ADR-011,** where segregated fits beat libmalloc on random sizes 1.29×. That measurement called `BasicHeap` directly, on a different trace seed and with a pre-faulted arena per rep. The C API path and this harness have not been profiled, so the gap is open for Phase 17.

### Cost of thread safety, single-threaded
| | `app_like` instructions/rep | `random` instructions/rep |
|---|---|---|
| no lock | 235.1 M | 341.1 M |
| global lock | 263.4 M (+12.1%) | 368.6 M (+8.1%) |
| **arenas** | **265.2 M (+0.7% vs. global lock)** | **371.7 M (+0.9%)** |

- **In instructions,** the lock (an `os_unfair_lock` lock and unlock per call) is the cost. Arena lookup adds under 1% on top of it.
- **In wall-clock time it varies by trace:**
  - `app_like` shows no measurable difference (46.9 vs. 46.7 Mops/s with no lock);
  - `random` loses about 10 ns per operation (21.5 vs. 27.5).

  An atomic read-modify-write costs latency that instruction counts don't see. Why it shows on one trace and not the other is not established.
- **Earlier data understated this.** Before the harness kept threads alive, arenas measured +4.5–6.7% over the global lock; the difference was page faults on fresh arenas.

### Lock implementations under one global lock ([lock_choice.csv](../results/mt/lock_choice.csv), scratch builds, loaded machine)
| Lock | 1 thread | 2 | 4 | 8 |
|---|---|---|---|---|
| none | 57.4 / 52.3 | | | |
| `std::mutex` | 43.2 / 41.2 | 21.7 / 21.4 | 9.9 / 10.5 | 7.0 / 6.8 |
| `os_unfair_lock` | 31.9 (noisy) / 49.0 | 8.2 / 8.4 | 10.3 / 10.7 | 9.2 / 9.3 |
| spinlock | 34.7 / 43.7 | 34.0 / 34.6 | 21.0 / 30.2 | 24.5 / 29.5 |

Each lock only changes *how badly* throughput collapses. That is the evidence for step 2.

### False sharing ([false_sharing.csv](../results/mt/false_sharing.csv))
With arena slots packed, `app_like` ran at 84–86 Mops/s at 4 threads, no faster than at 2 (87). With each `Heap` and lock on its own 128 B lines it ran at **167–170**, with per-thread cost flat at about 23 ns/op instead of doubling. Why 2 threads were unaffected is not established. One plausible reason is which pairs of arenas the threads happened to share lines with.

### Rule
| Rule (set before measuring step 2) | Result |
|---|---|
| Single-thread within 10% of global lock | +0.7% / +0.9% instructions ✅ |
| `app_like` 4 threads ≥ 2.5× 1 thread | 2.9× / 3.5× ✅ (on a busy machine; re-checked in Phase 17) |
| Footprint outputs byte-identical | 6/6 ✅ (the probe uses its own heaps; the C API's placement is unchanged) |

### Other consequences
- **Address space.** Up to 8 × 256 MiB reserved. Pages are only touched when used, and a single-threaded program still reserves exactly one arena.
- **Memory is not shared between arenas.** A thread whose arena is fragmented doesn't benefit from free space in another until its own arena is exhausted. Freed memory goes back to the owning arena, never to the freeing thread's.
- **`my_realloc` doesn't move a block across arenas.** If the owning arena is full, it returns NULL even when another arena has room.
- **ADR-015's `operator new` measurements predate the lock.** The shipped `myalloc_new` now pays it on every call (+8–12% instructions single-threaded, measured here on traces, not containers). `run_stl.sh` is re-run in Phase 17 anyway.
- **`default_heap()` is arena 0, unlocked.** Tests and probes use it directly; it is documented as not for concurrent use.

## Validation
- **New tests:** [test_threads.cpp](../../tests/test_threads.cpp) adds 6 tests to the main suite, so they run under all three policies:
  - 8-thread churn with fill patterns verified and every arena `check()`ed;
  - consecutive threads landing in distinct arenas, and freeing each other's blocks;
  - producer/consumer cross-thread frees (50,000 blocks);
  - exhaustion falling back to another arena;
  - stats and the leak report covering another thread's arena;
  - 20 `fork()`s while 4 threads allocate, each child required to allocate and exit within 5 s.
- **Totals:** 142 tests × 3 + 13 = **439 runs**, all passing under UBSan, ASan + UBSan and **ThreadSanitizer (0 reports)**, and with every binary also run directly.
- **Mutation checks** (2026-11-08, sources restored):
  - **No locks** (`MYALLOC_LOCK=-1`): TSan reports only 4 races, because threads mostly stay in their own arenas, and the process aborts because the heap's hardening (ADR-012) detects the corrupted free lists.
  - **No fork handlers:** the fork test failed 3 of 3 runs (the child deadlocked and the alarm killed it).
  - **Lazy arena-0 initialisation:** the fork test hung in **96 of 100** fresh-process runs, against **0 of 100** with eager initialisation. That is how the bug was found: one hang in `ctest`'s fresh processes, never in a whole-binary run, where an earlier test had already initialised the heap.
- **Bugs found while measuring,** each fixed and recorded above:
  - fresh threads per rep penalised the arenas;
  - the main thread's spinning inflated instruction counts (it now sleeps on a condition variable);
  - false sharing between arena slots.
