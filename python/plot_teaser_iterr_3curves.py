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
from matplotlib.legend_handler import HandlerTuple
from matplotlib.lines import Line2D
from matplotlib.offsetbox import (
    AnchoredOffsetbox, DrawingArea, HPacker, TextArea, VPacker,
)


def _make_swatch(color, linestyle, linewidth, markersize, markeredgewidth,
                 width_px=40, height_px=18):
    """Return a DrawingArea containing a short line segment with two markers,
    matching the linestyle/markers of the corresponding curve."""
    area = DrawingArea(width_px, height_px, 0, 0)
    y = height_px / 2
    line = Line2D(
        [1, width_px - 1], [y, y],
        color=color, linestyle=linestyle, linewidth=linewidth,
        marker="o", markersize=markersize,
        markerfacecolor="none", markeredgecolor=color,
        markeredgewidth=markeredgewidth,
        markevery=[0, 1],
    )
    area.add_artist(line)
    return area


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
    ap.add_argument("--xtick-every", type=int, default=1,
                    help="Label every N-th iteration on the x-axis (1 = every tick).")
    ap.add_argument("--max-iter", type=int, default=None,
                    help="Truncate each trace to the first N iterations (after iter 0).")
    ap.add_argument("--font-scale", type=float, default=1.0,
                    help="Multiply tick-label and axis-label font sizes by this factor.")
    ap.add_argument("--legend-font-scale", type=float, default=None,
                    help="Override font scale for the legend only (defaults to --font-scale).")
    ap.add_argument("--xtick-rotation", type=float, default=0.0,
                    help="Rotate x-tick labels by N degrees (use 45 or 90 if labels collide).")
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
    handles_by_key = {}
    for label, key, sm, color, ls in CURVES:
        ttts = load(inputs[key], args.seed, sm)
        if args.max_iter is not None:
            ttts = ttts[: args.max_iter + 1]
        x = list(range(len(ttts)))
        y = [math.log10(max(t / 3600.0, 1e-12)) for t in ttts]
        # When LaTeX is off, matplotlib's mathtext does not interpret \% so
        # strip the backslash; with LaTeX on, leave it as-is.
        legend_label = label if args.usetex else label.replace(r"\%", "%")
        line, = ax.plot(
            x, y,
            color=color, marker="o", linestyle=ls,
            linewidth=args.linewidth, markersize=args.markersize,
            markerfacecolor="none", markeredgecolor=color,
            markeredgewidth=args.markeredgewidth, alpha=0.95,
            label=legend_label,
            zorder=3,
        )
        handles_by_key[(key, sm)] = line
        max_iter = max(max_iter or 0, len(ttts) - 1)

    ax.set_xlim(0, max_iter)
    tick_positions = [i for i in range(0, max_iter + 1)
                      if i == 0 or i == max_iter or i % args.xtick_every == 0]
    ax.set_xticks(tick_positions)
    ax.xaxis.set_major_formatter(
        mticker.FuncFormatter(lambda v, _: "initial" if v == 0 else f"{int(v)}")
    )
    fs = args.font_scale
    ax.tick_params(axis="both", labelsize=9 * fs, pad=2)
    if args.xtick_rotation:
        for lbl in ax.get_xticklabels():
            lbl.set_rotation(args.xtick_rotation)
            lbl.set_horizontalalignment("right" if args.xtick_rotation > 0 else "center")
    ax.set_xlabel("Iteration number", fontsize=10 * fs)
    ax.set_ylabel("Log. TTT (h)", fontsize=10 * fs)

    # Custom inline-swatch legend placed inside the axes (upper right).
    # The legend has its own font scale (defaults to --font-scale) so it can be
    # made smaller than the tick/axis labels without losing room for them.
    lfs = args.legend_font_scale if args.legend_font_scale is not None else fs
    legend_fontsize = 8 * lfs
    text_props = {"size": legend_fontsize}
    # Match the line/marker geometry of the actual curves but scale the swatch
    # geometry and marker size with the legend font (not the main markersize).
    legend_marker_size = args.markersize * lfs / fs
    legend_linewidth   = args.linewidth   * lfs / fs
    legend_markeredgewidth = args.markeredgewidth * lfs / fs
    sw_kwargs = dict(
        linewidth=legend_linewidth,
        markersize=legend_marker_size,
        markeredgewidth=legend_markeredgewidth,
        # Long enough for the dashed pattern to be unambiguous against solid.
        width_px=int(round(max(34, legend_marker_size * 4))),
        height_px=int(round(max(12, legend_marker_size * 2))),
    )
    purple = "#8F6CCF"
    blue   = "#64BEE8"

    row_pad = 2
    row1 = HPacker(
        children=[
            TextArea("Random:", textprops=text_props),
            _make_swatch(purple, "--", **sw_kwargs),
            TextArea(r" $\phi$=10%, ", textprops=text_props),
            _make_swatch(purple, "-",  **sw_kwargs),
            TextArea(r" $\phi$=30%", textprops=text_props),
        ],
        align="center", pad=0, sep=row_pad,
    )
    row2 = HPacker(
        children=[
            TextArea("Most-delayed:", textprops=text_props),
            _make_swatch(blue, "--", **sw_kwargs),
            TextArea(r" $\phi$=10%", textprops=text_props),
        ],
        align="center", pad=0, sep=row_pad,
    )
    block = VPacker(children=[row1, row2], align="left", pad=0, sep=4)
    anchored = AnchoredOffsetbox(
        loc="upper right", child=block,
        frameon=False, pad=0.2, borderpad=0.5,
    )
    ax.add_artist(anchored)
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
