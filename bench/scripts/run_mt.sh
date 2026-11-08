#!/usr/bin/env bash
# Multithreaded throughput and scaling (ADR-016): libmalloc, the shipped
# per-thread arenas, one global lock, and (1 thread only) no lock. Variants
# alternate, ROUNDS times, so drift on a busy machine hits all of them.
#
# Writes to OUT_DIR (default docs/results/mt):
#   mt.csv                    throughput per thread count (wall clock: needs a
#                             quiet machine).
#   instructions_1thread.csv  single-thread instructions retired per rep, from
#                             /usr/bin/time -l ((5 reps - 0 reps) / 5, each the
#                             median of 3 runs): the cost of locking and arena
#                             lookup, repeatable to a few % even under load.
#   machine-state.txt         load average and top processes.
# Usage: bench/scripts/run_mt.sh [BUILD_DIR]   (needs a Release build)
set -euo pipefail

BUILD=${1:-build}
OUT_DIR=${OUT_DIR:-docs/results/mt}
REPS=${REPS:-5}
ROUNDS=${ROUNDS:-2}
THREADS=${THREADS:-1,2,4,8}
TRACES=${TRACES:-app_like random}

mkdir -p "$OUT_DIR"
unset MallocNanoZone  # libmalloc's default configuration (ADR-008)
out="$OUT_DIR/mt.csv"
echo "round,variant,allocator,trace,threads,reps,median_mops,iqr_pct,median_ns_per_op_per_thread" >"$out"
for r in $(seq 1 "$ROUNDS"); do
    for t in $TRACES; do
        run() {  # variant binary alloc threads
            "$BUILD/$2" --alloc "$3" --threads "$4" --trace "$t" --reps "$REPS" | tail -n +2 |
                sed "s/^/$r,$1,/" >>"$out"
        }
        run libmalloc mt_bench system "$THREADS"
        run arenas mt_bench myalloc "$THREADS"
        run global_lock mt_bench_global myalloc "$THREADS"
        run no_lock mt_bench_nolock myalloc 1
    done
done

instructions() {  # binary alloc trace reps -> median of 3 runs
    for _ in 1 2 3; do
        /usr/bin/time -l "$BUILD/$1" --alloc "$2" --threads 1 --trace "$3" --reps "$4" 2>&1 \
            >/dev/null | awk '/instructions retired/ {print $1}'
    done | sort -n | sed -n 2p
}
ins="$OUT_DIR/instructions_1thread.csv"
echo "variant,trace,instructions_per_rep" >"$ins"
for t in $TRACES; do
    while read -r variant binary alloc; do
        base=$(instructions "$binary" "$alloc" "$t" 0)
        full=$(instructions "$binary" "$alloc" "$t" 5)
        echo "$variant,$t,$(((full - base) / 5))" >>"$ins"
    done <<VARIANTS
libmalloc mt_bench system
arenas mt_bench myalloc
global_lock mt_bench_global myalloc
no_lock mt_bench_nolock myalloc
VARIANTS
done

{
    echo "load_average: $(uptime | sed 's/.*load averages*: //')"
    echo "top_cpu:"
    set +o pipefail
    ps -Ao pcpu,comm -r | sed -n 2,4p
} >"$OUT_DIR/machine-state.txt"
column -s, -t "$out"
column -s, -t "$ins"
