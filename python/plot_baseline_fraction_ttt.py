import os
import math
import argparse
import pandas as pd
import matplotlib.pyplot as plt


def build_curve_for_group(group: pd.DataFrame) -> pd.DataFrame:
    """
    Build one curve for a single (dataset/seed/hop/rep/fraction) group.

    x = 0  : initial TTT, i.e., total_before of iteration 0
    x = 1..10 : total_after of iteration 0..9
    """
    group = group.sort_values("iteration").reset_index(drop=True)

    rows = []

    # initial TTT
    rows.append({
        "plot_iteration": 0,
        "ttt": group.loc[0, "total_before"]
    })

    # TTT after each iteration
    for _, row in group.iterrows():
        rows.append({
            "plot_iteration": int(row["iteration"]) + 1,
            "ttt": row["total_after"]
        })

    return pd.DataFrame(rows)


def curves_from_groups(
    df: pd.DataFrame,
    group_keys,
    label_builder,
) -> pd.DataFrame:
    curve_rows = []
    for key, group in df.groupby(group_keys, dropna=False):
        curve = build_curve_for_group(group)

        if isinstance(key, tuple):
            key_dict = dict(zip(group_keys, key))
        else:
            key_dict = {group_keys[0]: key}

        label = label_builder(key_dict)
        for _, row in curve.iterrows():
            item = dict(key_dict)
            item["plot_iteration"] = int(row["plot_iteration"])
            item["ttt"] = row["ttt"]
            item["method_label"] = label
            curve_rows.append(item)

    return pd.DataFrame(curve_rows)


def build_plot_dataframe(input_csv, tdg_input_csv=None, tdg_gammas=None):
    baseline_df = pd.read_csv(input_csv)

    # Keep only baseline random normal with fraction 10 and 30.
    baseline_df = baseline_df[
        (baseline_df["selection_method"] == "random") &
        (baseline_df["reroute_method"] == "normal_td_dijkstra") &
        (baseline_df["selection_fraction"].isin([10, 30]))
    ].copy()

    if baseline_df.empty:
        raise ValueError("No baseline rows matched the required conditions.")

    baseline_group_keys = [
        "hop", "rep", "selection_fraction",
        "dataset", "seed"
    ]
    baseline_group_keys = [k for k in baseline_group_keys if k in baseline_df.columns]

    curve_frames = [
        curves_from_groups(
            baseline_df,
            baseline_group_keys,
            lambda keys: f"Random {int(keys['selection_fraction'])}%"
        )
    ]

    if tdg_input_csv:
        tdg_df = pd.read_csv(tdg_input_csv)
        tdg_df = tdg_df[
            (tdg_df["selection_method"] == "tdg_excess") &
            (tdg_df["reroute_method"] == "normal_td_dijkstra")
        ].copy()

        if tdg_gammas:
            tdg_df = tdg_df[tdg_df["gamma"].isin(tdg_gammas)].copy()

        if tdg_df.empty:
            raise ValueError("No TDG rows matched the required conditions.")

        tdg_group_keys = [
            "hop", "rep", "gamma",
            "dataset", "seed"
        ]
        tdg_group_keys = [k for k in tdg_group_keys if k in tdg_df.columns]

        curve_frames.append(
            curves_from_groups(
                tdg_df,
                tdg_group_keys,
                lambda keys: f"TDG excess gamma={int(keys['gamma'])}"
            )
        )

    curve_df = pd.concat(curve_frames, ignore_index=True)

    # Average across dataset/seed for the same hop-rep-method-iteration.
    return (
        curve_df.groupby(
            ["hop", "rep", "method_label", "plot_iteration"],
            as_index=False
        )["ttt"]
        .mean()
    )


def plot_fraction_ttt_by_hop_rep(
    input_csv,
    output_path,
    tdg_input_csv=None,
    tdg_gammas=None,
    log_y=False,
):
    plot_df = build_plot_dataframe(
        input_csv=input_csv,
        tdg_input_csv=tdg_input_csv,
        tdg_gammas=tdg_gammas,
    )

    combos = (
        plot_df[["hop", "rep"]]
        .drop_duplicates()
        .sort_values(["hop", "rep"])
        .values
        .tolist()
    )

    n = len(combos)
    n_cols = 3
    n_rows = math.ceil(n / n_cols)

    fig, axes = plt.subplots(
        n_rows,
        n_cols,
        figsize=(5.2 * n_cols, 3.8 * n_rows),
        squeeze=False,
        sharex=True
    )

    style_map = {
        "Random 10%": {"marker": "o", "color": "#4C78A8", "linestyle": "-"},
        "Random 30%": {"marker": "s", "color": "#F58518", "linestyle": "-"},
        "TDG excess gamma=25": {"marker": "^", "color": "#54A24B", "linestyle": "-"},
        "TDG excess gamma=50": {"marker": "D", "color": "#E45756", "linestyle": "-"},
    }
    fallback_markers = ["o", "s", "^", "D", "v", "P", "X"]
    method_order = [
        "Random 10%",
        "Random 30%",
        "TDG excess gamma=25",
        "TDG excess gamma=50",
    ]
    all_methods = list(plot_df["method_label"].drop_duplicates())
    ordered_methods = [m for m in method_order if m in all_methods]
    ordered_methods += [m for m in all_methods if m not in ordered_methods]
    max_iteration = int(plot_df["plot_iteration"].max())
    legend_handles = {}

    for idx, (hop, rep) in enumerate(combos):
        r = idx // n_cols
        c = idx % n_cols
        ax = axes[r][c]

        sub = plot_df[(plot_df["hop"] == hop) & (plot_df["rep"] == rep)]

        for method_index, method_label in enumerate(ordered_methods):
            curve = sub[sub["method_label"] == method_label].sort_values("plot_iteration")
            if curve.empty:
                continue

            style = style_map.get(
                method_label,
                {
                    "marker": fallback_markers[method_index % len(fallback_markers)],
                    "color": None,
                    "linestyle": "-"
                }
            )
            line, = ax.plot(
                curve["plot_iteration"],
                curve["ttt"],
                marker=style["marker"],
                color=style["color"],
                linestyle=style["linestyle"],
                linewidth=2,
                markersize=5,
                label=method_label
            )
            legend_handles.setdefault(method_label, line)

        ax.set_title(f"Hop {hop}, Rep {rep}")
        ax.set_xlabel("Iteration")
        ax.set_ylabel("TTT")
        ax.set_xticks(range(0, max_iteration + 1))
        ax.grid(True, alpha=0.3)

        if log_y:
            ax.set_yscale("log")
            ax.set_ylabel("Log TTT")

    # Hide unused subplots
    for idx in range(n, n_rows * n_cols):
        r = idx // n_cols
        c = idx % n_cols
        axes[r][c].set_visible(False)

    if legend_handles:
        fig.legend(
            [legend_handles[label] for label in ordered_methods if label in legend_handles],
            [label for label in ordered_methods if label in legend_handles],
            loc="upper center",
            ncol=min(4, len(legend_handles)),
            frameon=False,
            bbox_to_anchor=(0.5, 1.02)
        )

    output_dir = os.path.dirname(output_path)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    fig.savefig(output_path, dpi=300, bbox_inches="tight")
    print(f"Saved to {output_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="Input CSV file")
    parser.add_argument("--tdg-input", help="Optional TDG selection CSV file")
    parser.add_argument(
        "--tdg-gammas",
        default="",
        help="Optional comma-separated TDG gamma values to include, e.g. 25,50"
    )
    parser.add_argument("--output", required=True, help="Output figure path")
    parser.add_argument("--log-y", action="store_true", help="Use log scale for y-axis")
    args = parser.parse_args()

    tdg_gammas = None
    if args.tdg_gammas.strip():
        tdg_gammas = [
            int(value.strip())
            for value in args.tdg_gammas.split(",")
            if value.strip()
        ]

    plot_fraction_ttt_by_hop_rep(
        input_csv=args.input,
        tdg_input_csv=args.tdg_input,
        tdg_gammas=tdg_gammas,
        output_path=args.output,
        log_y=args.log_y
    )
