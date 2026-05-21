import argparse
import math
import os
from typing import Optional

os.makedirs("tmp/matplotlib", exist_ok=True)
os.environ.setdefault("MPLCONFIGDIR", os.path.abspath("tmp/matplotlib"))

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import pandas as pd
from matplotlib.lines import Line2D
from matplotlib.patches import Patch


HOP_LABELS = {
    5: "Short Queries",
    10: "Medium Queries",
    40: "Long Queries",
}

SELECTION_STYLE = {
    "Random": "#8F6CCF",
    "Latency-based": "#64BEE8",
    "TDG-guided": "#E76F51",
}

REROUTE_STYLE = {
    "Normal TD-Dijkstra": {"marker": "o", "linestyle": "--", "linewidth": 1},
    "TDG-impact reroute": {"marker": "^", "linestyle": "--", "linewidth": 1},
}


def load_method(
    path: Optional[str],
    selection_label: str,
    reroute_label: str,
    selection_method: str,
    reroute_method: str,
    selection_fraction: Optional[int] = None,
    gamma: Optional[int] = None,
    removal_mode: Optional[str] = None,
) -> pd.DataFrame:
    if not path:
        return pd.DataFrame()

    df = pd.read_csv(path)
    mask = (
        (df["selection_method"] == selection_method)
        & (df["reroute_method"] == reroute_method)
    )
    if selection_fraction is not None:
        mask &= df["selection_fraction"] == selection_fraction
    if gamma is not None:
        mask &= df["gamma"] == gamma
    if removal_mode is not None:
        if "removal_mode" not in df.columns:
            raise ValueError(f"Cannot filter removal_mode for {path}; column is missing")
        mask &= df["removal_mode"] == removal_mode

    df = df[mask].copy()
    if df.empty:
        raise ValueError(f"No rows matched expected filters for {path}")

    df["selection_label"] = selection_label
    df["reroute_label"] = reroute_label
    return df


def build_curve(group: pd.DataFrame) -> pd.DataFrame:
    group = group.sort_values("iteration").reset_index(drop=True)
    if group.empty:
        return pd.DataFrame(columns=["plot_iteration", "ttt", "selected_fraction"])

    rows = [
        {
            "plot_iteration": 0,
            "ttt": group.loc[0, "total_before"],
            "selected_fraction": math.nan,
        }
    ]
    for _, row in group.iterrows():
        rows.append(
            {
                "plot_iteration": int(row["iteration"]) + 1,
                "ttt": row["total_after"],
                "selected_fraction": row.get("selected_fraction", math.nan),
            }
        )
    return pd.DataFrame(rows)


def build_plot_dataframe(method_frames: list[pd.DataFrame]) -> pd.DataFrame:
    rows = []
    combined = pd.concat(
        [frame for frame in method_frames if not frame.empty],
        ignore_index=True,
    )
    if combined.empty:
        raise ValueError("No method data was provided.")

    group_keys = [
        "hop",
        "rep",
        "dataset",
        "seed",
        "selection_label",
        "reroute_label",
    ]
    for key, group in combined.groupby(group_keys):
        key_dict = dict(zip(group_keys, key))
        curve = build_curve(group)
        for _, row in curve.iterrows():
            item = dict(key_dict)
            item["plot_iteration"] = int(row["plot_iteration"])
            item["ttt"] = float(row["ttt"])
            item["selected_fraction"] = float(row["selected_fraction"])
            rows.append(item)

    curve_df = pd.DataFrame(rows)
    return (
        curve_df.groupby(
            ["hop", "rep", "selection_label", "reroute_label", "plot_iteration"],
            as_index=False,
        )
        .agg(
            ttt=("ttt", "mean"),
            selected_fraction=("selected_fraction", "mean"),
        )
    )


def format_panel_title(index: int, rep: int, hop: int) -> str:
    letter = chr(ord("a") + index)
    hop_label = HOP_LABELS.get(hop, f"Hop {hop}")
    return f"({letter}) Rep #: {rep}, {hop_label}"


def plot_component_ablation(
    plot_df: pd.DataFrame,
    output: str,
    show_selection_fraction: bool = False,
    selection_fraction_label: str = "TDG-guided",
    selection_fraction_reroute: str = "Normal TD-Dijkstra",
    baseline_fraction: Optional[int] = None,
) -> None:
    combos = (
        plot_df[["rep", "hop"]]
        .drop_duplicates()
        .sort_values(["rep", "hop"])
        .values
        .tolist()
    )

    n_cols = 3
    n_rows = math.ceil(len(combos) / n_cols)
    fig, axes = plt.subplots(
        n_rows,
        n_cols,
        figsize=(13.8, 9.1),
        sharex=True,
        squeeze=False,
    )

    for panel_index, (rep, hop) in enumerate(combos):
        row = panel_index // n_cols
        col = panel_index % n_cols
        ax = axes[row][col]
        sub = plot_df[(plot_df["rep"] == rep) & (plot_df["hop"] == hop)]

        ax2 = None
        if show_selection_fraction:
            fraction_curve = sub[
                (sub["selection_label"] == selection_fraction_label)
                & (sub["reroute_label"] == selection_fraction_reroute)
                & (sub["plot_iteration"] > 0)
            ].sort_values("plot_iteration")
            fraction_curve = fraction_curve.dropna(subset=["selected_fraction"])
            if not fraction_curve.empty:
                ax2 = ax.twinx()
                ax2.bar(
                    fraction_curve["plot_iteration"],
                    fraction_curve["selected_fraction"] * 100.0,
                    width=0.62,
                    color="#D9D9D9",
                    alpha=0.45,
                    edgecolor="none",
                    zorder=0,
                )
                if baseline_fraction is not None:
                    ax2.axhline(
                        baseline_fraction,
                        color="#4F4F4F",
                        linestyle=":",
                        linewidth=1.25,
                        alpha=0.75,
                        zorder=1,
                    )
                max_fraction = (
                    plot_df[
                        (plot_df["selection_label"] == selection_fraction_label)
                        & (plot_df["reroute_label"] == selection_fraction_reroute)
                    ]["selected_fraction"]
                    .dropna()
                    .max()
                )
                top = max(10.0, math.ceil((max_fraction * 100.0) / 10.0) * 10.0)
                ax2.set_ylim(0, min(100.0, top))
                ax2.grid(False)
                ax2.tick_params(axis="y", labelsize=9, pad=2, colors="#666666")
                if col == n_cols - 1:
                    ax2.set_ylabel(
                        "TDG selected queries (%)",
                        fontsize=11,
                        color="#666666",
                    )

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

        ax.set_title(format_panel_title(panel_index, rep, hop), fontsize=12, pad=5)
        ax.set_xlim(0, int(plot_df["plot_iteration"].max()))
        ax.set_xticks(range(0, int(plot_df["plot_iteration"].max()) + 1, 2))
        ax.grid(False)
        ax.tick_params(axis="both", labelsize=10, pad=2)

        if col == 0:
            ax.set_ylabel("Log. Total travel time (h)", fontsize=12)
        if row == n_rows - 1:
            ax.set_xlabel("Iteration number", fontsize=12)

    for panel_index in range(len(combos), n_rows * n_cols):
        axes[panel_index // n_cols][panel_index % n_cols].set_visible(False)

    selection_handles = [
        Patch(facecolor=color, edgecolor="none", label=label)
        for label, color in SELECTION_STYLE.items()
    ]
    reroute_handles = [
        Line2D(
            [0],
            [0],
            color="black",
            marker=style["marker"],
            linestyle="None",
            markersize=9,
            markerfacecolor="none",
            markeredgecolor="black",
            markeredgewidth=1.9,
            label=label,
        )
        for label, style in REROUTE_STYLE.items()
    ]

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

    os.makedirs(os.path.dirname(output), exist_ok=True)
    fig.tight_layout(rect=(0.035, 0.035, 0.995, 0.915), h_pad=1.5, w_pad=1.4)
    fig.savefig(output, dpi=300, bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--random-normal")
    parser.add_argument("--delayed-normal", required=True)
    parser.add_argument("--delayed-tdg-reroute")
    parser.add_argument("--tdg-excess-normal", required=True)
    parser.add_argument("--tdg-excess-full")
    parser.add_argument("--tdg-selection-method", default="tdg_excess")
    parser.add_argument("--tdg-removal-mode")
    parser.add_argument("--baseline-fraction", type=int, default=10)
    parser.add_argument("--tdg-gamma", type=int, default=25)
    parser.add_argument("--show-selection-fraction", action="store_true")
    parser.add_argument("--selection-fraction-label", default="TDG-guided")
    parser.add_argument("--selection-fraction-reroute", default="Normal TD-Dijkstra")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    frames = [
        load_method(
            args.random_normal,
            "Random",
            "Normal TD-Dijkstra",
            "random",
            "normal_td_dijkstra",
            selection_fraction=args.baseline_fraction,
        ),
        load_method(
            args.delayed_normal,
            "Latency-based",
            "Normal TD-Dijkstra",
            "most_delayed",
            "normal_td_dijkstra",
            selection_fraction=args.baseline_fraction,
        ),
        load_method(
            args.delayed_tdg_reroute,
            "Latency-based",
            "TDG-impact reroute",
            "most_delayed",
            "tdg_impact_reroute",
            selection_fraction=args.baseline_fraction,
        ),
        load_method(
            args.tdg_excess_normal,
            "TDG-guided",
            "Normal TD-Dijkstra",
            args.tdg_selection_method,
            "normal_td_dijkstra",
            gamma=args.tdg_gamma,
            removal_mode=args.tdg_removal_mode,
        ),
        load_method(
            args.tdg_excess_full,
            "TDG-guided",
            "TDG-impact reroute",
            args.tdg_selection_method,
            "tdg_impact_reroute",
            gamma=args.tdg_gamma,
            removal_mode=args.tdg_removal_mode,
        ),
    ]

    plot_df = build_plot_dataframe(frames)
    plot_component_ablation(
        plot_df,
        args.output,
        show_selection_fraction=args.show_selection_fraction,
        selection_fraction_label=args.selection_fraction_label,
        selection_fraction_reroute=args.selection_fraction_reroute,
        baseline_fraction=args.baseline_fraction,
    )
    print(f"Saved to {args.output}")


if __name__ == "__main__":
    main()
