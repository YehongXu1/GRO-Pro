#!/usr/bin/env python3
"""Plot the 4-parameter sensitivity sweep as a 1 x 4 figure.

Each panel:
- x-axis: parameter value (log for kappa, linear for the rest)
- left y-axis (log): best TTT across the first 4 iterations
- right y-axis (linear): runtime_inf cumulative up to the best-TTT iteration,
  excluding traffic evaluation and assuming infinite-thread parallelism in
  reroute (i.e. method_total_sec - evaluate_before - evaluate_after
  - reroute_sec + reroute_critical_sec, summed iter-by-iter through the iter
  that achieves the best TTT).

Reads CSVs from
  python/results/experiments/exp4_parameter_sensitivity/
    bj_real_scalability_peak1h_rep1_10k/raw/

Writes:
  python/results/experiments/exp4_parameter_sensitivity/
    bj_real_scalability_peak1h_rep1_10k/plots/sensitivity_4panel.{png,pdf}
"""
import csv
import glob
import statistics
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter, FixedLocator

ROOT = Path(__file__).resolve().parent.parent
RAW = (
    ROOT
    / "python/results/experiments/exp4_parameter_sensitivity"
    / "bj_real_scalability_peak1h_rep1_10k/raw"
)
OUT_DIR = RAW.parent / "plots"
OUT_DIR.mkdir(parents=True, exist_ok=True)

MAX_ITER = 3  # inclusive, so iter 0..3 = first 4 iterations.

# Palette taken from python/build_scalability_3iter_6panel.py.
TTT_COLOR = "#64BEE8"  # blue
RT_COLOR = "#8F6CCF"   # purple

PARAMS = [
    {
        "key": "epsilon",
        "panel": "(a)",
        "label": r"$\varepsilon$ (surge threshold, % of capacity)",
        "xlog": False,
    },
    {
        "key": "conflict",
        "panel": "(b)",
        "label": r"$\kappa$ (batch conflict)",
        "xlog": True,
    },
    {
        "key": "gamma",
        "panel": "(c)",
        "label": r"$\gamma$ (QS relief target, %)",
        "xlog": False,
    },
    {
        "key": "impact",
        "panel": "(d)",
        "label": r"$\psi$ (QR impact weight, %)",
        "xlog": False,
    },
]


def runtime_inf_per_iter(row):
    return (
        float(row["method_total_sec"])
        - float(row["evaluate_before_sec"])
        - float(row["evaluate_after_sec"])
        - float(row["reroute_sec"])
        + float(row["reroute_critical_sec"])
    )


def aggregate(param_key):
    """Return parallel lists of (value, mean TTT reduction %, mean total
    runtime over the first 4 iterations).

    TTT reduction % = (initial_ttt - best_ttt_in_4_iter) / initial_ttt * 100.
    Runtime = sum of runtime_inf_per_iter over iter 0..MAX_ITER.
    """
    files = sorted(glob.glob(str(RAW / f"gro_sensitivity_{param_key}_*_capacity2_cap10e8.csv")))
    values = []
    reduction_pct = []
    runtime_4iter = []
    for f in files:
        value = int(Path(f).stem.split("_")[-3])
        with open(f) as fh:
            recs = list(csv.DictReader(fh))
        by_ds = {}
        for r in recs:
            by_ds.setdefault(r["dataset"], []).append(r)
        ds_red_pct = []
        ds_rt_4 = []
        for ds, rows in by_ds.items():
            rows.sort(key=lambda r: int(r["iteration"]))
            rows = [r for r in rows if int(r["iteration"]) <= MAX_ITER]
            if not rows:
                continue
            init_ttt = float(rows[0]["total_before"])
            best_tt = min(float(r["total_after"]) for r in rows)
            cum_rt = sum(runtime_inf_per_iter(r) for r in rows)
            ds_red_pct.append((init_ttt - best_tt) / init_ttt * 100.0)
            ds_rt_4.append(cum_rt)
        values.append(value)
        reduction_pct.append(statistics.mean(ds_red_pct))
        runtime_4iter.append(statistics.mean(ds_rt_4))
    order = sorted(range(len(values)), key=lambda i: values[i])
    values = [values[i] for i in order]
    reduction_pct = [reduction_pct[i] for i in order]
    runtime_4iter = [runtime_4iter[i] for i in order]
    return values, reduction_pct, runtime_4iter


def millions(x, pos):
    if x >= 1e6:
        return f"{x/1e6:g}M"
    if x >= 1e3:
        return f"{x/1e3:g}k"
    return f"{x:g}"


def plot():
    fig, axes = plt.subplots(1, 4, figsize=(16, 3.0))

    for ax_ttt, spec in zip(axes, PARAMS):
        values, red_pct, rt = aggregate(spec["key"])

        ax_ttt.plot(
            values,
            red_pct,
            marker="o",
            color=TTT_COLOR,
            linewidth=1.8,
            markersize=6,
        )
        ax_ttt.set_title(
            f"{spec['panel']} {spec['label']}",
            fontsize=13,
            fontweight="bold",
            pad=6,
        )
        ax_ttt.set_ylabel("TTT reduction (%)", color=TTT_COLOR, fontsize=13)
        ax_ttt.tick_params(axis="y", labelcolor=TTT_COLOR, labelsize=11)
        if spec["xlog"]:
            ax_ttt.set_xscale("log")
        ax_ttt.set_xticks(values)
        ax_ttt.set_xticklabels([str(v) for v in values], fontsize=11)
        # Suppress intermediate log-minor x-ticks so only the swept values show.
        ax_ttt.xaxis.set_minor_locator(plt.NullLocator())

        ax_rt = ax_ttt.twinx()
        ax_rt.plot(
            values,
            rt,
            marker="s",
            color=RT_COLOR,
            linewidth=1.8,
            markersize=6,
            linestyle="--",
        )
        ax_rt.set_ylabel("Runtime (s)", color=RT_COLOR, fontsize=13)
        ax_rt.tick_params(axis="y", labelcolor=RT_COLOR, labelsize=11)
        ax_rt.set_ylim(bottom=0)

    fig.tight_layout()
    png = OUT_DIR / "sensitivity_4panel.png"
    pdf = OUT_DIR / "sensitivity_4panel.pdf"
    fig.savefig(png, dpi=200, bbox_inches="tight")
    fig.savefig(pdf, bbox_inches="tight")
    print(f"wrote {png}")
    print(f"wrote {pdf}")


if __name__ == "__main__":
    plot()
