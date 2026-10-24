# Testing strategy

Run: `cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Debug -DMYALLOC_UBSAN=ON && cmake --build build-test -j && ctest --test-dir build-test --output-on-failure`

| Level | Files | What it covers |
|---|---|---|
| Unit | `test_arena.cpp`, `test_heap_unit.cpp`, `test_split.cpp`, `test_coalesce.cpp`, `test_realloc.cpp`, `test_alignment.cpp` | One behaviour per test on a fresh small `Heap`: alloc/free, reuse order, OOM, double/invalid free, split arithmetic and thresholds, merges in each direction, prev-allocated bit maintenance, every realloc path (shrink, grow in place, move, failure), calloc zeroing and overflow, alignment for every size 0..600, stats. |
| Property / stress | `test_stress.cpp` | Seeded random alloc/free mixes. `Heap::check()` after **every** operation, per-block fill patterns verified before free (catches overlap/corruption), tiny arena to force constant OOM. |
| Footprint probe (measurement) | `bench/footprint_probe.cpp` | Not a test: deterministic fragmentation/utilisation numbers per phase, committed to `results/` and quoted in ADRs. `footprint_probe realloc` compares realloc policies. |
| Integration | `test_api_integration.cpp` | The public `my_*` C API on the real 256 MiB heap: linked structures, strings, a 32 MiB allocation, a realloc string builder, a calloc matrix, `std::vector`/`std::map`/`basic_string` on `my_malloc` via a test-only allocator adapter, death tests for abort-on-bad-free/realloc. |

Rationale and the full invariant list: [ADR-004](adr/004-verification-strategy.md).

Per-phase additions: Phase 2 split tests (done, `test_split.cpp`); Phase 3 coalescing (done, `test_coalesce.cpp`, plus the stress tests now require one free block after freeing everything); Phase 4 realloc/calloc (done, `test_realloc.cpp`, realloc stress configurations, STL integration); Phase 6 the same suites run against both fit policies.

Sanitizers: UBSan is on in test builds. ASan + UBSan: `cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMYALLOC_ASAN=ON -DMYALLOC_UBSAN=ON` (needs Xcode's clang; see [environment.md](environment.md)). `check()` remains the primary guard against in-arena corruption, which ASan cannot see.
