#!/usr/bin/env python3
"""Render the GRO paper teaser figure.

Single subplot showing IterR (random vs most-delayed selection, both with
normal TD-Dijkstra reroute) on one representative 100k Beijing peak-hour
workload. The figure motivates the paper by visualizing that IterR's TTT
oscillates instead of decreasing steadily and that picking queries at random
can sometimes outperform picking the most-delayed ones.

The styling mirrors python/plot_gro_component_ablation.py (which is frozen):
hollow circle markers, dashed lines, log10(TTT in hours) y-axis, "initial"
label at iter 0, and the same per-selection colors. The frozen plotter is
not imported, only its visual conventions are reproduced here.
"""

from __future__ import annotations

import argparse
import csv
import math
import os
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker


# Colors mirrored from plot_gro_component_ablation.SELECTION_STYLE.
SELECTION_COLOR = {
    "random": "#8F6CCF",       # purple
    "most_delayed": "#64BEE8", # light blue
}
SELECTION_LABEL = {
    "random": "Random",
    "most_delayed": "Most-delayed",
}
# Normal TD-Dijkstra style mirrored from REROUTE_STYLE.
LINESTYLE = "--"
MARKER = "o"


def load_traces(path: Path, seeds: list[int]) -> dict[str, list[float]]:
    """Per selection_method, return the geometric-mean TTT trace across seeds.

    Geometric mean (i.e. mean of log10(TTT/3600)) is what the log y-axis
    actually displays, so a curve labelled "mean" reads as the visual midpoint
    of its constituents rather than being pulled up by a single bad seed.
    """
    by_seed = {sm: {} for sm in SELECTION_COLOR}
    with path.open() as f:
        for row in csv.DictReader(f):
            sm = row["selection_method"]
            seed = int(row["seed"])
            if sm not in by_seed or seed not in seeds:
                continue
            by_seed[sm].setdefault(seed, []).append(
                (int(row["iteration"]), int(row["total_before"]), int(row["total_after"]))
            )

    out = {}
    for sm in SELECTION_COLOR:
        per_seed_traces = []
        for seed in seeds:
            recs = sorted(by_seed[sm].get(seed, []))
            if not recs:
                raise SystemExit(f"no rows for selection={sm} seed={seed} in {path}")
            per_seed_traces.append([recs[0][1]] + [r[2] for r in recs])
        K = min(len(t) for t in per_seed_traces)
        out[sm] = []
        for it in range(K):
            logs = [math.log10(max(t[it] / 3600.0, 1e-12)) for t in per_seed_traces]
            out[sm].append(10 ** (sum(logs) / len(logs)) * 3600.0)
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--input",
        default="python/results/experiments/exp_teaser/bj_peak1h_100k/iterr_phi30_K10_capacity2_cap10e8.csv",
    )
    ap.add_argument("--seeds", default="2",
                    help="Comma-separated seed list (BJRealRep10-<seed>). Multiple => geo-mean across seeds.")
    ap.add_argument("--output",
                    default="python/results/experiments/exp_teaser/bj_peak1h_100k/teaser_iterr_phi30_seed2.pdf")
    ap.add_argument("--fig-width", type=float, default=3.3, help="inches (VLDB single column)")
    ap.add_argument("--fig-height", type=float, default=2.35, help="inches")
    ap.add_argument("--linewidth", type=float, default=1.4)
    ap.add_argument("--markersize", type=float, default=6.0)
    ap.add_argument("--markeredgewidth", type=float, default=1.6)
    args = ap.parse_args()

    seeds = [int(s) for s in args.seeds.split(",")]
    traces = load_traces(Path(args.input), seeds)
    max_iter = min(len(v) - 1 for v in traces.values())

    fig, ax = plt.subplots(figsize=(args.fig_width, args.fig_height))

    # Plot most-delayed first so random sits on top (random is the punchline).
    for sm in ("most_delayed", "random"):
        ttts = traces[sm][: max_iter + 1]
        x = list(range(len(ttts)))
        y = [math.log10(max(t / 3600.0, 1e-12)) for t in ttts]
        color = SELECTION_COLOR[sm]
        ax.plot(
            x, y,
            color=color,
            marker=MARKER,
            linestyle=LINESTYLE,
            linewidth=args.linewidth,
            markersize=args.markersize,
            markerfacecolor="none",
            markeredgecolor=color,
            markeredgewidth=args.markeredgewidth,
            label=SELECTION_LABEL[sm],
            alpha=0.95,
            zorder=3,
        )

    ax.set_xlim(0, max_iter)
    ax.set_xticks(list(range(0, max_iter + 1)))
    ax.xaxis.set_major_formatter(
        mticker.FuncFormatter(lambda v, _: "initial" if v == 0 else f"{int(v)}")
    )
    ax.tick_params(axis="both", labelsize=9, pad=2)
    ax.set_xlabel("Iteration number", fontsize=10)
    ax.set_ylabel("Log. Total travel time (h)", fontsize=10)

    ax.legend(
        loc="upper right",
        fontsize=9,
        frameon=False,
        handlelength=1.6,
        handletextpad=0.4,
        labelspacing=0.25,
        borderaxespad=0.4,
    )

    ax.grid(False)
    # Match frozen plotter: avoid scientific offset on y-axis when small.
    ax.ticklabel_format(axis="y", style="plain", useOffset=False)

    out_pdf = Path(args.output)
    out_pdf.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout(pad=0.4)
    fig.savefig(out_pdf, dpi=300, bbox_inches="tight")
    out_png = out_pdf.with_suffix(".png")
    fig.savefig(out_png, dpi=300, bbox_inches="tight")
    plt.close(fig)
    print(f"saved {out_pdf}")
    print(f"saved {out_png}")


if __name__ == "__main__":
    main()
