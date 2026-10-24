#!/usr/bin/env bash
# One-shot final timing run (ADR-008): records the machine state, then runs the
# current tree (system, system_nano_off, heap + realloc policies) and every
# phase commit. Best run from Terminal right after a restart, on AC power, with
# other apps closed.
#   bench/scripts/run_final.sh [reps]
set -euo pipefail
cd "$(dirname "$0")/../.."
reps=${1:-15}
out=docs/results
state="$out/timing-machine-state.txt"
# The state block runs without pipefail: `ps | head` makes ps exit on SIGPIPE,
# which pipefail + set -e would turn into a silent exit.
set +o pipefail
{
    echo "run started $(date '+%Y-%m-%d %H:%M:%S')"
    uptime
    pmset -g batt | head -1
    pmset -g | grep -i lowpowermode || true
    echo "MallocNanoZone in launching environment: ${MallocNanoZone-unset} (scripts set it explicitly)"
    echo "top processes:"
    ps -Ao pcpu,comm -r | head -6 | cut -c1-100
} > "$state"
set -o pipefail
echo "Machine state recorded in $state" >&2
echo "== current tree (about 2 min)" >&2
bench/scripts/run_all.sh "$out" "$reps"
echo "== phase commits (about 10 min; phase2 is the slow one)" >&2
bench/scripts/run_phases.sh "$out" "$reps"
{
    echo "run finished $(date '+%Y-%m-%d %H:%M:%S')"
    uptime
} >> "$state"
echo "Results in $out/timing-current.csv and $out/timing-phases.csv" >&2
