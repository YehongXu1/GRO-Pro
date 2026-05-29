#!/usr/bin/env python3
"""Build --plot-data CSVs for the BJ component-ablation 3x3 figure from raw/ sweeps.

Produces two pre-aggregated CSVs consumable by plot_gro_component_ablation.py via
its --plot-data flag (the plot script is left unmodified):

  1. FIXED  (fair): TDG-guided curves at a single gamma (+impact_weight). A
     defensible single-setting result.
  2. DIAGNOSTIC (best-param-by-iteration lower envelope): per
     (dataset, hop, rep, seed, iteration) the TDG-guided rows keep the choice
     with the lowest total_after across the gamma (and impact_weight) sweep.
     This is a diagnostic lower envelope, NOT a fair final result.

Both apply the per-rep baseline-selection-fraction convention to the random and
latency-based baselines: Rep1 -> 1%, Rep2 -> 10%, Rep4 -> 30%. The aggregation /
curve logic is reused verbatim from plot_gro_component_ablation.build_plot_dataframe.
"""

from __future__ import annotations

import argparse
import math
import os
import re

import pandas as pd

# Importing the (frozen) plot module first runs its matplotlib backend setup
# (Agg + MPLCONFIGDIR). We reuse its curve/aggregation logic and style constants
# without modifying it; the only rendering difference here is the x-tick labels.
from plot_gro_component_ablation import (
    HOP_LABELS,
    REROUTE_STYLE,
    SELECTION_STYLE,
    build_plot_dataframe,
    selection_fraction_axis_top,
)

import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
from matplotlib.lines import Line2D
from matplotlib.patches import Patch

# Display name for the most_delayed selection method (frozen script calls it
# "Latency-based"). Rebind a local style dict with the renamed key so the legend
# and the curve labels stay in sync; the frozen script's dict is untouched.
LATENCY_LABEL = "Most-delayed"
SELECTION_STYLE = {
    (LATENCY_LABEL if key == "Latency-based" else key): color
    for key, color in SELECTION_STYLE.items()
}

DATASET_PATTERN = re.compile(r"^Hop(\d+)Rep(\d+)-(\d+)$")
REP_FRACTION = {1: 1, 2: 10, 4: 30}
# How the raw rep value is displayed in panel titles.
REP_DISPLAY = {1: 0, 2: 1, 4: 3}
ORACLE_KEY = ["dataset", "hop", "rep", "seed", "iteration"]
ORACLE_TIEBREAK = ["total_after", "selected_fraction", "gamma", "impact_weight"]


def load_clean(path: str) -> pd.DataFrame:
    """Read a raw CSV and drop rows whose dataset is not a valid HopXRepY-Z id."""
    df = pd.read_csv(path, dtype={"dataset": str})
    valid = df["dataset"].fillna("").str.match(DATASET_PATTERN)
    dropped = int((~valid).sum())
    if dropped:
        bad_ids = df.loc[~valid, "run_id"].tolist()
        print(f"  [{os.path.basename(path)}] dropped {dropped} malformed row(s): run_id={bad_ids}")
    df = df[valid].copy()
    for col in ("gamma", "impact_weight", "selection_fraction"):
        df[col] = pd.to_numeric(df[col], errors="coerce")
    return df


def label(df: pd.DataFrame, selection_label: str, reroute_label: str) -> pd.DataFrame:
    df = df.copy()
    df["selection_label"] = selection_label
    df["reroute_label"] = reroute_label
    return df


def baseline_per_rep(df: pd.DataFrame, reroute_method: str, impact_weight=None) -> pd.DataFrame:
    """Select baseline rows applying the per-rep selection fraction."""
    parts = []
    for rep, frac in REP_FRACTION.items():
        mask = (df["rep"] == rep) & (df["reroute_method"] == reroute_method) & (df["selection_fraction"] == frac)
        if impact_weight is not None:
            mask &= df["impact_weight"] == impact_weight
        parts.append(df[mask])
    return pd.concat(parts, ignore_index=True)


def tdg_best_param(df: pd.DataFrame, reroute_method: str) -> pd.DataFrame:
    """Per (dataset, iteration) keep the lowest-total_after row across the sweep."""
    cand = df[df["reroute_method"] == reroute_method].copy()
    cand = cand.sort_values(ORACLE_KEY + ORACLE_TIEBREAK)
    return cand.groupby(ORACLE_KEY, as_index=False).first()


def format_panel_title_labeled(index: int, rep, hop) -> str:
    """Panel title with integer, remapped rep number (1->0, 2->1, 4->3)."""
    letter = chr(ord("a") + index)
    hop_label = HOP_LABELS.get(hop, f"Hop {hop}")
    rep_int = int(round(rep))
    rep_shown = REP_DISPLAY.get(rep_int, rep_int)
    return f"({letter}) Rep #: {rep_shown}, {hop_label}"


def plot_component_ablation_labeled(
    plot_df: pd.DataFrame,
    output: str,
    show_selection_fraction: bool = False,
    selection_fraction_label: str = "TDG-guided",
    selection_fraction_reroute: str = "Normal TD-Dijkstra",
    baseline_fraction=None,
    baseline_fraction_by_panel_from_data: bool = False,
    show_reroute_legend: bool = True,
    fig_width: float = 13.8,
    row_height: float = 2.35,
    h_pad: float = 0.55,
    w_pad: float = 1.2,
) -> None:
    """Faithful copy of plot_gro_component_ablation.plot_component_ablation, with
    the only change being x-tick labels: position 0 -> "initial" and the final
    plotted iteration -> "final". The frozen plot script is not touched."""
    combos = (
        plot_df[["rep", "hop"]]
        .drop_duplicates()
        .sort_values(["rep", "hop"])
        .values
        .tolist()
    )

    n_cols = 3
    n_rows = math.ceil(len(combos) / n_cols)
    fig_height = max(3.0, row_height * n_rows + 0.85)
    fig, axes = plt.subplots(
        n_rows, n_cols, figsize=(fig_width, fig_height), sharex=True, squeeze=False
    )
    has_selection_fraction_axis = False
    max_iter = int(plot_df["plot_iteration"].max())

    for panel_index, (rep, hop) in enumerate(combos):
        row = panel_index // n_cols
        col = panel_index % n_cols
        ax = axes[row][col]
        sub = plot_df[(plot_df["rep"] == rep) & (plot_df["hop"] == hop)]

        if show_selection_fraction:
            fraction_curve = sub[
                (sub["selection_label"] == selection_fraction_label)
                & (sub["reroute_label"] == selection_fraction_reroute)
                & (sub["plot_iteration"] > 0)
            ].sort_values("plot_iteration")
            fraction_curve = fraction_curve.dropna(subset=["selected_fraction"])
            if not fraction_curve.empty:
                has_selection_fraction_axis = True
                ax2 = ax.twinx()
                panel_baseline_fraction = baseline_fraction
                if baseline_fraction_by_panel_from_data:
                    baseline_curve = sub[
                        (sub["selection_label"] == "Random")
                        & (sub["reroute_label"] == "Normal TD-Dijkstra")
                        & (sub["plot_iteration"] > 0)
                    ]["selected_fraction"].dropna()
                    if not baseline_curve.empty:
                        panel_baseline_fraction = int(
                            round(float(baseline_curve.mean()) * 100.0)
                        )
                ax2.bar(
                    fraction_curve["plot_iteration"],
                    fraction_curve["selected_fraction"] * 100.0,
                    width=0.62,
                    color="#D9D9D9",
                    alpha=0.45,
                    edgecolor="none",
                    zorder=0,
                )
                if panel_baseline_fraction is not None:
                    ax2.axhline(
                        panel_baseline_fraction,
                        color="#4F4F4F",
                        linestyle=":",
                        linewidth=1.25,
                        alpha=0.75,
                        zorder=1,
                    )
                ax2.set_ylim(
                    0,
                    selection_fraction_axis_top(fraction_curve, panel_baseline_fraction),
                )
                ax2.grid(False)
                ax2.tick_params(axis="y", labelsize=9, pad=2, colors="#666666")
                ax.set_zorder(ax2.get_zorder() + 1)
                ax.patch.set_visible(False)

        for selection_label in SELECTION_STYLE:
            for reroute_label in REROUTE_STYLE:
                curve = sub[
                    (sub["selection_label"] == selection_label)
                    & (sub["reroute_label"] == reroute_label)
                ].sort_values("plot_iteration")
                if curve.empty:
                    continue

                style = REROUTE_STYLE[reroute_label]
                y_values = (curve["ttt"] / 3600.0).clip(lower=1e-12)
                ax.plot(
                    curve["plot_iteration"],
                    y_values.apply(math.log10),
                    color=SELECTION_STYLE[selection_label],
                    marker=style["marker"],
                    linestyle=style["linestyle"],
                    linewidth=style["linewidth"],
                    markersize=8,
                    markerfacecolor="none",
                    markeredgecolor=SELECTION_STYLE[selection_label],
                    markeredgewidth=1.75,
                    alpha=0.95,
                    zorder=3,
                )

        ax.set_title(format_panel_title_labeled(panel_index, rep, hop), fontsize=12, pad=5)
        ax.set_xlim(0, max_iter)
        ticks = list(range(0, max_iter + 1))
        if ticks[-1] != max_iter:
            ticks.append(max_iter)
        ax.set_xticks(ticks)
        # x=0 is the initial pre-reroute TTT; x=N is the TTT entering iteration N
        # (= outcome of reroute N-1). The last point (x=max_iter) is the outcome of
        # the final reroute, so it is just its iteration number, not a "final".
        ax.xaxis.set_major_formatter(
            mticker.FuncFormatter(
                lambda value, _pos: "initial" if value == 0 else f"{int(value)}"
            )
        )
        ax.grid(False)
        ax.ticklabel_format(axis="y", style="plain", useOffset=False)
        if rep == 1:
            ax.yaxis.set_major_formatter(mticker.FormatStrFormatter("%.3f"))
        ax.tick_params(axis="both", labelsize=10, pad=2)

        if row == n_rows - 1:
            ax.set_xlabel("Iteration number", fontsize=12)

    for panel_index in range(len(combos), n_rows * n_cols):
        axes[panel_index // n_cols][panel_index % n_cols].set_visible(False)

    selection_handles = [
        Patch(facecolor=color, edgecolor="none", label=label_text)
        for label_text, color in SELECTION_STYLE.items()
    ]
    reroute_handles = [
        Line2D(
            [0], [0],
            color="black",
            marker=style["marker"],
            linestyle="None",
            markersize=9,
            markerfacecolor="none",
            markeredgecolor="black",
            markeredgewidth=1.9,
            label=label_text,
        )
        for label_text, style in REROUTE_STYLE.items()
    ]

    if show_reroute_legend:
        section_handles = [
            Line2D([], [], linestyle="None", marker=None, color="none"),
            *selection_handles,
            Line2D([], [], linestyle="None", marker=None, color="none"),
            *reroute_handles,
        ]
        section_labels = [
            "Selection method:",
            *[handle.get_label() for handle in selection_handles],
            "Reroute method:",
            *[handle.get_label() for handle in reroute_handles],
        ]
    else:
        section_handles = [
            Line2D([], [], linestyle="None", marker=None, color="none"),
            *selection_handles,
        ]
        section_labels = [
            "Selection method:",
            *[handle.get_label() for handle in selection_handles],
        ]
    legend = fig.legend(
        section_handles,
        section_labels,
        loc="upper center",
        bbox_to_anchor=(0.5, 0.965),
        ncol=len(section_labels),
        frameon=False,
        fontsize=11,
        columnspacing=1.25,
        handlelength=2.2,
        handleheight=0.85,
        handletextpad=0.55,
    )
    for text in legend.get_texts():
        if text.get_text().endswith(":"):
            text.set_weight("semibold")

    fig.text(
        0.052, 0.48, "Log. Total travel time (h)",
        rotation=90, va="center", ha="center", fontsize=15,
    )
    if has_selection_fraction_axis:
        fig.text(
            0.958, 0.48, "TDG selected queries (%)",
            rotation=270, va="center", ha="center", fontsize=15, color="#666666",
        )

    output_dir = os.path.dirname(output)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)
    fig.tight_layout(rect=(0.075, 0.035, 0.94, 0.91), h_pad=h_pad, w_pad=w_pad)
    fig.savefig(output, dpi=300, bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw-dir", required=True)
    parser.add_argument("--out-fixed", required=True)
    parser.add_argument("--out-diagnostic", required=True)
    parser.add_argument("--fixed-gamma", type=float, default=50)
    parser.add_argument("--fixed-impact-weight", type=float, default=30)
    parser.add_argument("--latency-impact-weight", type=float, default=30)
    parser.add_argument("--fig-fixed", help="If set, render the fixed-param figure here.")
    parser.add_argument("--fig-diagnostic", help="If set, render the diagnostic figure here.")
    args = parser.parse_args()

    raw = args.raw_dir
    rand = load_clean(os.path.join(raw, "gro_ablation_selection_random_reroute_tdg__normal.csv"))
    dely = load_clean(os.path.join(raw, "gro_ablation_selection_most_delayed_reroute_tdg__normal.csv"))
    exn = load_clean(os.path.join(raw, "gro_ablation_selection_tdg_excess_reroute_normal.csv"))
    ext = load_clean(os.path.join(raw, "gro_ablation_selection_tdg_excess_reroute_tdg.csv"))

    g, w, lw = args.fixed_gamma, args.fixed_impact_weight, args.latency_impact_weight

    # Baselines + latency are identical in both figures (oracle only touches TDG-guided).
    shared = [
        label(baseline_per_rep(rand, "normal_td_dijkstra"), "Random", "Normal TD-Dijkstra"),
        label(baseline_per_rep(dely, "normal_td_dijkstra"), LATENCY_LABEL, "Normal TD-Dijkstra"),
        label(baseline_per_rep(dely, "tdg_impact_reroute", impact_weight=lw),
              LATENCY_LABEL, "TDG-impact reroute"),
    ]

    fixed_frames = shared + [
        label(exn[(exn["reroute_method"] == "normal_td_dijkstra") & (exn["gamma"] == g)],
              "TDG-guided", "Normal TD-Dijkstra"),
        label(ext[(ext["reroute_method"] == "tdg_impact_reroute") & (ext["gamma"] == g) & (ext["impact_weight"] == w)],
              "TDG-guided", "TDG-impact reroute"),
    ]

    diag_frames = shared + [
        label(tdg_best_param(exn, "normal_td_dijkstra"), "TDG-guided", "Normal TD-Dijkstra"),
        label(tdg_best_param(ext, "tdg_impact_reroute"), "TDG-guided", "TDG-impact reroute"),
    ]

    for tag, frames, out, fig_out in (
        (f"FIXED (gamma={g:g}, impact_weight={w:g})", fixed_frames, args.out_fixed, args.fig_fixed),
        ("DIAGNOSTIC (best-param-by-iteration)", diag_frames, args.out_diagnostic, args.fig_diagnostic),
    ):
        plot_df = build_plot_dataframe(frames)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        plot_df.to_csv(out, index=False)
        panels = plot_df[["rep", "hop"]].drop_duplicates().sort_values(["rep", "hop"])
        print(f"\n{tag} -> {out}  rows={len(plot_df)}")
        for rep, hop in panels.itertuples(index=False):
            sub = plot_df[(plot_df["rep"] == rep) & (plot_df["hop"] == hop)]
            methods = sub[["selection_label", "reroute_label"]].drop_duplicates()
            print(f"   panel rep={rep} hop={hop}: {len(methods)}/5 method curves")

        if fig_out:
            plot_component_ablation_labeled(
                plot_df,
                fig_out,
                show_selection_fraction=True,
                baseline_fraction_by_panel_from_data=True,
            )
            print(f"   rendered figure -> {fig_out}")


if __name__ == "__main__":
    main()
