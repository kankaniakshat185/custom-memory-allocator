#!/usr/bin/env bash
# Benchmarks each phase commit with the *current* harness (ADR-008): extracts
# the commit's source with `git archive` (no worktree, git state untouched),
# compiles the harness against it and runs the heap allocator only.
#   bench/scripts/run_phases.sh [out_dir] [reps]
# Phase 2 (splitting without coalescing) runs PHASE2_REPS reps (default 3):
# its free list grows without bound, so one rep of the adversarial workload
# takes minutes, and it is a known-broken intermediate state whose timing only
# needs to show the trend (ADR-008).
set -euo pipefail
cd "$(dirname "$0")/../.."
out=${1:-docs/results}
reps=${2:-15}
phase2_reps=${PHASE2_REPS:-3}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
summary="$out/timing-phases.csv"
raw="$out/timing-phases-raw.csv"
: > "$raw"
first=1
for spec in phase1:9baf140 phase2:4c8d98e phase3:0ad7359 phase4:62ae343; do
    label=${spec%%:*}
    commit=${spec##*:}
    n=$reps
    [ "$label" = phase2 ] && n=$phase2_reps
    src="$tmp/$label"
    mkdir -p "$src"
    git archive "$commit" src include | tar -x -C "$src"
    clang++ -std=c++17 -O2 -DNDEBUG -I"$src/src" -I"$src/include" \
        "$src"/src/*.cpp bench/bench_main.cpp bench/workloads.cpp -o "$src/bench"
    if [ $first = 1 ]; then
        env -u MallocNanoZone "$src/bench" --label "$label" --reps "$n" --raw "$raw" --alloc heap > "$summary"
        first=0
    else
        env -u MallocNanoZone "$src/bench" --label "$label" --reps "$n" --raw "$raw" --alloc heap | tail -n +2 >> "$summary"
    fi
    echo "done $label" >&2
done
column -s, -t "$summary"
