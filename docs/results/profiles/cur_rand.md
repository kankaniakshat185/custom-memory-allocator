Samples inside replay(): 5224 (1 ms each), trace `/var/folders/d4/kllzklmj2lsb1kkc1lvgvvp80000gn/T//prof/cur_rand.trace`

### By category

| Share | Samples | Where |
|---|---|---|
|  31.5% | 1647 | free: merge (release) |
|  20.1% | 1049 | harness (replay loop, touch) |
|  17.2% | 897 | unlink: safe-unlinking checks |
|   8.9% | 463 | search: walk + per-node check |
|   7.0% | 366 | malloc body |
|   3.5% | 181 | free body |
|   3.1% | 160 | free: pointer validation |
|   2.8% | 144 | unlink: relink neighbours |
|   2.1% | 108 | stats / extent |
|   2.0% | 107 | list push |
|   1.7% | 91 | malloc: split (place) |
|   0.2% | 10 | outside binary (libsystem_kernel.dylib) |
|   0.0% | 1 | outside binary (libsystem_malloc.dylib) |

### By innermost (inlined) function

| Share | Samples | Where |
|---|---|---|
|  29.9% | 1564 | BasicHeap::release |
|  17.9% | 936 | replay |
|   9.0% | 471 | BasicHeap::unlinkable |
|   5.6% | 290 | BasicHeap::malloc |
|   5.3% | 278 | is_alloc |
|   4.6% | 238 | FirstFit::find |
|   3.7% | 193 | BasicHeap::plausible_free |
|   3.5% | 181 | BasicHeap::free |
|   3.2% | 168 | BasicHeap::find_fit |
|   2.9% | 153 | block_size |
|   2.4% | 124 | BasicHeap::validate |
|   2.2% | 113 | Arena::base |
|   2.2% | 113 | BasicHeap::unlink |
|   2.1% | 111 | Arena::contains |
|   2.0% | 107 | BasicHeap::list_push |

### By source line

| Share | Samples | Where |
|---|---|---|
|  13.5% | 704 | heap.cpp:0 (BasicHeap::release) |
|   9.0% | 472 | bench_main.cpp:96 (replay) |
|   7.6% | 395 | bench_main.cpp:98 (replay) |
|   7.0% | 364 | heap.cpp:133 (BasicHeap::release) |
|   6.4% | 332 | heap.cpp:141 (BasicHeap::release) |
|   5.3% | 278 | block.h:34 (is_alloc) |
|   3.7% | 193 | heap.cpp:49 (BasicHeap::plausible_free) |
|   3.4% | 180 | heap.h:0 (FirstFit::find) |
|   3.0% | 156 | heap.cpp:61 (BasicHeap::unlinkable) |
|   2.9% | 153 | block.h:33 (block_size) |
|   2.6% | 135 | heap.cpp:0 (BasicHeap::unlinkable) |
|   2.2% | 114 | heap.cpp:0 (BasicHeap::malloc) |
|   2.2% | 113 | arena.h:19 (Arena::base) |
|   2.0% | 104 | heap.cpp:27 (BasicHeap::list_push) |
|   2.0% | 103 | ? (BasicHeap::unlinkable) |
