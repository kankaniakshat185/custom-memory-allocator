Samples inside replay(): 6499 (1 ms each), trace `/var/folders/d4/kllzklmj2lsb1kkc1lvgvvp80000gn/T//prof/cur_bf_rand.trace`

### By category

| Share | Samples | Where |
|---|---|---|
|  92.5% | 6011 | search: walk + per-node check |
|   2.1% | 138 | malloc body |
|   1.7% | 113 | harness (replay loop, touch) |
|   1.3% | 84 | free: merge (release) |
|   0.8% | 55 | unlink: safe-unlinking checks |
|   0.4% | 24 | malloc: split (place) |
|   0.3% | 22 | free body |
|   0.3% | 17 | free: pointer validation |
|   0.2% | 12 | outside binary (libsystem_kernel.dylib) |
|   0.2% | 11 | unlink: relink neighbours |
|   0.1% | 8 | list push |
|   0.1% | 4 | stats / extent |

### By innermost (inlined) function

| Share | Samples | Where |
|---|---|---|
|  42.3% | 2747 | BestFit::find |
|  41.5% | 2698 | BasicHeap::find_fit |
|   7.0% | 453 | is_alloc |
|   2.1% | 138 | BasicHeap::in_heap |
|   2.0% | 130 | BasicHeap::malloc |
|   1.6% | 103 | replay |
|   1.1% | 72 | BasicHeap::release |
|   0.4% | 29 | BasicHeap::unlinkable |
|   0.4% | 24 | BasicHeap::place |
|   0.3% | 22 | BasicHeap::free |
|   0.2% | 14 | BasicHeap::validate |
|   0.2% | 12 | libsystem_kernel.dylib: mach_absolute_time |
|   0.2% | 10 | Arena::contains |
|   0.1% | 9 | BasicHeap::unlink |
|   0.1% | 9 | BasicHeap::plausible_free |

### By source line

| Share | Samples | Where |
|---|---|---|
|  32.2% | 2091 | heap.cpp:93 (BasicHeap::find_fit) |
|  20.0% | 1301 | heap.h:61 (BestFit::find) |
|  17.0% | 1102 | heap.h:59 (BestFit::find) |
|   9.3% | 603 | heap.h:0 (BasicHeap::find_fit) |
|   7.0% | 453 | block.h:34 (is_alloc) |
|   5.3% | 343 | heap.h:63 (BestFit::find) |
|   1.6% | 104 | heap.cpp:174 (BasicHeap::malloc) |
|   1.2% | 79 | heap.cpp:40 (BasicHeap::in_heap) |
|   0.9% | 59 | bench_main.cpp:96 (replay) |
|   0.9% | 59 | heap.cpp:39 (BasicHeap::in_heap) |
|   0.6% | 38 | bench_main.cpp:98 (replay) |
|   0.5% | 31 | heap.cpp:0 (BasicHeap::release) |
|   0.4% | 23 | heap.cpp:133 (BasicHeap::release) |
|   0.3% | 17 | heap.cpp:109 (BasicHeap::place) |
|   0.2% | 15 | ? (BasicHeap::malloc) |
