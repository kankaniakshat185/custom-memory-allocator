# Documentation index

| Path | Contents |
|---|---|
| [plan.md](plan.md) | Project plan: scope, design, phases, benchmark methodology, tooling |
| [testing.md](testing.md) | Test strategy and how to run |
| [environment.md](environment.md) | Machine/toolchain details and known tooling issues |
| [adr/](adr/) | Architecture decision records (template: [000-template.md](adr/000-template.md)) |
| [results/](results/) | Raw benchmark and footprint-probe CSVs, profiler traces/screenshots, `vmmap` output |

## ADR index

| ADR | Title | Status |
|---|---|---|
| [001](adr/001-block-tracking-structure.md) | Block tracking structure | Accepted |
| [002](adr/002-baseline-header-layout-and-free-list.md) | Baseline allocator: header layout, explicit free list | Accepted (timing pending) |
| [003](adr/003-arena-fixed-mmap-reservation.md) | Arena: one fixed mmap reservation | Accepted |
| [004](adr/004-verification-strategy.md) | Verification strategy: invariant checker, tests, footprint probe | Accepted |
| [005](adr/005-splitting.md) | Splitting | Accepted (timing pending) |
| [006](adr/006-coalescing.md) | Coalescing with boundary tags and prev-allocated bit | Accepted (timing pending) |
| 007 | Realloc strategy | Planned |
| 008 | First-fit vs. best-fit | Planned |
| 009 | Profiler findings | Planned |
| 010 | Segregated lists | Conditional on profiling |
