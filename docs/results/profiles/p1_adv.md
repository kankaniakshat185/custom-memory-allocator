Samples inside replay(): 5973 (1 ms each), trace `/var/folders/d4/kllzklmj2lsb1kkc1lvgvvp80000gn/T//prof/p1_adv.trace`

### By category

| Share | Samples | Where |
|---|---|---|
|  98.4% | 5880 | search: walk + per-node check |
|   1.3% | 76 | unlink: relink neighbours |
|   0.1% | 5 | harness (replay loop, touch) |
|   0.1% | 5 | free body |
|   0.1% | 4 | malloc body |
|   0.0% | 2 | outside binary (libsystem_kernel.dylib) |
|   0.0% | 1 | list push |

### By innermost (inlined) function

| Share | Samples | Where |
|---|---|---|
|  87.6% | 5235 | Heap::find_first_fit |
|  10.8% | 645 | block_size |
|   1.3% | 76 | Heap::list_remove |
|   0.1% | 5 | Heap::free |
|   0.1% | 3 | Heap::malloc |
|   0.1% | 3 | replay |
|   0.0% | 2 | libsystem_kernel.dylib: mach_absolute_time |
|   0.0% | 2 | Arena::contains |
|   0.0% | 1 | Heap::list_push |
|   0.0% | 1 | Arena::base |

### By source line

| Share | Samples | Where |
|---|---|---|
|  86.7% | 5178 | heap.cpp:31 (Heap::find_first_fit) |
|  10.8% | 645 | block.h:33 (block_size) |
|   1.3% | 76 | heap.cpp:24 (Heap::list_remove) |
|   1.0% | 57 | heap.cpp:32 (Heap::find_first_fit) |
|   0.1% | 3 | heap.cpp:61 (Heap::free) |
|   0.0% | 2 | heap.cpp:53 (Heap::malloc) |
|   0.0% | 2 | libsystem_kernel.dylib: mach_absolute_time |
|   0.0% | 2 | arena.cpp:39 (Arena::contains) |
|   0.0% | 2 | bench_main.cpp:101 (replay) |
|   0.0% | 1 | bench_main.cpp:96 (replay) |
|   0.0% | 1 | heap.cpp:19 (Heap::list_push) |
|   0.0% | 1 | heap.cpp:51 (Heap::malloc) |
|   0.0% | 1 | heap.cpp:60 (Heap::free) |
|   0.0% | 1 | arena.h:19 (Arena::base) |
|   0.0% | 1 | heap.cpp:67 (Heap::free) |
