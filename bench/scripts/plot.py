#!/usr/bin/env python3
"""Renders the result charts from the committed result files (light + dark PNGs).

Every chart is computed from files under docs/results/, using the same
statistics as summarize.py (median across reps), so the charts and the tables
in the README/ADRs come from the same numbers. Output: docs/results/charts/.

    python3 bench/scripts/plot.py            # requires matplotlib

Design rules (from the dataviz method): one y-scale per panel and small
multiples where workloads differ by orders of magnitude; emphasis (this
allocator highlighted, baselines in gray) instead of a rainbow; categorical
colors only for genuinely categorical series, in a fixed validated order;
recessive hairline grid; light and dark variants rendered separately.
"""
import collections
import csv
import re
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
RES = ROOT / "docs" / "results"
OUT = RES / "charts"

# Reference palette of the dataviz method. Categorical slots 1-5 in fixed order,
# validated with its validate_palette.js (worst adjacent CVD dE 9.1 light / 8.4
# dark). Light-mode aqua/yellow are below 3:1 contrast, so segments are labelled
# and every chart has a table twin in the README or its ADR.
THEMES = {
    "light": dict(surface="#fcfcfb", ink="#0b0b0b", ink2="#52514e", muted="#898781",
                  grid="#e1e0d9", axis="#c3c2b7", base1="#898781", base2="#c3c2b7",
                  accent="#2a78d6", accent2="#86b6ef",
                  cat=["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4"],
                  on_cat=["#ffffff", "#ffffff", "#ffffff", "#0b0b0b", "#ffffff"]),
    "dark": dict(surface="#1a1a19", ink="#f0efec", ink2="#c3c2b7", muted="#898781",
                 grid="#2c2c2a", axis="#383835", base1="#898781", base2="#52514e",
                 accent="#3987e5", accent2="#184f95",
                 cat=["#3987e5", "#d95926", "#199e70", "#c98500", "#d55181"],
                 on_cat=["#ffffff", "#ffffff", "#ffffff", "#0b0b0b", "#ffffff"]),
}

WORKLOADS = [("random_16B_64KiB", "Random sizes"), ("app_like", "App-like"),
             ("adversarial_holes", "Adversarial"), ("realloc_growth", "realloc growth")]


# ---------------------------------------------------------------- data


def quantile(v, p):
    v = sorted(v)
    i = p * (len(v) - 1)
    lo = int(i)
    hi = min(lo + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (i - lo)


def raw_stats(path):
    """{(label, allocator, workload): (median, IQR %, failures/rep)} from a *-raw.csv."""
    reps = collections.defaultdict(list)
    fails = {}
    for r in csv.reader(path.open()):
        reps[(r[0], r[1], r[2])].append(float(r[4]))
        fails[(r[0], r[1], r[2])] = int(r[5])
    out = {}
    for k, v in reps.items():
        m = quantile(v, 0.5)
        out[k] = (m, 100 * (quantile(v, 0.75) - quantile(v, 0.25)) / m, fails[k])
    return out


def raw_medians(path):
    """{(label, allocator, workload): (median ns/op, failures/rep)} from a *-raw.csv."""
    return {k: (m, f) for k, (m, _, f) in raw_stats(path).items()}


def probe(path):
    """{workload: row dict} from a footprint-probe CSV."""
    return {r["workload"]: r for r in csv.DictReader(path.open())}


# ---------------------------------------------------------------- chrome


def setup(theme):
    t = THEMES[theme]
    plt.rcParams.update({
        "font.family": ["Helvetica Neue", "Helvetica", "Arial", "DejaVu Sans"],
        "font.size": 10, "axes.titlesize": 11, "axes.titleweight": "bold",
        "figure.facecolor": t["surface"], "axes.facecolor": t["surface"],
        "savefig.facecolor": t["surface"], "text.color": t["ink"],
        "axes.labelcolor": t["ink2"], "xtick.color": t["ink2"], "ytick.color": t["muted"],
        "axes.edgecolor": t["axis"], "axes.titlecolor": t["ink"],
        "axes.spines.top": False, "axes.spines.right": False, "axes.spines.left": False,
        "axes.grid": True, "axes.grid.axis": "y", "grid.color": t["grid"],
        "grid.linewidth": 0.8, "grid.linestyle": "-", "axes.axisbelow": True,
        "xtick.major.size": 0, "ytick.major.size": 0, "legend.frameon": False,
    })
    return t


def bar(ax, x, h, color, t, width=0.72):
    # A surface-colored edge gives the 2px gap between adjacent bars.
    return ax.bar(x, h, width=width, color=color, edgecolor=t["surface"], linewidth=1.5, zorder=2)


def value_label(ax, x, h, text, t, ymax, bold=False):
    ax.text(x, h + ymax * 0.015, text, ha="center", va="bottom", fontsize=8.5,
            color=t["ink"] if bold else t["ink2"], fontweight="bold" if bold else "normal")


def save(fig, name, theme, note=None):
    t = THEMES[theme]
    if note:
        fig.text(0.01, 0.01, note, fontsize=8, color=t["muted"], ha="left", va="bottom")
    OUT.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT / f"{name}-{theme}.png", dpi=160, bbox_inches="tight", pad_inches=0.2)
    plt.close(fig)


def fmt(v):
    return f"{v:,.0f}" if v >= 100 else f"{v:.1f}"


# ---------------------------------------------------------------- charts


def chart_versions(theme):
    """Speed of every version, one panel per workload (scales differ ~100x)."""
    t = setup(theme)
    data = raw_stats(RES / "final" / "timing-phases-raw.csv")
    versions = [("phase1", "Phase 1"), (None, "Phase 2"), ("phase3", "Phase 3"),
                ("phase4", "Phase 4"), ("phase6", "Phase 6"), ("worktree", "Hardened")]
    fig, axes = plt.subplots(1, 4, figsize=(13, 3.6))
    for ax, (wl, title) in zip(axes, WORKLOADS):
        vals = [data.get((v, "heap", wl)) if v else None for v, _ in versions]
        ymax = max(m for m, _, _ in filter(None, vals)) * 1.3
        for i, ((key, _), v) in enumerate(zip(versions, vals)):
            if v is None:
                untimeable = key is None and wl != "realloc_growth"
                ax.text(i, ymax * 0.04, "untimeable" if untimeable else "n/a", ha="center",
                        va="bottom", fontsize=8, color=t["muted"], rotation=90 if untimeable else 0)
                continue
            m, iqr, fails = v
            hardened = key == "worktree"
            color = t["base2"] if fails else (t["accent"] if hardened else t["accent2"])
            bar(ax, i, m, color, t)
            label = fmt(m) + ("\nOOM, n/c" if fails else "") + ("\nnoisy" if iqr > 10 else "")
            value_label(ax, i, m, label, t, ymax, bold=hardened)
        ax.set_title(title, loc="left")
        ax.set_ylim(0, ymax)
        ax.set_xticks(range(len(versions)), [n for _, n in versions], rotation=35, ha="right")
    axes[0].set_ylabel("ns per operation (lower is better)")
    fig.suptitle("Every version on the same traces, one run (median of 15 reps)", x=0.01, ha="left",
                 fontsize=12, fontweight="bold", color=t["ink"])
    fig.tight_layout(rect=(0, 0.04, 1, 0.96))
    save(fig, "versions", theme,
         "Phase 2 (splitting without coalescing) never finished a run. Phase 1 app-like fails 436 "
         "allocations per rep, so its time is not comparable (n/c). noisy = IQR > 10%. "
         "realloc exists from Phase 4. Source: docs/results/final/.")


def chart_vs_libmalloc(theme):
    """This allocator (highlighted) against the macOS system allocator (gray)."""
    t = setup(theme)
    data = raw_medians(RES / "final" / "timing-current-raw.csv")
    allocs = [("system", "libmalloc", t["base1"]), ("system_nano_off", "libmalloc\nNano off", t["base2"]),
              ("heap", "this: first-fit", t["accent"]), ("heap_bestfit", "this: best-fit", t["accent2"])]
    fig, axes = plt.subplots(1, 4, figsize=(13, 3.6))
    for ax, (wl, title) in zip(axes, WORKLOADS):
        vals = [data[("current", a, wl)][0] for a, _, _ in allocs]
        ymax = max(vals) * 1.18
        for i, ((key, _, color), v) in enumerate(zip(allocs, vals)):
            bar(ax, i, v, color, t)
            value_label(ax, i, v, fmt(v), t, ymax, bold=key == "heap")
        ax.set_title(title, loc="left")
        ax.set_ylim(0, ymax)
        ax.set_xticks(range(len(allocs)), [n for _, n, _ in allocs], rotation=35, ha="right")
    axes[0].set_ylabel("ns per operation (lower is better)")
    fig.suptitle("Against the macOS system allocator, one run (median of 15 reps)", x=0.01, ha="left",
                 fontsize=12, fontweight="bold", color=t["ink"])
    fig.tight_layout(rect=(0, 0.04, 1, 0.96))
    save(fig, "vs-libmalloc", theme,
         "libmalloc is thread-safe; this allocator is single-threaded. Apple M2, native arm64, -O3. "
         "Source: docs/results/final/.")


def chart_footprint(theme):
    """Memory utilisation per version/policy, one panel per workload."""
    t = setup(theme)
    runs = [("Phase 1", RES / "footprint-phase1-baseline.csv"),
            ("Phase 2", RES / "footprint-phase2-splitting.csv"),
            ("Phase 3+\nfirst-fit", RES / "footprint-phase3-coalescing.csv"),
            ("Best-fit", RES / "phase6" / "footprint-bestfit.csv")]
    rows = [(name, probe(p)) for name, p in runs]
    wls = [("random_16B_4KiB", "Random sizes"), ("app_like", "App-like"), ("phase_shift", "Phase shift")]
    fig, axes = plt.subplots(1, 3, figsize=(10.5, 3.6))
    for ax, (wl, title) in zip(axes, wls):
        for i, (name, data) in enumerate(rows):
            u = float(data[wl]["utilisation"])
            oom = int(data[wl]["oom"])
            current = name.startswith("Phase 3")
            bar(ax, i, u, t["accent"] if current else t["accent2"], t)
            shown = f"{u:.3f}" if u >= 0.99 else f"{u:.2f}"  # never round 0.997 up to 1.00
            value_label(ax, i, u, shown + (f"\n{oom:,} OOM" if oom else ""), t, 1.2, bold=current)
        ax.set_title(title, loc="left")
        ax.set_ylim(0, 1.2)
        ax.set_yticks([0, 0.25, 0.5, 0.75, 1.0])
        ax.set_xticks(range(len(rows)), [n for n, _ in rows])
    axes[0].set_ylabel("utilisation: peak live ÷ high-water\n(higher is better)")
    fig.suptitle("Memory: how much of the touched arena holds live data", x=0.01, ha="left",
                 fontsize=12, fontweight="bold", color=t["ink"])
    fig.tight_layout(rect=(0, 0.04, 1, 0.96))
    save(fig, "footprint", theme,
         "Deterministic footprint probe; OOM = failed allocations. Phase 3 onwards (first-fit, incl. "
         "hardened) is byte-identical. Source: docs/results/.")


def chart_hardening(theme):
    """Overhead of the use-after-free checks across the four design iterations."""
    t = setup(theme)
    hd = RES / "hardening"
    iters = [("v1", [hd / "v1-footer-check" / f"timing-run{r}-raw.csv" for r in (1, 2)]),
             ("v2", [hd / "v2-no-footer-check" / f"timing-run{r}-raw.csv" for r in (1, 2, 3)]),
             ("v3", [hd / "v3-in-heap-only" / f"timing-run{r}-raw.csv" for r in (1, 2)]),
             ("v4\n(final)", [hd / "v4-in-heap-free-bit" / f"timing-run{r}-raw.csv" for r in (1, 2)])]
    series = [("heap_bestfit", "app_like", "best-fit, app-like"),
              ("heap_bestfit", "random_16B_64KiB", "best-fit, random"),
              ("heap", "app_like", "first-fit, app-like"),
              ("heap", "random_16B_64KiB", "first-fit, random"),
              ("heap", "adversarial_holes", "first-fit, adversarial")]
    xs = range(len(iters))
    ends = []
    fig, ax = plt.subplots(figsize=(8.5, 4.2))
    ax.spines["bottom"].set_visible(False)
    ax.axhline(0, color=t["axis"], linewidth=1, zorder=1)
    for si, (alloc, wl, name) in enumerate(series):
        ys = []
        for _, files in iters:
            deltas = []
            for f in files:
                d = raw_medians(f)
                deltas.append(100 * (d[("current", alloc, wl)][0] / d[("phase6", alloc, wl)][0] - 1))
            ys.append(sum(deltas) / len(deltas))
        color = t["cat"][si]
        ax.plot(xs, ys, color=color, linewidth=2, marker="o", markersize=7,
                markeredgecolor=t["surface"], markeredgewidth=2, zorder=3, label=name)
        ends.append([ys[-1], ys[-1], f"{name}  {ys[-1]:+.0f}%"])
    # Direct end labels, nudged apart so equal end values do not overlap.
    ends.sort(key=lambda e: e[0])
    for i in range(1, len(ends)):
        ends[i][1] = max(ends[i][1], ends[i - 1][1] + 4.5)
    for _, y, text in ends:
        ax.text(len(iters) - 1 + 0.08, y, text, va="center", fontsize=8.5, color=t["ink2"])
    ax.set_xticks(list(xs), [n for n, _ in iters])
    ax.set_xlim(-0.2, len(iters) + 0.9)
    ax.set_ylabel("overhead vs. unhardened Phase 6 (%)")
    ax.legend(loc="upper right", fontsize=8.5, labelcolor=t["ink2"], ncol=1)
    ax.set_title("Use-after-free hardening: overhead across four measured iterations", loc="left")
    fig.tight_layout(rect=(0, 0.05, 1, 1))
    save(fig, "hardening", theme,
         "v1 full check incl. footer · v2 no footer read · v3 in-heap only · v4 in-heap + free bit. "
         "Mean of 2-3 same-run comparisons each. Source: docs/results/hardening/.")


def chart_bottlenecks(theme):
    """CPU Counters bottleneck breakdown (share of pipeline slots), 100% stacked."""
    t = setup(theme)
    text = (RES / "profiles" / "cpu-counters.md").read_text()
    names = {"cc_p6_app": "Phase 6, app-like", "cc_cur_app": "Hardened, app-like",
             "cc_p6_rand": "Phase 6, random", "cc_cur_rand": "Hardened, random",
             "cc_cur_adv": "Hardened, adversarial", "cc_cur_bf_rand": "Best-fit, random",
             "cc_p1_adv": "Phase 1, adversarial"}
    rows = []
    header = None
    for line in text.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if cells[0] == "Trace":
            header = cells
        elif header and cells[0].endswith(".trace"):
            col = {h: c for h, c in zip(header, cells)}
            rows.append((names[cells[0].removesuffix(".trace")],
                         [float(col[k].rstrip("%")) for k in ("Useful", "Delivery", "Processing", "Discarded")]))
    cats = ["Useful (retiring)", "Delivery (front end)", "Processing (back end / memory)",
            "Discarded (mis-speculation)"]
    fig, ax = plt.subplots(figsize=(10, 4.2))
    ax.grid(axis="y", visible=False)
    ax.grid(axis="x", visible=True)
    for ri, (name, vals) in enumerate(reversed(rows)):
        left = 0.0
        for ci, v in enumerate(vals):
            ax.barh(ri, v, left=left, height=0.66, color=t["cat"][ci], edgecolor=t["surface"],
                    linewidth=1.5, zorder=2)
            if v >= 7:  # label only where it fits with padding
                ax.text(left + v / 2, ri, f"{v:.0f}%", ha="center", va="center", fontsize=8,
                        color=t["on_cat"][ci])
            left += v
    ax.set_yticks(range(len(rows)), [n for n, _ in reversed(rows)])
    ax.set_xlim(0, 100)
    ax.set_xlabel("share of pipeline slots (cycle-weighted)")
    ax.legend([plt.Rectangle((0, 0), 1, 1, color=c) for c in t["cat"][:4]], cats, loc="lower center",
              bbox_to_anchor=(0.45, 1.0), ncol=4, fontsize=8.5, labelcolor=t["ink2"])
    ax.set_title("Where the CPU's pipeline slots go (Instruments CPU Counters)", loc="left", pad=28)
    fig.tight_layout(rect=(0, 0.05, 1, 1))
    save(fig, "bottlenecks", theme,
         "Typical workloads lose ~37-52% of slots to mis-speculation; long free-list walks are "
         "memory-bound. Source: docs/results/profiles/cpu-counters.md.")


def main():
    for theme in THEMES:
        chart_versions(theme)
        chart_vs_libmalloc(theme)
        chart_footprint(theme)
        chart_hardening(theme)
        chart_bottlenecks(theme)
    for p in sorted(OUT.glob("*.png")):
        print(p.relative_to(ROOT))


if __name__ == "__main__":
    main()
