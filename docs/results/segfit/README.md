# ADR-011 results: segregated fits

| File | What |
|---|---|
| `timing-current*.csv`, `timing-summary.md` | `run_all.sh`, 15 reps, 2026-11-07 19:42. **In this run `heap` = first-fit** (the default was switched to segregated fits only after this run); `heap_segfit` = segregated fits. Machine was under load (see `timing-machine-state.txt`); compare within this run only. Clean re-run scheduled in Phase 17. |
| `footprint-segregated.csv`, `realloc-segregated.csv` | `footprint_probe --segregated` and `footprint_probe realloc --segregated`. Deterministic; CI checks them byte for byte. First-fit/best-fit references are in `../phase6/`. |
| `freelist-length.txt` | `freelist_length <workload> [--best-fit\|--segregated]`: free-list length and nodes visited per malloc. |
| `merge-counts.txt` | Splits per malloc and merges per free (scratch-instrumented copy; see the file header). |
| Profiles | `../profiles/{seg,ff}_{rand,app}.md`: Time Profiler, same binary, 200 reps each. |
