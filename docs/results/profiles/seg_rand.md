Samples inside replay(): 6039 (1 ms each), trace `/var/folders/d4/kllzklmj2lsb1kkc1lvgvvp80000gn/T//prof/seg_rand.trace`

### By category

| Share | Samples | Where |
|---|---|---|
|  30.3% | 1832 | search: walk + per-node check |
|  16.1% | 971 | free: merge (release) |
|  15.0% | 908 | harness (replay loop, touch) |
|  11.7% | 708 | unlink: safe-unlinking checks |
|   6.4% | 389 | list push |
|   5.9% | 358 | malloc: split (place) |
|   4.1% | 249 | malloc body |
|   2.8% | 167 | free body |
|   2.6% | 160 | free: pointer validation |
|   1.8% | 108 | bin index + bitmap (list push/unlink) |
|   1.8% | 107 | unlink: relink neighbours |
|   1.1% | 68 | stats / extent |
|   0.2% | 12 | outside binary (libsystem_kernel.dylib) |
|   0.0% | 1 | outside binary (libsystem_c.dylib) |
|   0.0% | 1 | outside binary (libsystem_malloc.dylib) |

### By innermost (inlined) function

| Share | Samples | Where |
|---|---|---|
|  15.1% | 913 | BasicHeap::release |
|  15.0% | 905 | SegregatedFit::find |
|  13.5% | 815 | replay |
|   7.4% | 446 | BasicHeap::find_fit |
|   7.1% | 428 | BasicHeap::unlinkable |
|   6.3% | 379 | BasicHeap::list_push |
|   5.9% | 358 | BasicHeap::place |
|   5.8% | 351 | is_alloc |
|   5.1% | 309 | FreeLists::next_nonempty |
|   3.1% | 189 | BasicHeap::malloc |

### By source line

| Share | Samples | Where |
|---|---|---|
|   7.5% | 451 | heap.h:126 (SegregatedFit::find) |
|   6.8% | 411 | heap.cpp:104 (BasicHeap::find_fit) |
|   6.7% | 404 | heap.cpp:0 (BasicHeap::release) |
|   5.9% | 356 | bench_main.cpp:100 (replay) |
|   5.8% | 351 | block.h:34 (is_alloc) |
|   5.7% | 343 | bench_main.cpp:102 (replay) |
|   5.1% | 311 | heap.cpp:145 (BasicHeap::release) |
|   4.9% | 293 | heap.h:52 (FreeLists::next_nonempty) |
|   3.7% | 223 | heap.cpp:31 (BasicHeap::list_push) |
|   3.7% | 223 | heap.h:0 (SegregatedFit::find) |
