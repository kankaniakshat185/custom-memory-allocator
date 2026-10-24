#!/usr/bin/env bash
# Timing run for the current tree (ADR-008). Writes summary + raw CSVs.
#   bench/scripts/run_all.sh [out_dir] [reps]
# The system allocator is run twice: libmalloc defaults (Nano zone on) and
# MallocNanoZone=0 (control). MallocNanoZone is always set explicitly, because
# some parent processes (e.g. editors) export MallocNanoZone=0 and would
# otherwise silently change the baseline.
set -euo pipefail
cd "$(dirname "$0")/../.."
out=${1:-docs/results}
reps=${2:-15}
mkdir -p "$out"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j8 --target bench_timing >/dev/null

summary="$out/timing-current.csv"
raw="$out/timing-current-raw.csv"
: > "$raw"
env -u MallocNanoZone ./build/bench_timing --reps "$reps" --raw "$raw" \
    --alloc system,heap,heap_naive_realloc,heap_growonly_realloc > "$summary"
env MallocNanoZone=0 ./build/bench_timing --reps "$reps" --raw "$raw" --alloc system | tail -n +2 >> "$summary"
column -s, -t "$summary"
