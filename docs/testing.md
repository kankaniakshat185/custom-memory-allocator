# Testing strategy

Run: `cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Debug -DMYALLOC_UBSAN=ON && cmake --build build-test -j && ctest --test-dir build-test --output-on-failure`

| Level | Files | What it covers |
|---|---|---|
| Unit | `test_arena.cpp`, `test_heap_unit.cpp`, `test_split.cpp`, `test_alignment.cpp` | One behaviour per test on a fresh small `Heap`: alloc/free, reuse order, OOM, double/invalid free, split arithmetic and thresholds, alignment for every size 0..600, stats. |
| Property / stress | `test_stress.cpp` | Seeded random alloc/free mixes. `Heap::check()` after **every** operation, per-block fill patterns verified before free (catches overlap/corruption), tiny arena to force constant OOM. |
| Footprint probe (measurement) | `bench/footprint_probe.cpp` | Not a test: deterministic fragmentation/utilisation numbers per phase, committed to `results/` and quoted in ADRs. |
| Integration | `test_api_integration.cpp` | The public `my_*` C API on the real 256 MiB heap: linked structures, strings, a 32 MiB allocation, death tests for abort-on-bad-free. |

Rationale and the full invariant list: [ADR-004](adr/004-verification-strategy.md).

Per-phase additions: Phase 2 split tests (done, `test_split.cpp`); Phase 3 coalescing (adjacent frees merge, both directions); Phase 4 realloc/calloc; Phase 6 the same suites run against both fit policies. Phase 4 also adds an integration test using the allocator behind a `std::vector` allocator adapter.

Sanitizers: UBSan is on in test builds. ASan is currently unusable on this machine (see [environment.md](environment.md)); `check()` is the primary guard against in-arena corruption.
