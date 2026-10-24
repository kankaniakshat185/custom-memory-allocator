# Environment

Every ADR links here. Update it whenever the machine, OS, toolchain, or flags change, and record the power/thermal state of each benchmark run in `results/`.

## Machine (measured 2026-10-24)

| Item | Value |
|---|---|
| CPU | Apple M2, arm64, 8 cores (performance + efficiency cores) |
| OS | macOS 26.5.2 |
| Page size | 16 KiB |
| Cache line | 128 B |
| Compiler | Apple clang 21.0.0 (clang-2100.1.1.101), from Xcode (since Phase 5). Phases 0–4 were built with Apple clang 17.0.0 (clang-1700.6.3.2) from the Command Line Tools. |
| CMake | 4.4.2, **x86_64 binary** at `/usr/local/bin/cmake` (runs under Rosetta; see below) |
| Developer tools | Xcode, `xcode-select -p` = `/Applications/Xcode.app/Contents/Developer`; `xctrace` 16.0 (17F113). Installed during Phase 5; before that, Command Line Tools only. |
| Baseline allocator | macOS libmalloc (not glibc) |

## Known toolchain issues

### Instruments — resolved
Unavailable during Phases 0–4 (Command Line Tools only). Resolved by installing Xcode: `xcrun xctrace version` reports 16.0 (17F113).

### AddressSanitizer hung at startup — resolved by the toolchain change
**Resolution (Phase 5):** with Xcode's Apple clang 21, an empty program under `-fsanitize=address` and `address,undefined` runs normally, and a deliberate heap overflow is reported (`heap-buffer-overflow ... WRITE of size 4`). The full suite (78 tests) passes under ASan + UBSan with no reports: `cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DMYALLOC_ASAN=ON -DMYALLOC_UBSAN=ON`. So the hang was specific to the Command Line Tools' clang 17 ASan runtime on this OS. Original diagnosis kept below for the record.

Reproduction: `int main(){return 0;}` compiled with `clang++ -fsanitize=address` hangs (killed by a 15 s alarm, exit 142).

| Flags | Result |
|---|---|
| `-fsanitize=address` | hangs |
| `-fsanitize=address,undefined` | hangs |
| `-fsanitize=undefined` | works (exit 0) |

`ASAN_OPTIONS=verbosity=2` shows the last lines before the hang:

```
AddressSanitizer: libc interceptors initialized
FindDynamicShadowStart, space_size = 0x0fffffffffff
```

So the hang is in ASan's shadow-memory placement, before any user code runs. `use_sigaltstack=0` and `detect_stack_use_after_return=0` did not help. Likely a runtime/OS-version incompatibility rather than anything in this project. Not yet tried: Homebrew LLVM (not installed), newer Xcode toolchain.

**Decision (Phases 0–4):** UBSan alone; `Heap::check()` as the primary correctness tool. **From Phase 5:** ASan + UBSan as well. `check()` stays primary for in-arena corruption, which ASan cannot see (ADR-004).

### CMake is an x86_64 binary, so builds silently targeted Rosetta — fixed
`/usr/local/bin/cmake` (4.4.2) is an Intel (`x86_64`) executable. It runs under Rosetta 2, detects an `x86_64` host, and passed `-arch x86_64` to every CMake build. So **every CMake-built binary in Phases 0–5 was x86_64 running under translation**: the tests, the footprint probe, and the first final timing run. Direct `clang++` builds (the phase script, the ASan smoke tests) were native arm64.

How it was found: the Phase 4 commit and the current tree have identical allocator code, but timed 1.3–1.8× apart (e.g. `random_16B_64KiB` 28.8 vs. 15.9 ns/op on the same quiet machine). `compile_commands.json` showed `-arch x86_64`.

Impact:
- **Correctness tests:** unaffected in meaning (x86_64 code is still valid code), and re-run natively on arm64: 78/78 under UBSan and 78/78 under ASan + UBSan.
- **Footprint probe:** deterministic, and re-run natively with byte-identical output, so all footprint numbers in ADR-005 to 007 stand.
- **Timing:** the first final run's `timing-current*.csv` was invalid (both this allocator *and* libmalloc ran translated) and has been discarded. All timing in `results/` is from native arm64 builds.

Fixes:
1. [CMakeLists.txt](../CMakeLists.txt) forces `CMAKE_OSX_ARCHITECTURES=arm64` when `sysctl hw.optional.arm64` is 1, which is truthful even under Rosetta, unless an architecture is set explicitly.
2. `bench_timing` refuses to run when `sysctl.proc_translated` is 1 (exit 3; override with `BENCH_ALLOW_ROSETTA=1`). Verified by building the harness with `-arch x86_64`.

Optional cleanup: install a native CMake (e.g. Homebrew under `/opt/homebrew`). Not required, given fix 1.

## Benchmark run protocol
- AC power, Low Power Mode off, other apps closed.
- Warm-up runs discarded from stats (but kept in raw CSV).
- ≥15 repetitions; report min/median/max.
- Timing by batch (timer granularity ~42 ns), see plan §4.
- No CPU pinning available on macOS.
