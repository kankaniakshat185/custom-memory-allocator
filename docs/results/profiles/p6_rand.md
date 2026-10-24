Samples inside replay(): 4071 (1 ms each), trace `/var/folders/d4/kllzklmj2lsb1kkc1lvgvvp80000gn/T//prof/p6_rand.trace`

### By category

| Share | Samples | Where |
|---|---|---|
|  26.3% | 1070 | free: merge (release) |
|  21.8% | 889 | harness (replay loop, touch) |
|  17.9% | 728 | unlink: relink neighbours |
|  13.7% | 558 | search: walk + per-node check |
|   5.9% | 240 | malloc body |
|   4.3% | 174 | list push |
|   3.8% | 153 | free body |
|   3.7% | 150 | free: pointer validation |
|   2.1% | 86 | malloc: split (place) |
|   0.3% | 13 | outside binary (libsystem_kernel.dylib) |
|   0.1% | 6 | stats / extent |
|   0.1% | 3 | outside binary (libsystem_c.dylib) |
|   0.0% | 1 | outside binary (libsystem_malloc.dylib) |

### By innermost (inlined) function

| Share | Samples | Where |
|---|---|---|
|  19.6% | 796 | replay |
|  18.8% | 767 | BasicHeap::release |
|  17.9% | 728 | BasicHeap::list_remove |
|  11.0% | 447 | FirstFit::find |
|   6.2% | 252 | prev_block |
|   4.9% | 198 | BasicHeap::malloc |
|   4.3% | 174 | BasicHeap::list_push |
|   3.8% | 153 | BasicHeap::free |
|   2.7% | 110 | BasicHeap::validate |
|   2.3% | 93 | block_size |
|   2.2% | 89 | Arena::contains |
|   2.1% | 86 | BasicHeap::place |
|   1.6% | 65 | is_alloc |
|   1.2% | 48 | BasicHeap::find_fit |
|   0.9% | 35 | Arena::base |

### By source line

| Share | Samples | Where |
|---|---|---|
|  16.0% | 651 | heap.cpp:37 (BasicHeap::list_remove) |
|  13.3% | 542 | heap.cpp:84 (BasicHeap::release) |
|   9.7% | 396 | bench_main.cpp:96 (replay) |
|   8.1% | 329 | bench_main.cpp:98 (replay) |
|   6.5% | 264 | heap.h:35 (FirstFit::find) |
|   6.2% | 252 | block.h:53 (prev_block) |
|   5.0% | 203 | heap.cpp:89 (BasicHeap::release) |
|   4.4% | 180 | heap.h:36 (FirstFit::find) |
|   3.5% | 144 | heap.cpp:27 (BasicHeap::list_push) |
|   2.3% | 93 | block.h:33 (block_size) |
|   2.1% | 84 | heap.cpp:59 (BasicHeap::place) |
|   1.9% | 79 | ? (BasicHeap::malloc) |
|   1.8% | 75 | heap.cpp:114 (BasicHeap::malloc) |
|   1.7% | 69 | heap.cpp:40 (BasicHeap::list_remove) |
|   1.6% | 65 | block.h:34 (is_alloc) |
