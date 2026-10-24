#!/usr/bin/env python3
"""Attributes Time Profiler samples to *inlined* source functions (ADR-010).

At -O3 the allocator's helpers (in_heap, plausible_free, unlinkable, ...) are
inlined into malloc/free, so a function-level profile cannot see them. This
script exports the raw sample addresses from an xctrace Time Profiler trace,
symbolizes each leaf address with `atos -i` (which reports the inlined call
chain and source line) and aggregates self time by innermost function, by
category, and by source line. Only samples inside the harness's timed
`replay()` loop (inlined into run_heap/run_system) are counted.

    python3 bench/scripts/profile_attrib.py TRACE.trace BINARY [--top N] [--md OUT.md]
"""
import argparse
import collections
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


def export_samples(trace):
    xml = subprocess.run(
        ["xcrun", "xctrace", "export", "--input", trace, "--xpath",
         '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]'],
        check=True, capture_output=True, text=True).stdout
    root = ET.fromstring(xml)
    ids = {el.attrib["id"]: el for el in root.iter() if "id" in el.attrib}

    def deref(el):
        return ids[el.attrib["ref"]] if el is not None and "ref" in el.attrib else el

    for row in root.iter("row"):
        state = deref(row.find("thread-state"))
        if state is None or state.text != "Running":
            continue
        weight = int(deref(row.find("weight")).text)
        bt = deref(row.find("tagged-backtrace"))
        if bt is None:
            bt = deref(row.find("backtrace"))
        if bt is None:
            continue
        inner = bt.find("backtrace")
        bt = deref(inner) if inner is not None else bt
        frames = [deref(f) for f in bt.findall("frame")]
        if not frames:
            continue
        names = [f.attrib.get("name", "") for f in frames]
        # replay() is inlined into run_heap()/run_system(), so match those too.
        if not any(k in n for n in names for k in ("replay", "run_heap", "run_system")):
            continue  # trace generation and setup, outside the timed loop
        leaf = frames[0]
        binary = deref(leaf.find("binary"))
        yield {
            "weight": weight,
            "addr": int(leaf.attrib["addr"], 16),
            "binary": binary.attrib.get("name", "?") if binary is not None else "?",
            "load": int(binary.attrib["load-addr"], 16) if binary is not None else 0,
            "leaf_name": names[0],
        }


def short(fn):
    """'myalloc::BasicHeap<myalloc::FirstFit>::in_heap(myalloc::Header const*) const' -> 'BasicHeap::in_heap'."""
    fn = fn.split(" (in ")[0].replace("(anonymous namespace)::", "")
    depth, out = 0, []
    for ch in fn:  # drop template arguments
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif depth == 0:
            out.append(ch)
    fn = "".join(out)
    fn = re.sub(r"\(.*", "", fn)  # drop parameter list
    fn = fn.split(" ")[-1]  # drop a return type, e.g. "RunResult replay"
    parts = [p for p in fn.split("::") if p not in ("myalloc", "bench", "std", "__1")]
    return "::".join(parts[-2:]) if len(parts) > 1 else (parts[0] if parts else fn)


def symbolize(binary_path, load, addrs):
    """Returns {addr: [(short_fn, 'file:line'), ...]} innermost first."""
    out = {}
    addrs = sorted(addrs)
    for i in range(0, len(addrs), 500):
        chunk = addrs[i:i + 500]
        text = subprocess.run(["xcrun", "atos", "-i", "-o", binary_path, "-l", hex(load)] +
                              [hex(a) for a in chunk], check=True, capture_output=True, text=True).stdout
        groups = text.strip("\n").split("\n\n")
        if len(groups) != len(chunk):
            sys.exit(f"atos returned {len(groups)} groups for {len(chunk)} addresses")
        for a, g in zip(chunk, groups):
            chain = []
            for line in g.splitlines():
                m = re.search(r"\(([^()]+:\d+)\)\s*$", line)
                chain.append((short(line), m.group(1) if m else "?"))
            out[a] = chain
    return out


# First match wins, scanning the inlined chain from innermost outwards.
CATEGORIES = [
    ("search: walk + per-node check", ("FirstFit::find", "BestFit::find", "in_heap", "find_fit", "find_first_fit")),
    ("unlink: safe-unlinking checks", ("unlinkable", "plausible_free")),
    ("unlink: relink neighbours", ("unlink", "list_remove")),
    ("free: merge (release)", ("release", "prev_block", "write_footer")),
    ("malloc: split (place)", ("place",)),
    ("list push", ("list_push",)),
    ("free: pointer validation", ("validate",)),
    ("stats / extent", ("note_extent",)),
    # "Heap::x" also matches "BasicHeap::x", so older phases (class Heap) are covered.
    ("realloc body", ("Heap::realloc",)),
    ("malloc body", ("Heap::malloc",)),
    ("free body", ("Heap::free",)),
]


def category(chain):
    for fn, _ in chain:
        for name, keys in CATEGORIES:
            if any(fn.endswith(k) or fn == k for k in keys):
                return name
    return "harness (replay loop, touch)"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("binary")
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--md")
    args = ap.parse_args()

    samples = list(export_samples(args.trace))
    total = sum(s["weight"] for s in samples)
    if not total:
        sys.exit("no samples inside replay()")
    own = [s for s in samples if s["binary"] == "bench"]
    load = own[0]["load"] if own else 0
    chains = symbolize(args.binary, load, {s["addr"] for s in own}) if own else {}

    by_cat, by_fn, by_line = (collections.Counter() for _ in range(3))
    for s in samples:
        if s["binary"] != "bench":
            key = f"{s['binary']}: {s['leaf_name']}"
            by_cat[f"outside binary ({s['binary']})"] += s["weight"]
            by_fn[key] += s["weight"]
            by_line[key] += s["weight"]
            continue
        chain = chains[s["addr"]]
        by_cat[category(chain)] += s["weight"]
        by_fn[chain[0][0]] += s["weight"]
        by_line[f"{chain[0][1]} ({chain[0][0]})"] += s["weight"]

    def table(title, counter, n):
        rows = [f"### {title}", "", "| Share | Samples | Where |", "|---|---|---|"]
        for k, w in counter.most_common(n):
            rows.append(f"| {100 * w / total:5.1f}% | {w // 1_000_000} | {k} |")
        return rows

    lines = [f"Samples inside replay(): {total // 1_000_000} (1 ms each), trace `{args.trace}`", ""]
    lines += table("By category", by_cat, 20) + [""]
    lines += table("By innermost (inlined) function", by_fn, args.top) + [""]
    lines += table("By source line", by_line, args.top)
    text = "\n".join(lines) + "\n"
    print(text)
    if args.md:
        open(args.md, "w").write(text)


if __name__ == "__main__":
    main()
