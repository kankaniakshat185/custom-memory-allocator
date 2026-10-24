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
| CMake | 4.4.2 |
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

## Benchmark run protocol
- AC power, Low Power Mode off, other apps closed.
- Warm-up runs discarded from stats (but kept in raw CSV).
- ≥15 repetitions; report min/median/max.
- Timing by batch (timer granularity ~42 ns), see plan §4.
- No CPU pinning available on macOS.
