#!/usr/bin/env python3

import argparse
from pathlib import Path

import pandas as pd


def load_tdg(path: Path) -> pd.DataFrame:
    tdg = pd.read_csv(path)
    tdg["tdg_selection_sec"] = tdg["tdg_prepare_sec"] + tdg["tdg_select_sec"]
    tdg["tdg_selected_fraction"] = tdg["selected_count"] / tdg["query_count"]
    return tdg


def load_baselines(path: Path) -> pd.DataFrame:
    baselines = pd.read_csv(path)
    baselines = baselines.rename(
        columns={
            "selected_count": "baseline_selected_count",
            "selection_fraction": "baseline_fraction",
            "unselected_after_remove": "baseline_unselected_after_remove",
            "reduction": "baseline_reduction",
            "select_sec": "baseline_select_sec",
            "evaluate_remaining_sec": "baseline_evaluate_remaining_sec",
        }
    )
    baselines["baseline_selected_fraction"] = (
        baselines["baseline_selected_count"] / baselines["query_count"]
    )
    return baselines


def compare(tdg: pd.DataFrame, baselines: pd.DataFrame) -> pd.DataFrame:
    join_cols = ["dataset", "hop", "rep", "seed", "query_count", "total_before"]
    merged = tdg.merge(
        baselines,
        on=join_cols,
        how="inner",
        suffixes=("", "_baseline"),
    )
    merged["value_vs_baseline"] = (
        merged["baseline_unselected_after_remove"]
        - merged["tdg_unselected_after_remove"]
    )
    merged["pct_vs_baseline_reduction"] = (
        merged["value_vs_baseline"] / merged["baseline_reduction"].replace(0, pd.NA) * 100.0
    )
    merged["pct_of_total_before"] = (
        merged["value_vs_baseline"] / merged["total_before"] * 100.0
    )
    merged["time_extra_vs_baseline_sec"] = (
        merged["tdg_selection_sec"] - merged["baseline_select_sec"]
    )
    merged["tdg_better"] = merged["value_vs_baseline"] > 0
    return merged


def summarize(df: pd.DataFrame, group_cols: list[str]) -> pd.DataFrame:
    summary = (
        df.groupby(group_cols, as_index=False)
        .agg(
            rows=("dataset", "count"),
            win_rate=("tdg_better", "mean"),
            mean_value_vs_baseline=("value_vs_baseline", "mean"),
            median_value_vs_baseline=("value_vs_baseline", "median"),
            mean_pct_vs_baseline_reduction=("pct_vs_baseline_reduction", "mean"),
            mean_pct_of_total_before=("pct_of_total_before", "mean"),
            mean_tdg_selection_sec=("tdg_selection_sec", "mean"),
            mean_baseline_select_sec=("baseline_select_sec", "mean"),
            mean_time_extra_vs_baseline_sec=("time_extra_vs_baseline_sec", "mean"),
            mean_tdg_selected_fraction=("tdg_selected_fraction", "mean"),
            mean_baseline_selected_fraction=("baseline_selected_fraction", "mean"),
        )
    )
    return summary


def print_main_summary(summary: pd.DataFrame) -> None:
    cols = [
        "gamma",
        "removal_mode",
        "selection_method",
        "baseline_fraction",
        "win_rate",
        "mean_value_vs_baseline",
        "mean_pct_vs_baseline_reduction",
        "mean_time_extra_vs_baseline_sec",
        "mean_tdg_selected_fraction",
        "mean_baseline_selected_fraction",
    ]
    shown = summary[cols].copy()
    shown["win_rate"] = shown["win_rate"].map(lambda x: f"{x:.1%}")
    shown["mean_value_vs_baseline"] = shown["mean_value_vs_baseline"].map(
        lambda x: f"{x:.0f}"
    )
    shown["mean_pct_vs_baseline_reduction"] = shown[
        "mean_pct_vs_baseline_reduction"
    ].map(lambda x: f"{x:.2f}%")
    shown["mean_time_extra_vs_baseline_sec"] = shown[
        "mean_time_extra_vs_baseline_sec"
    ].map(lambda x: f"{x:.3f}")
    shown["mean_tdg_selected_fraction"] = shown[
        "mean_tdg_selected_fraction"
    ].map(lambda x: f"{x:.1%}")
    shown["mean_baseline_selected_fraction"] = shown[
        "mean_baseline_selected_fraction"
    ].map(lambda x: f"{x:.1%}")
    print(shown.to_string(index=False))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--tdg-selection",
        default="python/results/gro_selection_debug_removal_modes.csv",
    )
    parser.add_argument(
        "--simple-baselines",
        default="python/results/gro_simple_selection_baselines_10_30.csv",
    )
    parser.add_argument("--output-dir", default="python/results")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    tdg = load_tdg(Path(args.tdg_selection))
    baselines = load_baselines(Path(args.simple_baselines))
    merged = compare(tdg, baselines)

    stem = Path(args.simple_baselines).stem
    merged_path = output_dir / f"{stem}_vs_tdg_selection_rows.csv"
    summary_path = output_dir / f"{stem}_vs_tdg_selection_summary.csv"
    by_hop_rep_path = output_dir / f"{stem}_vs_tdg_selection_by_hop_rep.csv"

    summary = summarize(
        merged,
        ["gamma", "removal_mode", "selection_method", "baseline_fraction"],
    )
    by_hop_rep = summarize(
        merged,
        [
            "hop",
            "rep",
            "gamma",
            "removal_mode",
            "selection_method",
            "baseline_fraction",
        ],
    )

    merged.to_csv(merged_path, index=False)
    summary.to_csv(summary_path, index=False)
    by_hop_rep.to_csv(by_hop_rep_path, index=False)

    print(f"merged_rows={len(merged)}")
    print_main_summary(summary)
    print()
    print(f"merged_csv={merged_path}")
    print(f"summary_csv={summary_path}")
    print(f"by_hop_rep_csv={by_hop_rep_path}")


if __name__ == "__main__":
    main()
