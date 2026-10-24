# Documentation index

| Path | Contents |
|---|---|
| [plan.md](plan.md) | Project plan: scope, design, phases, benchmark methodology, tooling |
| [testing.md](testing.md) | Test strategy and how to run |
| [environment.md](environment.md) | Machine/toolchain details and known tooling issues |
| [adr/](adr/) | Architecture decision records (template: [000-template.md](adr/000-template.md)) |
| [results/](results/) | Footprint-probe CSVs, timing CSVs + summaries, machine state per run, [profiles/](results/profiles/) (attributed Time Profiler + CPU Counters results) |

## ADR index

| ADR | Title | Status |
|---|---|---|
| [001](adr/001-block-tracking-structure.md) | Block tracking structure | Accepted |
| [002](adr/002-baseline-header-layout-and-free-list.md) | Baseline allocator: header layout, explicit free list | Accepted (footprint + timing measured) |
| [003](adr/003-arena-fixed-mmap-reservation.md) | Arena: one fixed mmap reservation | Accepted |
| [004](adr/004-verification-strategy.md) | Verification strategy: invariant checker, tests, footprint probe | Accepted |
| [005](adr/005-splitting.md) | Splitting | Accepted (Phase 2 untimeable) |
| [006](adr/006-coalescing.md) | Coalescing with boundary tags and prev-allocated bit | Accepted (footprint + timing measured) |
| [007](adr/007-realloc-and-calloc.md) | realloc (in place where possible) and calloc | Accepted (resolved by 009) |
| [008](adr/008-benchmark-methodology.md) | Benchmark methodology: trace replay, batch timing, controlled libmalloc baseline | Accepted (native arm64 results) |
| [009](adr/009-first-fit-vs-best-fit.md) | Fit policy: first-fit vs. best-fit, compile-time | Accepted (first-fit default) |
| [010](adr/010-profiling-findings.md) | Profiling findings: where the time goes and why (Time Profiler + CPU Counters) | Accepted |
| 011 | Segregated size-class free lists | Motivated by 009 + 010 (memory and worst case, not average speed); scope decision pending (stretch) |
| [012](adr/012-use-after-free-hardening.md) | Use-after-free hardening: validated links, safe unlinking, fail-stop | Accepted |
