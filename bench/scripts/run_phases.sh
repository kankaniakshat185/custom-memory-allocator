#!/usr/bin/env bash
# Benchmarks each phase commit with the *current* harness (ADR-008): extracts
# the commit's source with `git archive` (no worktree, git state untouched),
# compiles the harness against it (same -O3 -DNDEBUG as the CMake Release
# build, native arm64) and runs the heap allocator only.
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
# PHASES selects a subset, e.g. PHASES="phase3 phase4". Phase 2 is excluded by
# default: without coalescing its first-fit search makes a 1M-op workload run
# for hours (ADR-008).
phases=${PHASES:-"phase1 phase3 phase4"}
for spec in phase1:9baf140 phase2:4c8d98e phase3:0ad7359 phase4:62ae343; do
    case " $phases " in *" ${spec%%:*} "*) ;; *) continue ;; esac
    label=${spec%%:*}
    commit=${spec##*:}
    n=$reps
    [ "$label" = phase2 ] && n=$phase2_reps
    src="$tmp/$label"
    mkdir -p "$src"
    git archive "$commit" src include | tar -x -C "$src"
    clang++ -std=c++17 -O3 -DNDEBUG -arch arm64 -I"$src/src" -I"$src/include" \
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
