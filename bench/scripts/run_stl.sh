#!/usr/bin/env bash
# Standard-container workloads under three global operator new/delete
# implementations (ADR-015): libmalloc, myalloc_new (heap, the shipped
# replacement) and the bench-only heap + slab variant.
#
# Writes to OUT_DIR (default docs/results/stl):
#   instructions.csv  instructions retired per rep, from /usr/bin/time -l:
#                     (run with REPS reps - run with 0 reps) / REPS, so process
#                     start-up and the warm-up rep cancel out; each run is the
#                     median of 3. Repeatable to ~0.1% under load, unlike time.
#   rss.csv           peak resident set size of a 2-rep run per workload.
#   timing.csv        median ns/op with IQR; binaries alternate, ROUNDS times.
#                     Meaningful only on a quiet machine (docs/environment.md).
#
# Usage: bench/scripts/run_stl.sh [BUILD_DIR]   (macOS; needs a Release build)
set -euo pipefail

BUILD=${1:-build}
OUT_DIR=${OUT_DIR:-docs/results/stl}
REPS=${REPS:-5}
TIMING_REPS=${TIMING_REPS:-15}
ROUNDS=${ROUNDS:-2}
WORKLOADS="map_churn string_vector hash_count list_queue"
BINS="system myalloc slab"

mkdir -p "$OUT_DIR"
unset MallocNanoZone  # libmalloc's default configuration (ADR-008)

instructions() {  # binary reps workload -> median of 3 runs (single runs had outliers)
    for _ in 1 2 3; do
        /usr/bin/time -l "$BUILD/stl_bench_$1" --reps "$2" --workload "$3" 2>&1 >/dev/null |
            awk '/instructions retired/ {print $1}'
    done | sort -n | sed -n 2p
}

echo "allocator,workload,reps,instructions_per_rep" >"$OUT_DIR/instructions.csv"
for w in $WORKLOADS; do
    for b in $BINS; do
        base=$(instructions "$b" 0 "$w")
        full=$(instructions "$b" "$REPS" "$w")
        echo "$b,$w,$REPS,$(((full - base) / REPS))" >>"$OUT_DIR/instructions.csv"
    done
done

echo "allocator,workload,max_rss_bytes" >"$OUT_DIR/rss.csv"
for w in $WORKLOADS; do
    for b in $BINS; do
        rss=$(/usr/bin/time -l "$BUILD/stl_bench_$b" --reps 2 --workload "$w" 2>&1 >/dev/null |
            awk '/maximum resident set size/ {print $1}')
        echo "$b,$w,$rss" >>"$OUT_DIR/rss.csv"
    done
done

echo "round,allocator,workload,reps,median_ns_per_op,iqr_pct" >"$OUT_DIR/timing.csv"
for r in $(seq 1 "$ROUNDS"); do
    for b in $BINS; do
        "$BUILD/stl_bench_$b" --reps "$TIMING_REPS" 2>/dev/null | tail -n +2 |
            sed "s/^/$r,/" >>"$OUT_DIR/timing.csv"
    done
done

{
    echo "load_average: $(uptime | sed 's/.*load averages*: //')"
    echo "top_cpu:"
    set +o pipefail
    ps -Ao pcpu,comm -r | sed -n 2,4p
} >"$OUT_DIR/machine-state.txt"

column -s, -t "$OUT_DIR/instructions.csv"
column -s, -t "$OUT_DIR/rss.csv"
column -s, -t "$OUT_DIR/timing.csv"
