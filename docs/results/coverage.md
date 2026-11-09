# Test coverage (2026-11-09)

clang source-based coverage, all four test binaries merged: `myalloc_tests` (segregated fits), `myalloc_tests_firstfit`, `myalloc_tests_bestfit` and `myalloc_new_tests` (the global `operator new` replacement).

| Scope | Lines | Functions | Branches |
|---|---|---|---|
| Allocator core (`heap.cpp`, `arena.cpp`, `heap.h`, `block.h`, `arena.h`) | **99.3%** (3 of 442 missed) | **100%** (56) | 89.1% |
| Everything in `src/` and `include/` | 95.2% | 98.7% (2 of 150 missed) | 80.7% |

The previous report (2026-10-25: core 99.6% lines, 100% functions) covered only the files that existed then.

**The two functions never executed:**
- `leak_report_at_exit`. It runs at process exit, and the tests exercise it only in forked children (ADR-013). Children that end in `abort()` or `_exit()` skip writing profile data, so their work counts as uncovered.
- The failure lambda inside `SlabCache::check`. It only runs when a slab invariant is broken. The mutation checks break invariants deliberately (ADR-014), but not in a coverage build.

**Where uncovered lines come from:**
- **Children that don't write profile data.** In `myalloc.cpp`, the abort diagnostics run only inside death-test children, and the fork handlers' child side (`after_fork_child`) runs only in the children of the fork test.
- **Defensive branches** in `slab.h` and `allocator.h` (corrupted slab headers, impossible slot states).

`warning: 1 functions have mismatched data` comes from merging profiles of the same header-defined function compiled under three different policies.

```
Filename                          Regions    Missed Regions     Cover   Functions  Missed Functions  Executed       Lines      Missed Lines     Cover    Branches   Missed Branches     Cover
---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
src/lock.h                              4                 0   100.00%           3                 0   100.00%           3                 0   100.00%           0                 0         -
src/heap.cpp                          310                21    93.23%          21                 0   100.00%         315                 3    99.05%         232                28    87.93%
src/arena.cpp                          25                 1    96.00%           5                 0   100.00%          26                 0   100.00%          14                 2    85.71%
src/slab.h                            226                28    87.61%          25                 1    96.00%         222                16    92.79%         154                39    74.68%
src/allocator.h                        55                10    81.82%           9                 0   100.00%          61                 8    86.89%          46                17    63.04%
src/new_delete.cpp                     36                 0   100.00%          23                 0   100.00%          50                 0   100.00%           6                 0   100.00%
src/heap.h                             78                 0   100.00%          15                 0   100.00%          73                 0   100.00%          46                 2    95.65%
src/block.h                            15                 0   100.00%          12                 0   100.00%          25                 0   100.00%           2                 0   100.00%
src/arena.h                             3                 0   100.00%           3                 0   100.00%           3                 0   100.00%           0                 0         -
src/myalloc.cpp                       123                28    77.24%          28                 1    96.43%         198                21    89.39%          76                23    69.74%
include/myalloc/allocator.hpp          18                 2    88.89%           6                 0   100.00%          16                 0   100.00%           5                 1    80.00%

Files which contain no functions:
include/myalloc/myalloc.h               0                 0         -           0                 0         -           0                 0         -           0                 0         -
---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
TOTAL                                 893                90    89.92%         150                 2    98.67%         992                48    95.16%         581               112    80.72%
```

How (as CI does): configure `build-cov` with `-fprofile-instr-generate -fcoverage-mapping`, run the four binaries with `LLVM_PROFILE_FILE` set, then `xcrun llvm-profdata merge` and `xcrun llvm-cov report` ([testing.md](../testing.md)).
