# Multithreaded throughput and scaling (ADR-016)

Produced by `bench/scripts/run_mt.sh build` (Release, native arm64) on 2026-11-08. N threads each replay their own seeded trace (`app_like` or `random_16B_64KiB`, seed 1000 + thread index); the threads live across the reps of a configuration; throughput is total operations ÷ wall time.

| Variant | Binary | What it is |
|---|---|---|
| `libmalloc` | `mt_bench --alloc system` | macOS malloc, default configuration |
| `arenas` | `mt_bench --alloc myalloc` | the shipped C API: per-thread arenas, one `os_unfair_lock` each |
| `global_lock` | `mt_bench_global` | the same C API built with `MYALLOC_ARENAS=1`: one heap, one lock |
| `no_lock` | `mt_bench_nolock` | `MYALLOC_ARENAS=1 MYALLOC_LOCK=-1`: not thread-safe, 1 thread only, the cost of locking |

The M2 has 4 performance and 4 efficiency cores, so 8 threads mix core types. `BTLEServer` was stuck at ~100% CPU during every run (load average 7–9 for the final run), which takes most of one core.

## Files
- `mt.csv`: throughput, two alternating rounds ([machine-state.txt](machine-state.txt)).
- `instructions_1thread.csv`: single-thread instructions retired per rep, (5 reps − 0 reps) ÷ 5, median of 3 runs each. This includes the kernel's work, such as page faults.
- `lock_choice.csv`: an earlier scratch comparison of lock implementations.
- `false_sharing.csv`: arena slots packed vs. padded to 128 B.

## Throughput (Mops/s, round 1 / round 2; higher is better)
| Trace | Variant | 1 thread | 2 | 4 | 8 |
|---|---|---|---|---|---|
| app_like | libmalloc | 43.4 (noisy) / 55.4 | 48.4 (noisy) / 84.8 | 89.6 / 117.1 (noisy) | 75.7 / 79.2 |
| app_like | **arenas** | 47.5 / 46.2 | 92.6 / 86.2 | **137.8 / 159.8** | 154.5 / 171.7 |
| app_like | global_lock | 43.5 / 46.2 | 8.1 / 8.1 | 9.5 / 9.3 | 10.4 / 7.7 |
| app_like | no_lock | 47.5 / 45.8 | | | |
| random | libmalloc | 32.9 / 29.6 | 32.1 / 36.6 | 33.5 / 39.4 | 26.2 / 28.3 |
| random | **arenas** | 21.3 / 21.7 | 41.7 / 40.6 | **68.2 / 72.0** | 75.8 / 86.1 |
| random | global_lock | 21.5 / 21.8 | 6.0 / 6.1 | 4.3 / 4.1 | 3.0 / 2.7 |
| random | no_lock | 28.6 / 26.3 | | | |

"noisy" = IQR above 10% of the median (`mt.csv` has every IQR).

## Single-thread instructions per rep (millions)
| Trace | libmalloc | no_lock | global_lock | arenas |
|---|---|---|---|---|
| app_like | 203.9 | 235.1 | 263.4 (+12.1%) | 265.2 (+0.7% vs. global) |
| random | 323.9 | 341.1 | 368.6 (+8.1%) | 371.7 (+0.9% vs. global) |
