import argparse
import math
import os
from typing import Optional

os.makedirs("tmp/matplotlib", exist_ok=True)
os.environ.setdefault("MPLCONFIGDIR", os.path.abspath("tmp/matplotlib"))

import matplotlib.pyplot as plt
import pandas as pd
from matplotlib.ticker import ScalarFormatter


def parse_int_list(text: str) -> list[int]:
    return [
        int(value.strip())
        for value in text.split(",")
        if value.strip()
    ]


def build_curve(group: pd.DataFrame) -> pd.DataFrame:
    group = group.sort_values("iteration").reset_index(drop=True)
    if group.empty:
        return pd.DataFrame(columns=["plot_iteration", "ttt"])

    rows = [{"plot_iteration": 0, "ttt": group.loc[0, "total_before"]}]
    for _, row in group.iterrows():
        rows.append(
            {
                "plot_iteration": int(row["iteration"]) + 1,
                "ttt": row["total_after"],
            }
        )
    return pd.DataFrame(rows)


def curves_from_groups(
    df: pd.DataFrame,
    group_keys: list[str],
    label_builder,
) -> pd.DataFrame:
    rows = []
    for key, group in df.groupby(group_keys, dropna=False):
        key_tuple = key if isinstance(key, tuple) else (key,)
        key_dict = dict(zip(group_keys, key_tuple))
        label = label_builder(key_dict)
        curve = build_curve(group)
        for _, row in curve.iterrows():
            item = dict(key_dict)
            item["method_label"] = label
            item["plot_iteration"] = int(row["plot_iteration"])
            item["ttt"] = row["ttt"]
            rows.append(item)
    return pd.DataFrame(rows)


def curve_values(group: pd.DataFrame) -> list[float]:
    group = group.sort_values("iteration").reset_index(drop=True)
    if group.empty:
        return []
    return [float(group.loc[0, "total_before"])] + [
        float(value) for value in group["total_after"]
    ]


def tdg_volatility_metrics(tdg: pd.DataFrame) -> pd.DataFrame:
    rows = []
    group_cols = ["dataset", "hop", "rep", "gamma"]
    for (dataset, hop, rep, gamma), group in tdg.groupby(group_cols):
        values = curve_values(group)
        if not values:
            continue
        initial = values[0]
        ups = [
            max(0.0, values[index + 1] - values[index])
            for index in range(len(values) - 1)
        ]
        rows.append(
            {
                "dataset": dataset,
                "hop": hop,
                "rep": rep,
                "gamma": gamma,
                "initial_ttt": initial,
                "final_ttt": values[-1],
                "max_up": max(ups) if ups else 0.0,
                "sum_up": sum(ups),
                "num_up_steps": sum(1 for value in ups if value > 0.0),
                "max_up_ratio": (max(ups) / initial) if initial > 0 else 0.0,
                "sum_up_ratio": (sum(ups) / initial) if initial > 0 else 0.0,
                "range_ratio": (
                    (max(values) - min(values)) / initial
                    if initial > 0
                    else 0.0
                ),
            }
        )

    if not rows:
        return pd.DataFrame()

    per_gamma = pd.DataFrame(rows)
    return (
        per_gamma.groupby(["dataset", "hop", "rep"], as_index=False)
        .agg(
            initial_ttt=("initial_ttt", "first"),
            max_up=("max_up", "max"),
            sum_up=("sum_up", "max"),
            num_up_steps=("num_up_steps", "max"),
            max_up_ratio=("max_up_ratio", "max"),
            sum_up_ratio=("sum_up_ratio", "max"),
            range_ratio=("range_ratio", "max"),
        )
        .sort_values(["hop", "rep", "max_up_ratio"], ascending=[True, True, False])
    )


def load_curves(
    delayed_csv: str,
    tdg_anchor_csv: str,
    tdg_excess_csv: Optional[str] = None,
    delayed_tdg_reroute_csv: Optional[str] = None,
    baseline_fractions: Optional[list[int]] = None,
    tdg_gammas: Optional[list[int]] = None,
    max_up_ratio_threshold: Optional[float] = None,
    sum_up_ratio_threshold: Optional[float] = None,
    removed_output: Optional[str] = None,
):
    if baseline_fractions is None:
        baseline_fractions = [10, 30]
    if tdg_gammas is None:
        tdg_gammas = [25, 50]

    delayed = pd.read_csv(delayed_csv)
    delayed = delayed[
        (delayed["selection_method"] == "most_delayed")
        & (delayed["reroute_method"] == "normal_td_dijkstra")
        & (delayed["selection_fraction"].isin(baseline_fractions))
    ].copy()
    if delayed.empty:
        raise ValueError("No delayed baseline rows matched the expected filters.")

    delayed_tdg = pd.DataFrame()
    if delayed_tdg_reroute_csv:
        delayed_tdg = pd.read_csv(delayed_tdg_reroute_csv)
        delayed_tdg = delayed_tdg[
            (delayed_tdg["selection_method"] == "most_delayed")
            & (delayed_tdg["reroute_method"] == "tdg_impact_reroute")
            & (delayed_tdg["selection_fraction"].isin(baseline_fractions))
        ].copy()
        if delayed_tdg.empty:
            raise ValueError(
                "No delayed TDG-reroute baseline rows matched the expected filters."
            )

    tdg = pd.read_csv(tdg_anchor_csv)
    tdg = tdg[
        (tdg["selection_method"] == "tdg")
        & (tdg["removal_mode"] == "anchor_important")
        & (tdg["reroute_method"] == "normal_td_dijkstra")
        & (tdg["gamma"].isin(tdg_gammas))
    ].copy()
    if tdg.empty:
        raise ValueError("No TDG anchor rows matched the expected filters.")

    tdg_excess = pd.DataFrame()
    if tdg_excess_csv:
        tdg_excess = pd.read_csv(tdg_excess_csv)
        tdg_excess = tdg_excess[
            (tdg_excess["selection_method"] == "tdg_excess")
            & (tdg_excess["reroute_method"] == "normal_td_dijkstra")
            & (tdg_excess["gamma"].isin(tdg_gammas))
        ].copy()
        if tdg_excess.empty:
            raise ValueError("No TDG excess rows matched the expected filters.")

    removed = pd.DataFrame()
    if (
        max_up_ratio_threshold is not None
        or sum_up_ratio_threshold is not None
    ):
        metrics = tdg_volatility_metrics(tdg)
        remove_mask = pd.Series(False, index=metrics.index)
        if max_up_ratio_threshold is not None:
            remove_mask |= metrics["max_up_ratio"] >= max_up_ratio_threshold
        if sum_up_ratio_threshold is not None:
            remove_mask |= metrics["sum_up_ratio"] >= sum_up_ratio_threshold
        removed = metrics[remove_mask].copy()

        if not removed.empty:
            removed_datasets = set(removed["dataset"])
            delayed = delayed[~delayed["dataset"].isin(removed_datasets)].copy()
            tdg = tdg[~tdg["dataset"].isin(removed_datasets)].copy()
            if not delayed_tdg.empty:
                delayed_tdg = delayed_tdg[
                    ~delayed_tdg["dataset"].isin(removed_datasets)
                ].copy()
            if not tdg_excess.empty:
                tdg_excess = tdg_excess[
                    ~tdg_excess["dataset"].isin(removed_datasets)
                ].copy()

        if removed_output:
            os.makedirs(os.path.dirname(removed_output), exist_ok=True)
            removed.to_csv(removed_output, index=False)

    delayed_curves = curves_from_groups(
        delayed,
        ["hop", "rep", "selection_fraction", "dataset", "seed"],
        lambda keys: f"baseline_delayed_normal {int(keys['selection_fraction'])}%",
    )

    tdg_curves = curves_from_groups(
        tdg,
        ["hop", "rep", "gamma", "dataset", "seed"],
        lambda keys: f"tdg_anchor_normal gamma={int(keys['gamma'])}",
    )

    curve_frames = [delayed_curves, tdg_curves]
    if not delayed_tdg.empty:
        curve_frames.append(
            curves_from_groups(
                delayed_tdg,
                ["hop", "rep", "selection_fraction", "dataset", "seed"],
                lambda keys: (
                    f"baseline_delayed_tdg_reroute {int(keys['selection_fraction'])}%"
                ),
            )
        )
    if not tdg_excess.empty:
        curve_frames.append(
            curves_from_groups(
                tdg_excess,
                ["hop", "rep", "gamma", "dataset", "seed"],
                lambda keys: f"tdg_excess_normal gamma={int(keys['gamma'])}",
            )
        )

    curves = pd.concat(curve_frames, ignore_index=True)
    plot_df = (
        curves.groupby(
            ["hop", "rep", "method_label", "plot_iteration"],
            as_index=False,
        )["ttt"]
        .mean()
    )
    return plot_df, removed


def plot_3x3(plot_df: pd.DataFrame, output: str, log_y: bool) -> None:
    combos = (
        plot_df[["hop", "rep"]]
        .drop_duplicates()
        .sort_values(["hop", "rep"])
        .values
        .tolist()
    )

    n_cols = 3
    n_rows = math.ceil(len(combos) / n_cols)
    fig, axes = plt.subplots(
        n_rows,
        n_cols,
        figsize=(16, 11.5),
        sharex=True,
        squeeze=False,
    )

    style_map = {
        "baseline_delayed_normal 10%": {"color": "#F58518", "marker": "o"},
        "baseline_delayed_normal 30%": {"color": "#E45756", "marker": "s"},
        "baseline_delayed_tdg_reroute 10%": {"color": "#72B7B2", "marker": "X"},
        "baseline_delayed_tdg_reroute 30%": {"color": "#B279A2", "marker": "P"},
        "tdg_anchor_normal gamma=25": {"color": "#4C78A8", "marker": "^"},
        "tdg_anchor_normal gamma=50": {"color": "#54A24B", "marker": "D"},
        "tdg_excess_normal gamma=25": {"color": "#B279A2", "marker": "v"},
        "tdg_excess_normal gamma=50": {"color": "#9D755D", "marker": "P"},
    }
    method_order = list(style_map.keys())
    present_methods = list(plot_df["method_label"].drop_duplicates())
    ordered_methods = [m for m in method_order if m in present_methods]
    ordered_methods.extend(m for m in present_methods if m not in ordered_methods)
    max_iteration = int(plot_df["plot_iteration"].max())
    legend_handles = {}

    for index, (hop, rep) in enumerate(combos):
        ax = axes[index // n_cols][index % n_cols]
        sub = plot_df[(plot_df["hop"] == hop) & (plot_df["rep"] == rep)]

        for method in ordered_methods:
            curve = sub[sub["method_label"] == method].sort_values("plot_iteration")
            if curve.empty:
                continue
            style = style_map.get(method, {"color": None, "marker": "o"})
            line, = ax.plot(
                curve["plot_iteration"],
                curve["ttt"],
                label=method,
                color=style["color"],
                marker=style["marker"],
                linewidth=2.0,
                markersize=4.5,
            )
            legend_handles.setdefault(method, line)

        ax.set_title(f"Hop {hop}, Rep {rep}", fontsize=12)
        ax.set_xlabel("Iteration")
        ax.set_ylabel("TTT")
        ax.set_xticks(range(max_iteration + 1))
        ax.grid(True, alpha=0.25)

        if log_y:
            ax.set_yscale("log")
        else:
            formatter = ScalarFormatter(useMathText=True)
            formatter.set_scientific(True)
            formatter.set_useOffset(False)
            formatter.set_powerlimits((-3, 4))
            ax.yaxis.set_major_formatter(formatter)

    for index in range(len(combos), n_rows * n_cols):
        axes[index // n_cols][index % n_cols].set_visible(False)

    if legend_handles:
        handles = [
            legend_handles[method]
            for method in ordered_methods
            if method in legend_handles
        ]
        labels = [
            method
            for method in ordered_methods
            if method in legend_handles
        ]
        fig.legend(
            handles,
            labels,
            loc="lower center",
            ncol=min(3, len(labels)),
            frameon=False,
            bbox_to_anchor=(0.5, 0.01),
        )

    os.makedirs(os.path.dirname(output), exist_ok=True)
    fig.tight_layout(rect=(0, 0.075, 1, 1))
    fig.savefig(output, dpi=300, bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--delayed", required=True)
    parser.add_argument("--tdg-anchor", required=True)
    parser.add_argument("--tdg-excess")
    parser.add_argument("--delayed-tdg-reroute")
    parser.add_argument("--baseline-fractions", default="10,30")
    parser.add_argument("--tdg-gammas", default="25,50")
    parser.add_argument("--output", required=True)
    parser.add_argument("--log-y", action="store_true")
    parser.add_argument(
        "--max-up-ratio-threshold",
        type=float,
        help="Remove a dataset if TDG anchor has a one-step upward spike above this initial-TTT ratio.",
    )
    parser.add_argument(
        "--sum-up-ratio-threshold",
        type=float,
        help="Remove a dataset if TDG anchor cumulative upward movement exceeds this initial-TTT ratio.",
    )
    parser.add_argument(
        "--removed-output",
        help="Optional CSV path for removed volatile datasets.",
    )
    args = parser.parse_args()

    plot_df, removed = load_curves(
        args.delayed,
        args.tdg_anchor,
        tdg_excess_csv=args.tdg_excess,
        delayed_tdg_reroute_csv=args.delayed_tdg_reroute,
        baseline_fractions=parse_int_list(args.baseline_fractions),
        tdg_gammas=parse_int_list(args.tdg_gammas),
        max_up_ratio_threshold=args.max_up_ratio_threshold,
        sum_up_ratio_threshold=args.sum_up_ratio_threshold,
        removed_output=args.removed_output,
    )
    plot_3x3(plot_df, args.output, args.log_y)
    if (
        args.max_up_ratio_threshold is not None
        or args.sum_up_ratio_threshold is not None
    ):
        print(f"Removed volatile datasets: {len(removed)}")
        if not removed.empty:
            summary = removed.groupby(["hop", "rep"]).size().reset_index(name="removed")
            print(summary.to_string(index=False))
    print(f"Saved to {args.output}")


if __name__ == "__main__":
    main()
