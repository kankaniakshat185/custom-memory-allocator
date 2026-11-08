Samples inside replay(): 4856 (1 ms each), trace `/var/folders/d4/kllzklmj2lsb1kkc1lvgvvp80000gn/T//prof/ff_app.trace`

### By category

| Share | Samples | Where |
|---|---|---|
|  32.4% | 1575 | free: merge (release) |
|  17.2% | 834 | harness (replay loop, touch) |
|  14.6% | 708 | unlink: safe-unlinking checks |
|  10.8% | 524 | search: walk + per-node check |
|   8.1% | 395 | list push |
|   6.2% | 300 | malloc body |
|   2.9% | 141 | free: pointer validation |
|   2.9% | 141 | free body |
|   2.3% | 110 | unlink: relink neighbours |
|   1.5% | 74 | stats / extent |
|   0.8% | 41 | malloc: split (place) |
|   0.2% | 8 | outside binary (libsystem_kernel.dylib) |
|   0.1% | 4 | bin index + bitmap (list push/unlink) |
|   0.0% | 1 | outside binary (libsystem_platform.dylib) |

### By innermost (inlined) function

| Share | Samples | Where |
|---|---|---|
|  31.3% | 1519 | BasicHeap::release |
|  15.5% | 755 | replay |
|   8.1% | 395 | BasicHeap::list_push |
|   7.4% | 357 | BasicHeap::unlinkable |
|   6.7% | 323 | FirstFit::find |
|   5.4% | 260 | is_alloc |
|   4.7% | 229 | BasicHeap::malloc |
|   3.2% | 157 | BasicHeap::plausible_free |
|   2.9% | 141 | BasicHeap::free |
|   2.6% | 124 | block_size |

### By source line

| Share | Samples | Where |
|---|---|---|
|  13.9% | 673 | heap.cpp:0 (BasicHeap::release) |
|   8.5% | 414 | heap.cpp:145 (BasicHeap::release) |
|   7.9% | 384 | bench_main.cpp:100 (replay) |
|   6.2% | 299 | bench_main.cpp:102 (replay) |
|   5.8% | 283 | heap.cpp:28 (BasicHeap::list_push) |
|   5.6% | 274 | heap.cpp:154 (BasicHeap::release) |
|   5.4% | 260 | block.h:34 (is_alloc) |
|   3.2% | 153 | heap.cpp:51 (BasicHeap::plausible_free) |
|   2.8% | 137 | heap.h:0 (FirstFit::find) |
|   2.6% | 124 | block.h:33 (block_size) |
