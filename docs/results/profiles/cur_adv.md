Samples inside replay(): 5979 (1 ms each), trace `/var/folders/d4/kllzklmj2lsb1kkc1lvgvvp80000gn/T//prof/cur_adv.trace`

### By category

| Share | Samples | Where |
|---|---|---|
|  97.7% | 5844 | search: walk + per-node check |
|   0.7% | 39 | malloc body |
|   0.5% | 30 | unlink: safe-unlinking checks |
|   0.3% | 19 | harness (replay loop, touch) |
|   0.3% | 19 | free: merge (release) |
|   0.1% | 8 | free body |
|   0.1% | 8 | free: pointer validation |
|   0.1% | 5 | stats / extent |
|   0.1% | 3 | list push |
|   0.0% | 2 | outside binary (libsystem_kernel.dylib) |
|   0.0% | 1 | malloc: split (place) |
|   0.0% | 1 | unlink: relink neighbours |

### By innermost (inlined) function

| Share | Samples | Where |
|---|---|---|
|  60.7% | 3630 | FirstFit::find |
|  20.8% | 1241 | BasicHeap::find_fit |
|  16.3% | 972 | is_alloc |
|   0.6% | 36 | BasicHeap::malloc |
|   0.3% | 17 | BasicHeap::release |
|   0.2% | 13 | BasicHeap::unlinkable |
|   0.2% | 12 | replay |
|   0.2% | 11 | BasicHeap::in_heap |
|   0.1% | 8 | BasicHeap::free |
|   0.1% | 8 | block_size |
|   0.1% | 7 | BasicHeap::validate |
|   0.1% | 6 | Arena::contains |
|   0.1% | 4 | Arena::base |
|   0.1% | 3 | BasicHeap::plausible_free |
|   0.1% | 3 | BasicHeap::list_push |

### By source line

| Share | Samples | Where |
|---|---|---|
|  28.3% | 1695 | heap.h:45 (FirstFit::find) |
|  25.3% | 1511 | heap.h:47 (FirstFit::find) |
|  20.7% | 1238 | heap.cpp:93 (BasicHeap::find_fit) |
|  16.3% | 972 | block.h:34 (is_alloc) |
|   7.0% | 416 | heap.h:48 (FirstFit::find) |
|   0.5% | 32 | heap.cpp:0 (BasicHeap::malloc) |
|   0.2% | 10 | heap.cpp:40 (BasicHeap::in_heap) |
|   0.2% | 9 | heap.cpp:158 (BasicHeap::release) |
|   0.1% | 8 | heap.h:0 (FirstFit::find) |
|   0.1% | 8 | block.h:33 (block_size) |
|   0.1% | 6 | heap.cpp:0 (BasicHeap::unlinkable) |
|   0.1% | 5 | bench_main.cpp:96 (replay) |
|   0.1% | 5 | heap.cpp:202 (BasicHeap::free) |
|   0.1% | 4 | arena.cpp:38 (Arena::contains) |
|   0.1% | 4 | arena.h:19 (Arena::base) |
