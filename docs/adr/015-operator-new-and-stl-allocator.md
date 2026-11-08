# ADR-015: `myalloc::allocator<T>` and an opt-in global `operator new`/`delete` replacement

- Status: Accepted. The heap-backed replacement ships as `myalloc_new`; a heap + slab variant is benchmark-only, pending a pre-registered timing check in Phase 17.
- Date: 2026-11-08
- Environment: see [../environment.md](../environment.md); native arm64, `-O2` Release
- Raw data: [results/stl/](../results/stl/) (see its README)
- Code: [include/myalloc/allocator.hpp](../../include/myalloc/allocator.hpp), [src/new_delete.cpp](../../src/new_delete.cpp), `my_aligned_alloc` and the immortal default heap in [src/myalloc.cpp](../../src/myalloc.cpp), [bench/new_delete_slab.cpp](../../bench/new_delete_slab.cpp), [examples/stl_demo.cpp](../../examples/stl_demo.cpp)

## Context
Before this phase, C++ code reached the allocator only by calling `my_malloc` directly or through a test-only allocator adapter in `test_api_integration.cpp`. The README listed "no `operator new` hook" as a limitation. Tier 2 of the plan asks for two things:

1. A **public STL allocator**, so chosen containers can use the heap without affecting the rest of the program.
2. An **opt-in global replacement** of `operator new`/`delete`, so an unmodified C++ program runs entirely on the heap.

C++17 has 20 replaceable forms: 8 `operator new` (plain and array, each also `nothrow`, `std::align_val_t`, and both) and 12 `operator delete` (the same plus sized and sized-aligned). Over-aligned forms need memory aligned to more than the heap's 16 B, which Phase 14's `BasicHeap::aligned_malloc` provides but the C API did not expose.

## Decision
1. **`my_aligned_alloc(alignment, n)`** joins the C API, over `aligned_malloc`. It returns NULL for a non-power-of-two alignment or exhaustion, and its blocks are freed with `my_free`. `MY_MALLOC_ALIGNMENT` (16) is now a public macro.
2. **`myalloc::allocator<T>`** ([allocator.hpp](../../include/myalloc/allocator.hpp)) is the test adapter promoted to a public header.
   - It is stateless, with `is_always_equal` and `propagate_on_container_move_assignment` both true.
   - Types with `alignof(T)` > 16 go through `my_aligned_alloc`.
   - A size overflow throws `std::bad_array_new_length`; exhaustion throws `std::bad_alloc`.
3. **`myalloc_new`** replaces all 20 forms ([new_delete.cpp](../../src/new_delete.cpp)) over the C API.
   - **Linking it is the only opt-in.** It is a CMake `OBJECT` library on purpose: from a static archive, the linker would only pull in a member that resolves a symbol nothing else defines, so whether a form was replaced would depend on link order.
   - **Allocation follows [new.delete].** On failure the throwing forms call the installed `new_handler` and retry, and throw `std::bad_alloc` if there is none. The nothrow forms behave as if they called the throwing form and caught the exception, so they run the `new_handler` too. `new(0)` returns a unique pointer, as `my_malloc(0)` does.
   - **Delete validates, and ignores the size.** Every delete form calls `my_free`, so a double delete or a foreign pointer aborts with the C API's diagnostic. The sized forms ignore the size, which the block header already records.
4. **The default heap is immortal.** It is constructed into static storage and never destroyed. With a global replacement, a static destructor can run after a function-local static `Heap` has been destroyed and still call `operator delete`. The OS reclaims the arena at exit. The at-exit leak report (ADR-013) is unaffected.
5. **The replacement uses the plain heap, not the slab, for now.** The slab measured better on every container workload (below), but switching needs evidence and work this phase doesn't have:
   - **Timing:** the machine was too loaded to time.
   - **Re-entrancy:** the slab's `init` allocates a `std::vector`, which re-enters `operator new`.
   - **Tracking:** a separate allocator would sit outside `my_alloc_stats` and the leak report.

   The variant is kept as `stl_bench_slab`, with a rule for Phase 17 (Validation).

## Alternatives considered
1. **Symbol interposition of `malloc`/`free`** (`DYLD_INSERT_LIBRARIES`, `LD_PRELOAD`). This would also capture C code and system libraries. Rejected:
   - the heap has one fixed 256 MiB arena and no thread safety (Phase 16), and system frameworks allocate from other threads;
   - macOS also needs `malloc_size`, `malloc_zone_*` and more. That is a different project (plan §1 non-goals).
2. **A static archive for `myalloc_new`.** Rejected: link-order-dependent replacement, as explained in Decision 3.
3. **Backing the replacement with the slab now** (`myalloc::Allocator`). It is the best on instruction counts. Deferred, for the reasons in Decision 5, with a pre-registered rule.
4. **Checking the size passed to sized delete against the block size.** It could catch some mismatched deletes. Rejected for now: the heap stores block sizes, not requested sizes, so the check could only catch a size larger than the block, and `my_free` already validates the pointer itself.
5. **A stateful allocator holding a heap pointer.** It would let containers use different heaps, but it would break `is_always_equal` and make every container larger by one pointer. Rejected: C++ code that wants its own heap can use `BasicHeap` directly.
6. **Leaving the default heap as a function-local static.** Simpler, but destruction-order dependent once `operator new` is replaced. Rejected (Decision 4).

## Consequences

### Instructions per rep on standard-container workloads (millions, median of 3; [results/stl/](../results/stl/))
| Workload | libmalloc | myalloc (heap) | heap + slab | heap vs libmalloc | slab vs heap |
|---|---|---|---|---|---|
| `map_churn` (random-key `std::map`) | 480.2 | 391.8 | **346.3** | −18.4% | −11.6% |
| `string_vector` (0–256 char strings) | 198.4 | 204.5 | **155.2** | +3.1% | −24.1% |
| `hash_count` (`unordered_map<string,int>`) | 303.5 | 358.2 | **288.1** | **+18.0%** | −19.6% |
| `list_queue` (`std::list` FIFO) | 429.1 | 495.0 | **362.1** | **+15.4%** | −26.8% |

- **The plain heap is behind libmalloc where small nodes dominate.** On `hash_count` and `list_queue` it retires 15–18% more instructions; on `map_churn` it retires 18% fewer.
- **The slab removes that gap.** It is below libmalloc on all four workloads (−5% to −28%) and below the heap by 12–27%.
  - **The obstacle from ADR-014 doesn't apply here.** ADR-014's only failure was the *realloc* growth workload, and `operator new` has no realloc path.
- **The mechanism is not verified.** One hypothesis is the split/merge churn ADR-011 measured for first-fit: a FIFO of small nodes frees blocks next to each other, which merge, and later small requests split them again. A Phase 17 profile can check it.
- **Peak RSS is roughly equal.** Heap and slab are within 0.5 MB of each other on three workloads; on `hash_count` the heap is 1.4 MB (16%) higher. libmalloc is lower than the heap on `hash_count` and `list_queue` and higher on `map_churn` and `string_vector`. libmalloc's own RSS varied between runs (6.7 vs 11.4 MB on `map_churn`), so these are rough.
- **Instructions are not time.** ADR-010 found the free path memory-bound. Instructions were the only metric that stayed repeatable: three runs agreed to ~0.1%, while wall-clock IQRs reached 115% with `BTLEServer` stuck at 100% CPU.

### Other consequences
- **The demo** ([examples/stl_demo.cpp](../../examples/stl_demo.cpp)) is an ordinary C++ program (`unordered_map`, `map`, `vector`, `string`, `shared_ptr`) that runs on the heap only because it links `myalloc_new`. It prints `my_alloc_stats` before and after: about 3,000 live blocks while the containers exist, back to 0 and one free block afterwards. `MYALLOC_LEAK_REPORT=1 stl_demo leak` lists the two blocks of a deliberately leaked `std::string`. The global it is stored in needs external linkage: with internal linkage, clang elided the unobserved `new` entirely, which [expr.new] allows.
- **Not thread-safe**, like the C API. The replacement is for single-threaded programs until Phase 16; this is documented in the header and the source.
- **GoogleTest runs on the heap.** In `myalloc_new_tests`, every allocation in the process goes through the heap, including the test framework's own. It works as an extra integration test.
- **`my_alloc_stats` counts all `operator new` traffic** when the replacement is linked, because both share one heap. That is the reason to keep the replacement on the C API rather than on a separate allocator.

## Validation
- **New tests.**
  - [test_stl_allocator.cpp](../../tests/test_stl_allocator.cpp), 8 tests in the main suite, so they run under all three policies:
    - the allocator requirements, checked through `allocator_traits` with `static_assert`;
    - over-aligned types (64 B and 4 KiB), including after a regrow;
    - `bad_array_new_length` and `bad_alloc`, with the heap still usable after exhaustion;
    - `list`/`deque`/`set`/`unordered_map` returning every block;
    - `allocate_shared`;
    - `my_aligned_alloc` for alignments 1 to 64 KiB, with bad alignments rejected.
  - [test_new_delete.cpp](../../tests/test_new_delete.cpp), 13 tests in its own binary `myalloc_new_tests` (ctest prefix `newdelete.`):
    - every replaced form, with pointers checked to lie in the heap's arena;
    - `new(0)` uniqueness and delete of null;
    - `bad_alloc` and `nullptr` on exhaustion;
    - the `new_handler` retry for both throwing and nothrow forms;
    - containers, `make_shared` and `std::function`;
    - exceptions;
    - death tests for double delete and deleting a foreign pointer.
  - The test-only adapter in `test_api_integration.cpp` is gone; those tests now use the public header.
- **Totals:** 136 tests × 3 policies + 13 = **421 runs**, all passing under UBSan and under ASan + UBSan, including the replacement binary under ASan. The footprint probe is unchanged: 6/6 reference outputs match, because the C API's placement is untouched.
- **Mutation checks** (2026-11-08, sources restored), each caught:
  - aligned `new` ignoring the alignment fails `OverAlignedNewIsAligned`;
  - throwing without calling the `new_handler` fails both handler tests;
  - the nothrow form skipping the handler fails `NothrowFormRunsTheNewHandlerToo`;
  - the allocator ignoring `alignof(T)` fails `OverAlignedTypesAreAligned`.
- **Phase 17 rule** (fixed before any clean timing exists). Switch `myalloc_new` to heap + slab if, on a quiet machine, `bench/scripts/run_stl.sh` shows the slab variant all of the following:
  - **≥ 10% lower median ns/op** than the heap-backed replacement on at least 3 of the 4 workloads;
  - **no workload more than 5% slower**;
  - **peak RSS no more than 10% higher** on any workload.

  Switching also requires removing the slab's `std::vector` metadata (so `init` cannot re-enter `operator new`) and bringing the slab into `my_alloc_stats` and the leak report. Otherwise the replacement stays on the heap and this ADR records why.
