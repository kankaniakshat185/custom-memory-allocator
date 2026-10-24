# Documentation index

| Path | Contents |
|---|---|
| [plan.md](plan.md) | Project plan: scope, design, phases, benchmark methodology, tooling |
| [testing.md](testing.md) | Test strategy and how to run |
| [environment.md](environment.md) | Machine/toolchain details and known tooling issues |
| [adr/](adr/) | Architecture decision records (template: [000-template.md](adr/000-template.md)) |
| [results/](results/) | Raw benchmark CSVs, profiler traces/screenshots, `vmmap` output |

## ADR index

| ADR | Title | Status |
|---|---|---|
| [001](adr/001-block-tracking-structure.md) | Block tracking structure | Proposed |
| [002](adr/002-baseline-header-layout-and-free-list.md) | Baseline allocator: header layout, explicit free list | Accepted (numbers pending) |
| 003 | Splitting | Planned |
| 004 | Coalescing | Planned |
| 005 | Realloc strategy | Planned |
| 006 | First-fit vs. best-fit | Planned |
| 007 | Profiler findings | Planned |
| 008 | Segregated lists | Conditional on profiling |
