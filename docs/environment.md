# Environment

Every ADR links here. Update it whenever the machine, OS, toolchain, or flags change, and record the power/thermal state of each benchmark run in `results/`.

## Machine (measured 2026-10-24)

| Item | Value |
|---|---|
| CPU | Apple M2, arm64, 8 cores (performance + efficiency cores) |
| OS | macOS 26.5.2 |
| Page size | 16 KiB |
| Cache line | 128 B |
| Compiler | Apple clang 17.0.0 (clang-1700.6.3.2), `/usr/bin/clang++` |
| CMake | 4.4.2 |
| Developer tools | Command Line Tools only (`xcode-select -p` = `/Library/Developer/CommandLineTools`); full Xcode not installed |
| Baseline allocator | macOS libmalloc (not glibc) |

## Known toolchain issues

### Instruments unavailable
`xcrun --find xctrace` fails; no `Xcode.app` in `/Applications`. Instruments (Time Profiler, CPU Counters, Allocations, os_signpost) needs full Xcode. **Open item:** install Xcode and verify. Fallback is `sample`/`spindump` (see plan §5).

### AddressSanitizer hangs at startup
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

**Decision:** UBSan alone is the sanitizer for now. Primary correctness tooling is `heap_check()` (plan §5). Revisit if Xcode gets installed.

## Benchmark run protocol
- AC power, Low Power Mode off, other apps closed.
- Warm-up runs discarded from stats (but kept in raw CSV).
- ≥15 repetitions; report min/median/max.
- Timing by batch (timer granularity ~42 ns), see plan §4.
- No CPU pinning available on macOS.
