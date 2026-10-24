Samples inside replay(): 5482 (1 ms each), trace `/var/folders/d4/kllzklmj2lsb1kkc1lvgvvp80000gn/T//prof/cur_app.trace`

### By category

| Share | Samples | Where |
|---|---|---|
|  32.7% | 1794 | free: merge (release) |
|  19.0% | 1042 | harness (replay loop, touch) |
|  16.0% | 877 | unlink: safe-unlinking checks |
|   9.7% | 530 | search: walk + per-node check |
|   6.0% | 331 | malloc body |
|   4.6% | 252 | malloc: split (place) |
|   3.3% | 180 | free body |
|   2.7% | 149 | free: pointer validation |
|   2.3% | 128 | unlink: relink neighbours |
|   1.9% | 106 | stats / extent |
|   1.4% | 77 | list push |
|   0.3% | 15 | outside binary (libsystem_kernel.dylib) |
|   0.0% | 1 | outside binary (libsystem_malloc.dylib) |

### By innermost (inlined) function

| Share | Samples | Where |
|---|---|---|
|  31.2% | 1708 | BasicHeap::release |
|  17.1% | 936 | replay |
|   7.8% | 426 | BasicHeap::unlinkable |
|   5.1% | 278 | FirstFit::find |
|   4.7% | 256 | BasicHeap::malloc |
|   4.6% | 253 | is_alloc |
|   4.6% | 252 | BasicHeap::place |
|   3.9% | 213 | BasicHeap::plausible_free |
|   3.4% | 189 | block_size |
|   3.3% | 180 | BasicHeap::free |
|   3.2% | 178 | BasicHeap::find_fit |
|   2.2% | 122 | BasicHeap::validate |
|   1.9% | 104 | Arena::base |
|   1.9% | 102 | Arena::contains |
|   1.6% | 88 | BasicHeap::unlink |

### By source line

| Share | Samples | Where |
|---|---|---|
|  14.0% | 767 | heap.cpp:0 (BasicHeap::release) |
|   9.1% | 499 | bench_main.cpp:96 (replay) |
|   8.7% | 478 | heap.cpp:133 (BasicHeap::release) |
|   6.6% | 363 | bench_main.cpp:98 (replay) |
|   5.6% | 305 | heap.cpp:141 (BasicHeap::release) |
|   4.6% | 253 | block.h:34 (is_alloc) |
|   3.9% | 216 | heap.cpp:109 (BasicHeap::place) |
|   3.9% | 213 | heap.cpp:49 (BasicHeap::plausible_free) |
|   3.4% | 189 | block.h:33 (block_size) |
|   2.5% | 137 | heap.cpp:61 (BasicHeap::unlinkable) |
|   2.4% | 132 | heap.h:0 (FirstFit::find) |
|   2.0% | 110 | heap.cpp:93 (BasicHeap::find_fit) |
|   2.0% | 108 | heap.cpp:0 (BasicHeap::unlinkable) |
|   1.9% | 104 | arena.h:19 (Arena::base) |
|   1.8% | 99 | ? (BasicHeap::unlinkable) |
