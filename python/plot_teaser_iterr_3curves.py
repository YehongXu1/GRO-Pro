#!/usr/bin/env python3
"""Render a 3-curve teaser comparison on a single seed.

Overlays on the same axes:
    - Random       @ phi=10
    - Most-delayed @ phi=10
    - Random       @ phi=30

The point is to show that (a) at fixed phi=10, random and most-delayed differ
in convergence vs. oscillation; and (b) increasing phi from 10 to 30 makes
random more aggressive but more oscillatory, not actually better.

Styling mirrors python/plot_gro_component_ablation.py: hollow markers, dashed
lines, log10(TTT in hours).
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


# Three curves. Same Random=purple / Most-delayed=blue as the frozen plotter.
# The second Random variant uses the same purple but a SOLID line, so two purple
# curves with different linestyles read as "same selection rule, different phi",
# and the lone dashed blue curve reads as "the heuristic alternative".
CURVES = [
    # (label,                          csv key, selection_method, color,     linestyle)
    (r"Random ($\phi$=10\%)",          "phi10", "random",         "#8F6CCF", "--"),
    (r"Random ($\phi$=30\%)",          "phi30", "random",         "#8F6CCF", "-"),
    (r"Most-delayed ($\phi$=10\%)",    "phi10", "most_delayed",   "#64BEE8", "--"),
]


def load(path: Path, seed: int, sm: str) -> list[float]:
    recs = []
    with path.open() as f:
        for row in csv.DictReader(f):
            if row["selection_method"] != sm or int(row["seed"]) != seed:
                continue
            recs.append((int(row["iteration"]), int(row["total_before"]), int(row["total_after"])))
    recs.sort()
    if not recs:
        raise SystemExit(f"no rows in {path} for sm={sm} seed={seed}")
    return [recs[0][1]] + [r[2] for r in recs]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input-phi10",
                    default="python/results/experiments/exp_teaser/bj_peak1h_100k/iterr_phi10_K10_capacity2_cap10e8.csv")
    ap.add_argument("--input-phi30",
                    default="python/results/experiments/exp_teaser/bj_peak1h_100k/iterr_phi30_K10_capacity2_cap10e8.csv")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--output",
                    default="python/results/experiments/exp_teaser/bj_peak1h_100k/cand_3curves_seed1.pdf")
    ap.add_argument("--fig-width",  type=float, default=3.5)
    ap.add_argument("--fig-height", type=float, default=2.4)
    ap.add_argument("--linewidth",  type=float, default=1.4)
    ap.add_argument("--markersize", type=float, default=6.0)
    ap.add_argument("--markeredgewidth", type=float, default=1.6)
    # No system pdflatex is required; we use matplotlib's mathtext. The
    # `stix` fontset renders Greek letters in a Times-like style that matches
    # VLDB body text and reads thicker than the default Computer Modern.
    ap.add_argument("--usetex", action="store_true",
                    help="Use system LaTeX (requires pdflatex on PATH).")
    ap.add_argument("--mathtext-fontset", default="stix",
                    choices=["stix", "cm", "stixsans", "dejavusans", "dejavuserif"])
    args = ap.parse_args()

    if args.usetex:
        matplotlib.rcParams["text.usetex"] = True
    else:
        matplotlib.rcParams["mathtext.fontset"] = args.mathtext_fontset

    inputs = {"phi10": Path(args.input_phi10), "phi30": Path(args.input_phi30)}
    fig, ax = plt.subplots(figsize=(args.fig_width, args.fig_height))

    max_iter = None
    for label, key, sm, color, ls in CURVES:
        ttts = load(inputs[key], args.seed, sm)
        x = list(range(len(ttts)))
        y = [math.log10(max(t / 3600.0, 1e-12)) for t in ttts]
        # When LaTeX is off, matplotlib's mathtext does not interpret \% so
        # strip the backslash; with LaTeX on, leave it as-is.
        legend_label = label if args.usetex else label.replace(r"\%", "%")
        ax.plot(
            x, y,
            color=color, marker="o", linestyle=ls,
            linewidth=args.linewidth, markersize=args.markersize,
            markerfacecolor="none", markeredgecolor=color,
            markeredgewidth=args.markeredgewidth, alpha=0.95,
            label=legend_label,
            zorder=3,
        )
        max_iter = max(max_iter or 0, len(ttts) - 1)

    ax.set_xlim(0, max_iter)
    ax.set_xticks(list(range(0, max_iter + 1)))
    ax.xaxis.set_major_formatter(
        mticker.FuncFormatter(lambda v, _: "initial" if v == 0 else f"{int(v)}")
    )
    ax.tick_params(axis="both", labelsize=9, pad=2)
    ax.set_xlabel("Iteration number", fontsize=10)
    ax.set_ylabel("Log. Total travel time (h)", fontsize=10)
    ax.legend(loc="upper right", fontsize=8, frameon=False,
              handlelength=1.6, handletextpad=0.4,
              labelspacing=0.2, borderaxespad=0.3)
    ax.grid(False)
    ax.ticklabel_format(axis="y", style="plain", useOffset=False)

    out_pdf = Path(args.output)
    out_pdf.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout(pad=0.4)
    fig.savefig(out_pdf, dpi=300, bbox_inches="tight")
    fig.savefig(out_pdf.with_suffix(".png"), dpi=300, bbox_inches="tight")
    plt.close(fig)
    print(f"saved {out_pdf}")
    print(f"saved {out_pdf.with_suffix('.png')}")


if __name__ == "__main__":
    main()
