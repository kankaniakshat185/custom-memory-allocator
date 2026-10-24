Samples inside replay(): 4313 (1 ms each), trace `/var/folders/d4/kllzklmj2lsb1kkc1lvgvvp80000gn/T//prof/p6_app.trace`

### By category

| Share | Samples | Where |
|---|---|---|
|  23.7% | 1021 | free: merge (release) |
|  21.2% | 916 | harness (replay loop, touch) |
|  17.8% | 769 | unlink: relink neighbours |
|  13.6% | 585 | search: walk + per-node check |
|   6.1% | 261 | malloc: split (place) |
|   5.6% | 243 | malloc body |
|   3.9% | 169 | free body |
|   3.8% | 165 | list push |
|   3.7% | 159 | free: pointer validation |
|   0.4% | 18 | outside binary (libsystem_kernel.dylib) |
|   0.1% | 4 | stats / extent |
|   0.1% | 3 | outside binary (libsystem_malloc.dylib) |

### By innermost (inlined) function

| Share | Samples | Where |
|---|---|---|
|  19.6% | 845 | replay |
|  17.8% | 769 | BasicHeap::list_remove |
|  15.3% | 659 | BasicHeap::release |
|  10.9% | 470 | FirstFit::find |
|   7.0% | 304 | prev_block |
|   6.1% | 261 | BasicHeap::place |
|   4.6% | 197 | BasicHeap::malloc |
|   3.9% | 169 | BasicHeap::free |
|   3.8% | 165 | BasicHeap::list_push |
|   2.9% | 125 | BasicHeap::validate |
|   2.5% | 106 | block_size |
|   1.6% | 70 | Arena::contains |
|   1.5% | 63 | is_alloc |
|   1.0% | 42 | BasicHeap::find_fit |
|   0.8% | 36 | Arena::base |

### By source line

| Share | Samples | Where |
|---|---|---|
|  16.3% | 701 | heap.cpp:37 (BasicHeap::list_remove) |
|  10.3% | 443 | heap.cpp:84 (BasicHeap::release) |
|   9.9% | 427 | bench_main.cpp:96 (replay) |
|   7.9% | 340 | heap.h:35 (FirstFit::find) |
|   7.8% | 337 | bench_main.cpp:98 (replay) |
|   7.0% | 304 | block.h:53 (prev_block) |
|   5.0% | 216 | heap.cpp:59 (BasicHeap::place) |
|   4.5% | 193 | heap.cpp:89 (BasicHeap::release) |
|   3.0% | 129 | heap.h:36 (FirstFit::find) |
|   3.0% | 129 | heap.cpp:27 (BasicHeap::list_push) |
|   2.5% | 106 | block.h:33 (block_size) |
|   2.0% | 88 | ? (BasicHeap::malloc) |
|   1.8% | 79 | ? (BasicHeap::free) |
|   1.6% | 70 | heap.cpp:114 (BasicHeap::malloc) |
|   1.5% | 64 | heap.cpp:40 (BasicHeap::list_remove) |
