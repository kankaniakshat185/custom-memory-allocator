# Profiles (ADR-010)

All recorded 2026-10-25 on the machine in [environment.md](../../environment.md), quiet (load 2.0–2.7), native arm64.
Profiled binaries are the timing harness built exactly like the benchmarks plus `-g`:
`clang++ -std=c++17 -O3 -g -DNDEBUG -arch arm64 -I<tree>/src -I<tree>/include <tree>/src/*.cpp bench/bench_main.cpp bench/workloads.cpp`
where `<tree>` is the working tree (`current`, = commit `4276082`) or `git archive <commit> src include` of
phase 1 (`9baf140`), phase 3 (`0ad7359`) or phase 6 (`128bcc3`). The `.trace` bundles are not committed (size).

| File | How it was produced |
|---|---|
| `cur_app.md`, `p6_app.md`, `cur_rand.md`, `p6_rand.md` | `xctrace record --template 'Time Profiler' --launch -- bench --reps 250 --alloc heap --workload <app_like\|random_16B_64KiB>`, then `bench/scripts/profile_attrib.py <trace> <binary>` |
| `cur_adv.md` | same, `--reps 20 --workload adversarial_holes` |
| `cur_bf_rand.md` | same, `--reps 25 --alloc heap_bestfit --workload random_16B_64KiB` |
| `p1_adv.md` | phase 1 binary, `--reps 6 --workload adversarial_holes` |
| `cpu-counters.md` | `xctrace record --template 'CPU Counters'` with the same arguments (`cc_*` = same runs as above), then `bench/scripts/counters_summary.py` |
| `freelist-length.txt` | `bench/freelist_length.cpp` built against each tree; free-list length at every malloc, nodes visited per malloc where the tree has the counter |

Time Profiler samples every 1 ms; only samples whose stack is inside the harness's timed `replay()` loop
(inlined into `run_heap`) are counted. Leaf addresses are symbolised with `atos -i`, which reports
the *inlined* call chain, so helpers inlined at `-O3` (e.g. `in_heap`, `unlinkable`) are attributed to themselves.
