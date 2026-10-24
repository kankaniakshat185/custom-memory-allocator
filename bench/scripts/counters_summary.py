#!/usr/bin/env python3
"""Summarises an xctrace "CPU Counters" trace (ADR-010).

The default CPU Counters template records, per counter sample, the share of
pipeline slots that were useful (retiring), delivery-bound (front end),
processing-bound (back end, incl. memory) or discarded (mis-speculation), plus
the sample's cycle count and core. This script cycle-weights those shares over
the whole process and reports total cycles, so two runs doing identical work
can be compared slot-by-slot. The "Sum" column should be ~100%.

    python3 bench/scripts/counters_summary.py A.trace [B.trace ...]
"""
import collections
import subprocess
import sys
import xml.etree.ElementTree as ET

METRICS = ("useful", "delivery", "processing", "discarded")


def summarise(trace):
    # MetricTable has one row per (counter sample, metric): the sample's cycle
    # count (pmi-event "cycle") and each bottleneck as a documented ratio of
    # pipeline slots. (MetricAggregationForProcess sums ratios over samples,
    # so its values are not fractions; it is not used.)
    xml = subprocess.run(
        ["xcrun", "xctrace", "export", "--input", trace, "--xpath",
         '/trace-toc/run[@number="1"]/data/table[@schema="MetricTable"]'],
        check=True, capture_output=True, text=True).stdout
    root = ET.fromstring(xml)
    ids = {el.attrib["id"]: el for el in root.iter() if "id" in el.attrib}

    def deref(el):
        return ids[el.attrib["ref"]] if "ref" in el.attrib else el

    samples = collections.defaultdict(dict)  # (timestamp, core) -> {metric: value}
    for row in root.iter("row"):
        cells = [deref(c) for c in row]
        ts, event, value, core = cells[0].text, cells[2].text, cells[6].text, cells[7].attrib.get("fmt", "")
        samples[(ts, core)][event] = float(value.replace(",", ""))
        samples[(ts, core)]["_core"] = core
    cycles = 0.0
    p_cycles = 0.0
    weighted = collections.Counter()
    for m in samples.values():
        c = m.get("cycle", 0.0)
        if not c or not all(k in m for k in METRICS):
            continue
        cycles += c
        if "P Core" in m["_core"]:
            p_cycles += c
        for k in METRICS:
            weighted[k] += m[k] * c
    return cycles, {k: weighted[k] / cycles for k in METRICS}, p_cycles / cycles


def main():
    rows = [(t, *summarise(t)) for t in sys.argv[1:]]
    print("| Trace | Cycles | On P cores | Useful | Delivery | Processing | Discarded | Sum |")
    print("|---|---|---|---|---|---|---|---|")
    for t, cycles, s, p_share in rows:
        total = sum(s.values())
        print(f"| {t.split('/')[-1]} | {cycles / 1e9:.3f} G | {100 * p_share:.0f}% | {100 * s['useful']:.1f}% "
              f"| {100 * s['delivery']:.1f}% | {100 * s['processing']:.1f}% | {100 * s['discarded']:.1f}% "
              f"| {100 * total:.0f}% |")


if __name__ == "__main__":
    main()
