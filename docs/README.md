# Documentation index

| Path | Contents |
|---|---|
| [plan.md](plan.md) | Project plan: scope, design, phases, benchmark methodology, tooling |
| [testing.md](testing.md) | Test strategy and how to run |
| [environment.md](environment.md) | Machine/toolchain details and known tooling issues |
| [adr/](adr/) | Architecture decision records (template: [000-template.md](adr/000-template.md)) |
| [results/](results/) | Footprint-probe CSVs, timing CSVs + [timing-summary.md](results/timing-summary.md), machine state per run, later: profiler traces |

## ADR index

| ADR | Title | Status |
|---|---|---|
| [001](adr/001-block-tracking-structure.md) | Block tracking structure | Accepted |
| [002](adr/002-baseline-header-layout-and-free-list.md) | Baseline allocator: header layout, explicit free list | Accepted (footprint + timing measured) |
| [003](adr/003-arena-fixed-mmap-reservation.md) | Arena: one fixed mmap reservation | Accepted |
| [004](adr/004-verification-strategy.md) | Verification strategy: invariant checker, tests, footprint probe | Accepted |
| [005](adr/005-splitting.md) | Splitting | Accepted (Phase 2 untimeable) |
| [006](adr/006-coalescing.md) | Coalescing with boundary tags and prev-allocated bit | Accepted (footprint + timing measured) |
| [007](adr/007-realloc-and-calloc.md) | realloc (in place where possible) and calloc | Accepted provisionally (timing done; footprint revisit after 009) |
| [008](adr/008-benchmark-methodology.md) | Benchmark methodology: trace replay, batch timing, controlled libmalloc baseline | Accepted (native arm64 results) |
| 009 | First-fit vs. best-fit | Planned |
| 010 | Profiler findings | Planned |
| 011 | Segregated lists | Conditional on profiling |
