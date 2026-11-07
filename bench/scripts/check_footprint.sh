#!/usr/bin/env bash
# Regression check (CI): the footprint probe is deterministic, so its output
# must match the committed reference CSVs byte for byte. A difference means a
# change altered placement decisions (which block a request gets), which must
# be intentional and re-recorded in an ADR.
# References: the Phase 6 probe results (ADR-009); ADR-012 verified that the
# use-after-free hardening left them byte-identical.
# Observed byte-identical on macOS arm64 (libc++, Apple clang) and on Linux
# arm64 and x86_64 (libstdc++; GCC 13 and Clang 18; x86_64 in CI). The C++ standard does not guarantee identical random distributions
# or libm results across libraries/platforms, so a mismatch on a *new*
# platform may come from the library rather than the allocator.
#   bench/scripts/check_footprint.sh [build_dir]
set -euo pipefail
cd "$(dirname "$0")/../.."
probe=${1:-build}/footprint_probe
ref=docs/results/phase6
status=0
check() {
    local name=$1; shift
    if diff -u "$ref/$name.csv" <("$probe" "$@"); then
        echo "ok   $name"
    else
        echo "FAIL $name: probe output differs from $ref/$name.csv" >&2
        status=1
    fi
}
check footprint-firstfit
check footprint-bestfit --best-fit
check realloc-firstfit realloc
check realloc-bestfit realloc --best-fit
exit $status
