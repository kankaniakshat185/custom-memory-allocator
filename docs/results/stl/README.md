# Standard-container workloads under three `operator new` implementations (ADR-015)

Produced by `bench/scripts/run_stl.sh build` (Release, native arm64) on 2026-11-08.

| Binary | Global `operator new`/`delete` |
|---|---|
| `stl_bench_system` | libmalloc (default configuration, `MallocNanoZone` unset) |
| `stl_bench_myalloc` | `myalloc_new`: the shipped replacement, over the `my_*` C API (segregated-fit heap) |
| `stl_bench_slab` | bench-only: heap + small-object slab, `myalloc::Allocator` ([bench/new_delete_slab.cpp](../../../bench/new_delete_slab.cpp)) |

Workloads ([bench/stl_bench.cpp](../../../bench/stl_bench.cpp), seeded, identical work in every binary): `map_churn` (random-key `std::map`, 100k inserts then 400k erase+insert), `string_vector` (strings of 0–256 chars appended and replaced), `hash_count` (word counting into `unordered_map<string, int>`, then erasing half), `list_queue` (`std::list` FIFO of small nodes).

## Files
- `instructions.csv`: **the primary result.** Instructions retired per rep, from `/usr/bin/time -l`: (5-rep run − 0-rep run) ÷ 5, each run the median of 3. Process start-up and the warm-up rep cancel out. Repeated runs agree to ~0.1% even on the loaded machine; single runs had occasional outliers (one was 17% off), hence the median.
- `rss.csv`: peak resident set size of a 2-rep run. Rough: libmalloc's `map_churn` RSS was 6.7 MB in an earlier run and 11.4 MB here.
- `timing.csv`: **noisy, not used for any conclusion.** Median ns/op with IQR, two alternating rounds. `BTLEServer` was stuck at ~100% CPU (load average 15–30); round 1 has IQRs up to 115%. Re-run on a quiet machine in Phase 17.
- `machine-state.txt`: load average and top processes at the end of the run.

## Instructions per rep (millions; lower is better)
| Workload | libmalloc | myalloc (heap) | heap + slab | heap vs libmalloc | slab vs libmalloc | slab vs heap |
|---|---|---|---|---|---|---|
| map_churn | 480.2 | 391.8 | **346.3** | −18.4% | −27.9% | −11.6% |
| string_vector | 198.4 | 204.5 | **155.2** | +3.1% | −21.8% | −24.1% |
| hash_count | 303.5 | 358.2 | **288.1** | **+18.0%** | −5.1% | −19.6% |
| list_queue | 429.1 | 495.0 | **362.1** | **+15.4%** | −15.6% | −26.8% |

## Peak RSS (MB)
| Workload | libmalloc | myalloc (heap) | heap + slab |
|---|---|---|---|
| map_churn | 11.4 | 8.5 | 8.5 |
| string_vector | 40.8 | 34.9 | 35.1 |
| hash_count | 8.9 | 10.0 | 8.6 |
| list_queue | 17.5 | 17.6 | 18.1 |

Instructions retired are not time: they ignore cache misses and stalls, which ADR-010 found to dominate the heap's free path. They are used here because they were the only metric that stayed repeatable on this machine.
