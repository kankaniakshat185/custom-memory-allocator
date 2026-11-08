Samples inside replay(): 3765 (1 ms each), trace `/var/folders/d4/kllzklmj2lsb1kkc1lvgvvp80000gn/T//prof/seg_app.trace`

### By category

| Share | Samples | Where |
|---|---|---|
|  23.5% | 885 | harness (replay loop, touch) |
|  15.3% | 575 | search: walk + per-node check |
|  13.2% | 496 | unlink: safe-unlinking checks |
|  12.1% | 456 | list push |
|   9.5% | 359 | free: merge (release) |
|   7.3% | 274 | malloc body |
|   5.6% | 212 | malloc: split (place) |
|   3.9% | 148 | free body |
|   3.7% | 138 | free: pointer validation |
|   1.9% | 72 | bin index + bitmap (list push/unlink) |
|   1.9% | 71 | unlink: relink neighbours |
|   1.6% | 60 | stats / extent |
|   0.5% | 17 | outside binary (libsystem_kernel.dylib) |
|   0.0% | 1 | outside binary (libsystem_c.dylib) |
|   0.0% | 1 | outside binary (libsystem_malloc.dylib) |

### By innermost (inlined) function

| Share | Samples | Where |
|---|---|---|
|  20.8% | 782 | replay |
|  11.6% | 436 | BasicHeap::list_push |
|   9.4% | 354 | BasicHeap::unlinkable |
|   8.8% | 332 | BasicHeap::release |
|   6.2% | 234 | SegregatedFit::find |
|   5.6% | 212 | BasicHeap::place |
|   5.4% | 205 | BasicHeap::malloc |
|   4.8% | 179 | FreeLists::next_nonempty |
|   3.9% | 148 | BasicHeap::free |
|   3.8% | 143 | is_alloc |

### By source line

| Share | Samples | Where |
|---|---|---|
|   9.3% | 350 | bench_main.cpp:100 (replay) |
|   8.8% | 333 | bench_main.cpp:102 (replay) |
|   8.2% | 309 | heap.cpp:31 (BasicHeap::list_push) |
|   5.9% | 221 | heap.cpp:0 (BasicHeap::unlinkable) |
|   4.7% | 176 | heap.h:52 (FreeLists::next_nonempty) |
|   4.1% | 154 | heap.h:126 (SegregatedFit::find) |
|   4.0% | 152 | heap.cpp:121 (BasicHeap::place) |
|   3.8% | 143 | block.h:34 (is_alloc) |
|   2.9% | 110 | heap.cpp:0 (BasicHeap::release) |
|   2.6% | 98 | heap.cpp:29 (BasicHeap::list_push) |
