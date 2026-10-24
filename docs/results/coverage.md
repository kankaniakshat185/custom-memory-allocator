# Test coverage (2026-10-25)

clang source-based coverage, both test binaries (first-fit + best-fit) merged. Death-test children exit via abort(), which skips writing profile data, so the abort/diagnostic paths in myalloc.cpp show as uncovered although death tests execute them.

```
Filename                      Regions    Missed Regions     Cover   Functions  Missed Functions  Executed       Lines      Missed Lines     Cover    Branches   Missed Branches     Cover
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
heap.cpp                          271                19    92.99%          20                 0   100.00%         263                 1    99.62%         202                29    85.64%
arena.cpp                          25                 1    96.00%           5                 0   100.00%          26                 0   100.00%          14                 3    78.57%
heap.h                             28                 0   100.00%           6                 0   100.00%          30                 0   100.00%          16                 0   100.00%
block.h                            15                 0   100.00%          12                 0   100.00%          25                 0   100.00%           2                 0   100.00%
arena.h                             3                 0   100.00%           3                 0   100.00%           3                 0   100.00%           0                 0         -
myalloc.cpp                        19                 8    57.89%           7                 0   100.00%          36                10    72.22%          14                 6    57.14%
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
TOTAL                             361                28    92.24%          53                 0   100.00%         383                11    97.13%         248                38    84.68%
```

How: cmake -S . -B build-cov -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping"; run both test binaries with LLVM_PROFILE_FILE set; xcrun llvm-profdata merge; xcrun llvm-cov report.
