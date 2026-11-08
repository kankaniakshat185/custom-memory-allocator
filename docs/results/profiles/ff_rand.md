Samples inside replay(): 4888 (1 ms each), trace `/var/folders/d4/kllzklmj2lsb1kkc1lvgvvp80000gn/T//prof/ff_rand.trace`

### By category

| Share | Samples | Where |
|---|---|---|
|  31.4% | 1533 | free: merge (release) |
|  18.1% | 883 | harness (replay loop, touch) |
|  17.5% | 854 | unlink: safe-unlinking checks |
|   8.7% | 427 | search: walk + per-node check |
|   7.4% | 362 | malloc body |
|   7.3% | 357 | list push |
|   3.3% | 160 | free body |
|   2.9% | 142 | free: pointer validation |
|   2.0% | 99 | unlink: relink neighbours |
|   1.2% | 58 | stats / extent |
|   0.1% | 5 | outside binary (libsystem_kernel.dylib) |
|   0.1% | 4 | bin index + bitmap (list push/unlink) |
|   0.1% | 3 | malloc: split (place) |
|   0.0% | 1 | outside binary (libsystem_c.dylib) |

### By innermost (inlined) function

| Share | Samples | Where |
|---|---|---|
|  29.9% | 1461 | BasicHeap::release |
|  16.4% | 801 | replay |
|   8.4% | 413 | BasicHeap::unlinkable |
|   7.3% | 357 | BasicHeap::list_push |
|   6.2% | 304 | BasicHeap::malloc |
|   6.1% | 299 | is_alloc |
|   5.9% | 288 | FirstFit::find |
|   3.6% | 175 | BasicHeap::plausible_free |
|   3.3% | 160 | BasicHeap::free |
|   2.9% | 143 | block_size |

### By source line

| Share | Samples | Where |
|---|---|---|
|  13.1% | 641 | heap.cpp:0 (BasicHeap::release) |
|   8.1% | 394 | bench_main.cpp:100 (replay) |
|   7.1% | 346 | bench_main.cpp:102 (replay) |
|   6.9% | 337 | heap.cpp:145 (BasicHeap::release) |
|   6.5% | 320 | heap.cpp:154 (BasicHeap::release) |
|   6.1% | 299 | block.h:34 (is_alloc) |
|   4.4% | 214 | heap.cpp:28 (BasicHeap::list_push) |
|   3.6% | 175 | heap.cpp:51 (BasicHeap::plausible_free) |
|   3.5% | 170 | heap.h:0 (FirstFit::find) |
|   2.9% | 143 | block.h:33 (block_size) |
