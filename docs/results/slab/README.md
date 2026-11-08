# ADR-014 results: small-object slab

| File | What |
|---|---|
| `footprint-slab.csv`, `realloc-slab.csv` | `footprint_probe --slab` and `footprint_probe realloc --slab` (slab v2: slabs carved from the heap). Compare with `../segfit/footprint-segregated.csv`, `../segfit/realloc-segregated.csv`. |
| `timing-current*.csv`, `timing-summary.md` | 15 reps, 2026-11-08 10:03: `system`, `heap` (= segregated fits, the default) and `slab_segfit` (slab v2 + segregated fits) in one run. Machine under load (`timing-machine-state.txt`); compare within this run. |
| `realloc-mechanism.txt` | Probes of the realloc-workload regression: realloc moves, slab fullness at the peak, high-water growth caused by taking slabs. |

Slab v1 (a separate slab region) was measured before v2 and replaced; its footprint numbers are quoted in ADR-014 (utilisation app-like 0.818, random 0.810, phase shift 0.856).
