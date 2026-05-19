#!/usr/bin/env python3

import argparse
from pathlib import Path

import pandas as pd


MODE_ORDER = ["all_nodes", "anchor_important", "congestion_important"]
MODE_LABELS = {
    "all_nodes": "All nodes",
    "anchor_important": "Anchor important",
    "congestion_important": "Congestion important",
}


def load_results(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path)
    df["tdg_reduction"] = df["total_before"] - df["tdg_unselected_after_remove"]
    df["random_reduction"] = df["total_before"] - df["random_unselected_after_remove"]
    df["gain_vs_random"] = df["tdg_reduction"] - df["random_reduction"]
    df["value_better_than_random"] = (
        df["random_unselected_after_remove"] - df["tdg_unselected_after_remove"]
    )
    df["pct_more_reduction_than_random"] = (
        df["value_better_than_random"] / df["random_reduction"].replace(0, pd.NA) * 100.0
    )
    df["gain_pct_total"] = df["gain_vs_random"] / df["total_before"] * 100.0
    df["gain_per_selected"] = df["gain_vs_random"] / df["selected_count"].clip(lower=1)
    df["tdg_better"] = df["gain_vs_random"] > 0
    df["selected_fraction"] = df["selected_count"] / df["query_count"]
    df["tdg_total_selection_sec"] = df["tdg_prepare_sec"] + df["tdg_select_sec"]
    df["time_extra_vs_random_sec"] = (
        df["tdg_total_selection_sec"] - df["random_select_sec"]
    )
    df["pure_select_extra_vs_random_sec"] = (
        df["tdg_select_sec"] - df["random_select_sec"]
    )
    return df


def summarize(df: pd.DataFrame) -> pd.DataFrame:
    summary = (
        df.groupby(["gamma", "removal_mode"], as_index=False)
        .agg(
            rows=("dataset", "count"),
            win_rate=("tdg_better", "mean"),
            mean_gain=("gain_vs_random", "mean"),
            median_gain=("gain_vs_random", "median"),
            mean_gain_pct_total=("gain_pct_total", "mean"),
            mean_gain_per_selected=("gain_per_selected", "mean"),
            mean_tdg_reduction=("tdg_reduction", "mean"),
            mean_random_reduction=("random_reduction", "mean"),
            mean_selected_count=("selected_count", "mean"),
            median_selected_count=("selected_count", "median"),
            mean_selected_fraction=("selected_fraction", "mean"),
            mean_tdg_prepare_sec=("tdg_prepare_sec", "mean"),
            mean_tdg_select_sec=("tdg_select_sec", "mean"),
            mean_random_select_sec=("random_select_sec", "mean"),
            mean_important_node_count=("important_node_count", "mean"),
        )
    )
    summary["removal_mode"] = pd.Categorical(
        summary["removal_mode"], MODE_ORDER, ordered=True
    )
    return summary.sort_values(["gamma", "removal_mode"])


def summarize_by_hop_rep(df: pd.DataFrame) -> pd.DataFrame:
    by_hop_rep = (
        df.groupby(["hop", "rep", "gamma", "removal_mode"], as_index=False)
        .agg(
            rows=("dataset", "count"),
            win_rate=("tdg_better", "mean"),
            mean_gain=("gain_vs_random", "mean"),
            median_gain=("gain_vs_random", "median"),
            mean_gain_pct_total=("gain_pct_total", "mean"),
            mean_selected_fraction=("selected_fraction", "mean"),
        )
    )
    by_hop_rep["removal_mode"] = pd.Categorical(
        by_hop_rep["removal_mode"], MODE_ORDER, ordered=True
    )
    return by_hop_rep.sort_values(["hop", "rep", "gamma", "removal_mode"])


def summarize_comparison(df: pd.DataFrame) -> pd.DataFrame:
    comparison = (
        df.groupby(["gamma", "removal_mode"], as_index=False)
        .agg(
            rows=("dataset", "count"),
            win_rate=("tdg_better", "mean"),
            mean_value_better_than_random=("value_better_than_random", "mean"),
            median_value_better_than_random=("value_better_than_random", "median"),
            mean_random_reduction=("random_reduction", "mean"),
            mean_total_before=("total_before", "mean"),
            mean_pct_more_reduction_than_random=(
                "pct_more_reduction_than_random",
                "mean",
            ),
            median_pct_more_reduction_than_random=(
                "pct_more_reduction_than_random",
                "median",
            ),
            mean_pct_of_total_before=("gain_pct_total", "mean"),
            mean_tdg_total_selection_sec=("tdg_total_selection_sec", "mean"),
            mean_random_selection_sec=("random_select_sec", "mean"),
            mean_extra_time_vs_random_sec=("time_extra_vs_random_sec", "mean"),
            mean_pure_select_extra_vs_random_sec=(
                "pure_select_extra_vs_random_sec",
                "mean",
            ),
            mean_selected_count=("selected_count", "mean"),
            mean_selected_fraction=("selected_fraction", "mean"),
        )
    )
    comparison["pct_vs_random_reduction_of_means"] = (
        comparison["mean_value_better_than_random"]
        / comparison["mean_random_reduction"]
        * 100.0
    )
    comparison["pct_of_mean_total_before"] = (
        comparison["mean_value_better_than_random"]
        / comparison["mean_total_before"]
        * 100.0
    )
    comparison["removal_mode"] = pd.Categorical(
        comparison["removal_mode"], MODE_ORDER, ordered=True
    )
    return comparison.sort_values(["gamma", "removal_mode"])


def summarize_random_baseline_timing(df: pd.DataFrame, by: list[str]) -> pd.DataFrame:
    summary = (
        df.groupby(by, as_index=False)
        .agg(
            rows=("dataset", "count"),
            mean_value_better_than_random=("value_better_than_random", "mean"),
            median_value_better_than_random=("value_better_than_random", "median"),
            mean_random_reduction=("random_reduction", "mean"),
            mean_total_before=("total_before", "mean"),
            mean_tdg_total_selection_sec=("tdg_total_selection_sec", "mean"),
            median_tdg_total_selection_sec=("tdg_total_selection_sec", "median"),
            mean_random_selection_sec=("random_select_sec", "mean"),
            median_random_selection_sec=("random_select_sec", "median"),
            mean_extra_time_vs_random_sec=("time_extra_vs_random_sec", "mean"),
            median_extra_time_vs_random_sec=("time_extra_vs_random_sec", "median"),
            mean_selected_fraction=("selected_fraction", "mean"),
        )
    )
    summary["pct_vs_random_reduction_of_means"] = (
        summary["mean_value_better_than_random"]
        / summary["mean_random_reduction"]
        * 100.0
    )
    summary["pct_of_mean_total_before"] = (
        summary["mean_value_better_than_random"]
        / summary["mean_total_before"]
        * 100.0
    )
    summary["mean_slowdown_ratio"] = (
        summary["mean_tdg_total_selection_sec"]
        / summary["mean_random_selection_sec"]
    )
    return summary


def plot_summary(summary: pd.DataFrame, output_path: Path) -> None:
    import matplotlib.pyplot as plt

    colors = {
        "all_nodes": "#4C78A8",
        "anchor_important": "#F58518",
        "congestion_important": "#54A24B",
    }
    gammas = sorted(summary["gamma"].unique())
    x = range(len(gammas))
    width = 0.24

    fig, axes = plt.subplots(2, 2, figsize=(13, 8), constrained_layout=True)
    metrics = [
        ("mean_gain", "Mean gain over random\n(million aggregate TTT units)", 1_000_000.0),
        ("win_rate", "Win rate vs random", 1.0),
        ("mean_selected_fraction", "Selected query fraction", 1.0),
        ("mean_tdg_select_sec", "TDG selection time (sec)", 1.0),
    ]

    for ax, (metric, title, scale) in zip(axes.flat, metrics):
        for offset_index, mode in enumerate(MODE_ORDER):
            mode_df = summary[summary["removal_mode"] == mode]
            values = []
            for gamma in gammas:
                row = mode_df[mode_df["gamma"] == gamma]
                values.append(float(row[metric].iloc[0]) / scale)
            positions = [i + (offset_index - 1) * width for i in x]
            ax.bar(
                positions,
                values,
                width=width,
                label=MODE_LABELS[mode],
                color=colors[mode],
            )
        ax.set_title(title)
        ax.set_xticks(list(x))
        ax.set_xticklabels([str(g) for g in gammas])
        ax.set_xlabel("gamma")
        ax.axhline(0, color="#333333", linewidth=0.8)
        ax.grid(axis="y", alpha=0.25)

    axes[0, 1].set_ylim(0, 1)
    axes[1, 0].set_ylim(0, 1)
    axes[0, 0].legend(loc="best", frameon=False)
    fig.suptitle("Selection Debug: TDG Selection vs Same-size Random Selection")
    fig.savefig(output_path, dpi=200)
    plt.close(fig)


def plot_distribution(df: pd.DataFrame, output_path: Path) -> None:
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(1, 2, figsize=(14, 5), constrained_layout=True)
    labels = [MODE_LABELS[m] for m in MODE_ORDER]

    data = [
        df[df["removal_mode"] == mode]["gain_vs_random"] / 1_000_000.0
        for mode in MODE_ORDER
    ]
    axes[0].boxplot(data, labels=labels, showfliers=False)
    axes[0].axhline(0, color="#333333", linewidth=0.8)
    axes[0].set_ylabel("Gain over random (million aggregate TTT units)")
    axes[0].set_title("Gain Distribution")
    axes[0].grid(axis="y", alpha=0.25)

    data_pct = [
        df[df["removal_mode"] == mode]["gain_pct_total"]
        for mode in MODE_ORDER
    ]
    axes[1].boxplot(data_pct, labels=labels, showfliers=False)
    axes[1].axhline(0, color="#333333", linewidth=0.8)
    axes[1].set_ylabel("Gain / total_before (%)")
    axes[1].set_title("Relative Gain Distribution")
    axes[1].grid(axis="y", alpha=0.25)

    for ax in axes:
        ax.tick_params(axis="x", labelrotation=15)

    fig.savefig(output_path, dpi=200)
    plt.close(fig)


def print_console_summary(df: pd.DataFrame, summary: pd.DataFrame) -> None:
    print(f"rows={len(df)} datasets={df['dataset'].nunique()}")
    print(f"gammas={sorted(df['gamma'].unique())}")
    print(f"removal_modes={list(summary['removal_mode'].cat.categories)}")
    print()

    display_cols = [
        "gamma",
        "removal_mode",
        "win_rate",
        "mean_gain",
        "median_gain",
        "mean_gain_pct_total",
        "mean_selected_fraction",
        "mean_tdg_select_sec",
    ]
    shown = summary[display_cols].copy()
    shown["win_rate"] = shown["win_rate"].map(lambda x: f"{x:.3f}")
    shown["mean_gain"] = shown["mean_gain"].map(lambda x: f"{x:.0f}")
    shown["median_gain"] = shown["median_gain"].map(lambda x: f"{x:.0f}")
    shown["mean_gain_pct_total"] = shown["mean_gain_pct_total"].map(lambda x: f"{x:.3f}")
    shown["mean_selected_fraction"] = shown["mean_selected_fraction"].map(lambda x: f"{x:.3f}")
    shown["mean_tdg_select_sec"] = shown["mean_tdg_select_sec"].map(lambda x: f"{x:.3f}")
    print(shown.to_string(index=False))


def print_comparison_summary(comparison: pd.DataFrame) -> None:
    print()
    print("Comparison against same-size random selection:")
    display_cols = [
        "gamma",
        "removal_mode",
        "win_rate",
        "mean_value_better_than_random",
        "median_value_better_than_random",
        "pct_vs_random_reduction_of_means",
        "pct_of_mean_total_before",
        "mean_tdg_total_selection_sec",
        "mean_random_selection_sec",
        "mean_extra_time_vs_random_sec",
        "mean_selected_fraction",
    ]
    shown = comparison[display_cols].copy()
    shown["win_rate"] = shown["win_rate"].map(lambda x: f"{x:.1%}")
    shown["mean_value_better_than_random"] = shown[
        "mean_value_better_than_random"
    ].map(lambda x: f"{x:.0f}")
    shown["median_value_better_than_random"] = shown[
        "median_value_better_than_random"
    ].map(lambda x: f"{x:.0f}")
    shown["pct_vs_random_reduction_of_means"] = shown[
        "pct_vs_random_reduction_of_means"
    ].map(lambda x: f"{x:.2f}%")
    shown["pct_of_mean_total_before"] = shown["pct_of_mean_total_before"].map(
        lambda x: f"{x:.3f}%"
    )
    shown["mean_tdg_total_selection_sec"] = shown[
        "mean_tdg_total_selection_sec"
    ].map(lambda x: f"{x:.3f}")
    shown["mean_random_selection_sec"] = shown[
        "mean_random_selection_sec"
    ].map(lambda x: f"{x:.6f}")
    shown["mean_extra_time_vs_random_sec"] = shown[
        "mean_extra_time_vs_random_sec"
    ].map(lambda x: f"{x:.3f}")
    shown["mean_selected_fraction"] = shown["mean_selected_fraction"].map(
        lambda x: f"{x:.1%}"
    )
    print(shown.to_string(index=False))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input",
        default="python/results/gro_selection_debug_removal_modes.csv",
        help="Selection debug CSV.",
    )
    parser.add_argument(
        "--output-dir",
        default="python/results",
        help="Directory for summary CSV files.",
    )
    parser.add_argument(
        "--plots",
        action="store_true",
        help="Also write PNG plots. Disabled by default to keep results clean.",
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    df = load_results(input_path)
    summary = summarize(df)
    by_hop_rep = summarize_by_hop_rep(df)
    comparison = summarize_comparison(df)
    random_timing_by_gamma = summarize_random_baseline_timing(df, ["gamma"])
    random_timing_by_query_count = summarize_random_baseline_timing(
        df, ["query_count"]
    )
    random_timing_by_hop_rep = summarize_random_baseline_timing(
        df, ["hop", "rep"]
    )
    random_timing_by_hop_rep_gamma = summarize_random_baseline_timing(
        df, ["hop", "rep", "gamma"]
    )

    stem = input_path.stem
    summary_path = output_dir / f"{stem}_analysis_summary.csv"
    by_hop_rep_path = output_dir / f"{stem}_analysis_by_hop_rep.csv"
    comparison_path = output_dir / f"{stem}_comparison_summary.csv"
    random_timing_by_gamma_path = (
        output_dir / f"{stem}_random_timing_by_gamma.csv"
    )
    random_timing_by_query_count_path = (
        output_dir / f"{stem}_random_timing_by_query_count.csv"
    )
    random_timing_by_hop_rep_path = (
        output_dir / f"{stem}_random_timing_by_hop_rep.csv"
    )
    random_timing_by_hop_rep_gamma_path = (
        output_dir / f"{stem}_random_timing_by_hop_rep_gamma.csv"
    )

    summary.to_csv(summary_path, index=False)
    by_hop_rep.to_csv(by_hop_rep_path, index=False)
    comparison.to_csv(comparison_path, index=False)
    random_timing_by_gamma.to_csv(random_timing_by_gamma_path, index=False)
    random_timing_by_query_count.to_csv(
        random_timing_by_query_count_path, index=False
    )
    random_timing_by_hop_rep.to_csv(random_timing_by_hop_rep_path, index=False)
    random_timing_by_hop_rep_gamma.to_csv(
        random_timing_by_hop_rep_gamma_path, index=False
    )
    if args.plots:
        summary_plot_path = output_dir / f"{stem}_analysis_summary.png"
        dist_plot_path = output_dir / f"{stem}_analysis_distribution.png"
        plot_summary(summary, summary_plot_path)
        plot_distribution(df, dist_plot_path)

    print_console_summary(df, summary)
    print_comparison_summary(comparison)
    print()
    print(f"summary_csv={summary_path}")
    print(f"by_hop_rep_csv={by_hop_rep_path}")
    print(f"comparison_csv={comparison_path}")
    print(f"random_timing_by_gamma_csv={random_timing_by_gamma_path}")
    print(f"random_timing_by_query_count_csv={random_timing_by_query_count_path}")
    print(f"random_timing_by_hop_rep_csv={random_timing_by_hop_rep_path}")
    print(f"random_timing_by_hop_rep_gamma_csv={random_timing_by_hop_rep_gamma_path}")
    if args.plots:
        print(f"summary_plot={summary_plot_path}")
        print(f"distribution_plot={dist_plot_path}")


if __name__ == "__main__":
    main()
