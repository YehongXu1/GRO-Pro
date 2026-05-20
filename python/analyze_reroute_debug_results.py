#!/usr/bin/env python3

import argparse
from pathlib import Path
from typing import Optional

import pandas as pd


REQUIRED_COLUMNS = [
    "dataset",
    "hop",
    "rep",
    "seed",
    "query_count",
    "selected_count",
    "reroute_method",
    "impact_weight",
    "total_before",
    "total_after",
    "tdg_prepare_sec",
    "normalize_sec",
    "reroute_sec",
    "evaluate_after_sec",
    "method_total_sec",
]

NUMERIC_COLUMNS = [
    "hop",
    "rep",
    "seed",
    "query_count",
    "selection_fraction",
    "random_seed",
    "selected_count",
    "lambda",
    "impact_weight",
    "total_before",
    "total_after",
    "tdg_prepare_sec",
    "normalize_sec",
    "reroute_sec",
    "evaluate_after_sec",
    "method_total_sec",
    "mean_selected_impact_score",
    "mean_all_query_impact_score",
    "tdg_node_count",
    "tdg_edge_timeline_count",
]


def load_results(path: Path) -> tuple[pd.DataFrame, pd.DataFrame]:
    raw = pd.read_csv(path)
    for col in NUMERIC_COLUMNS:
        if col in raw.columns:
            raw[col] = pd.to_numeric(raw[col], errors="coerce")
    complete = raw.dropna(subset=REQUIRED_COLUMNS).copy()
    complete["impact_weight"] = complete["impact_weight"].astype(int)
    return raw, complete


def compare_to_normal(df: pd.DataFrame) -> pd.DataFrame:
    normal = df[df["reroute_method"] == "normal_td_dijkstra"].copy()
    normal = normal.rename(
        columns={
            "total_after": "normal_after",
            "reroute_sec": "normal_reroute_sec",
            "evaluate_after_sec": "normal_evaluate_after_sec",
            "method_total_sec": "normal_total_sec",
        }
    )
    normal_cols = [
        "dataset",
        "hop",
        "rep",
        "seed",
        "query_count",
        "selected_count",
        "total_before",
        "normal_after",
        "normal_reroute_sec",
        "normal_evaluate_after_sec",
        "normal_total_sec",
    ]

    tdg = df[df["reroute_method"] == "tdg_impact_reroute"].copy()
    merged = tdg.merge(
        normal[normal_cols],
        on=["dataset", "hop", "rep", "seed", "query_count", "selected_count", "total_before"],
        how="inner",
    )
    merged = merged.rename(columns={"total_after": "tdg_after"})
    merged["normal_reduction"] = merged["total_before"] - merged["normal_after"]
    merged["tdg_reduction"] = merged["total_before"] - merged["tdg_after"]
    merged["gain_vs_normal"] = merged["normal_after"] - merged["tdg_after"]
    merged["pct_vs_normal_after"] = merged["gain_vs_normal"] / merged[
        "normal_after"
    ].replace(0, pd.NA) * 100.0
    merged["pct_extra_reduction_vs_normal"] = merged["gain_vs_normal"] / merged[
        "normal_reduction"
    ].replace(0, pd.NA) * 100.0
    merged["pct_of_total_before"] = (
        merged["gain_vs_normal"] / merged["total_before"].replace(0, pd.NA) * 100.0
    )
    merged["tdg_better"] = merged["gain_vs_normal"] > 0
    merged["extra_total_sec"] = merged["method_total_sec"] - merged["normal_total_sec"]
    merged["extra_reroute_sec"] = merged["reroute_sec"] - merged["normal_reroute_sec"]
    return merged


def summarize_by_weight(compared: pd.DataFrame) -> pd.DataFrame:
    return (
        compared.groupby("impact_weight", as_index=False)
        .agg(
            rows=("dataset", "count"),
            win_rate=("tdg_better", "mean"),
            mean_gain_vs_normal=("gain_vs_normal", "mean"),
            median_gain_vs_normal=("gain_vs_normal", "median"),
            mean_pct_vs_normal_after=("pct_vs_normal_after", "mean"),
            mean_pct_extra_reduction_vs_normal=(
                "pct_extra_reduction_vs_normal",
                "mean",
            ),
            mean_pct_of_total_before=("pct_of_total_before", "mean"),
            mean_normal_after=("normal_after", "mean"),
            mean_tdg_after=("tdg_after", "mean"),
            mean_normal_total_sec=("normal_total_sec", "mean"),
            mean_tdg_total_sec=("method_total_sec", "mean"),
            mean_extra_total_sec=("extra_total_sec", "mean"),
            mean_normal_reroute_sec=("normal_reroute_sec", "mean"),
            mean_tdg_reroute_sec=("reroute_sec", "mean"),
            mean_extra_reroute_sec=("extra_reroute_sec", "mean"),
            mean_tdg_prepare_sec=("tdg_prepare_sec", "mean"),
            mean_normalize_sec=("normalize_sec", "mean"),
            mean_tdg_evaluate_after_sec=("evaluate_after_sec", "mean"),
            mean_selected_count=("selected_count", "mean"),
            mean_selected_fraction=("selected_count", lambda s: float("nan")),
        )
        .sort_values("impact_weight")
    )


def add_selected_fraction(summary: pd.DataFrame, compared: pd.DataFrame) -> pd.DataFrame:
    fractions = (
        compared.assign(selected_fraction=compared["selected_count"] / compared["query_count"])
        .groupby("impact_weight", as_index=False)
        .agg(mean_selected_fraction=("selected_fraction", "mean"))
    )
    summary = summary.drop(columns=["mean_selected_fraction"], errors="ignore")
    return summary.merge(fractions, on="impact_weight", how="left")


def summarize_by_hop_rep(compared: pd.DataFrame) -> pd.DataFrame:
    return (
        compared.groupby(["hop", "rep", "impact_weight"], as_index=False)
        .agg(
            rows=("dataset", "count"),
            win_rate=("tdg_better", "mean"),
            mean_gain_vs_normal=("gain_vs_normal", "mean"),
            median_gain_vs_normal=("gain_vs_normal", "median"),
            mean_pct_vs_normal_after=("pct_vs_normal_after", "mean"),
            mean_extra_total_sec=("extra_total_sec", "mean"),
            mean_tdg_total_sec=("method_total_sec", "mean"),
            mean_normal_total_sec=("normal_total_sec", "mean"),
            mean_selected_fraction=(
                "selected_count",
                lambda s: float("nan"),
            ),
        )
        .sort_values(["hop", "rep", "impact_weight"])
    )


def add_hop_rep_fraction(summary: pd.DataFrame, compared: pd.DataFrame) -> pd.DataFrame:
    fractions = (
        compared.assign(selected_fraction=compared["selected_count"] / compared["query_count"])
        .groupby(["hop", "rep", "impact_weight"], as_index=False)
        .agg(mean_selected_fraction=("selected_fraction", "mean"))
    )
    summary = summary.drop(columns=["mean_selected_fraction"], errors="ignore")
    return summary.merge(fractions, on=["hop", "rep", "impact_weight"], how="left")


def summarize_best_weight(compared: pd.DataFrame) -> pd.DataFrame:
    best = compared.sort_values(["dataset", "tdg_after", "method_total_sec"]).groupby(
        "dataset", as_index=False
    ).first()
    best["best_gain_vs_normal"] = best["gain_vs_normal"]
    best["best_pct_vs_normal_after"] = best["pct_vs_normal_after"]
    return best


def completeness(
    raw: pd.DataFrame,
    complete: pd.DataFrame,
    expected_datasets_path: Optional[Path],
) -> pd.DataFrame:
    rows = []
    rows.append(("raw_data_rows", len(raw)))
    rows.append(("complete_data_rows", len(complete)))
    rows.append(("dropped_incomplete_rows", len(raw) - len(complete)))
    rows.append(("complete_unique_datasets", complete["dataset"].nunique()))

    if not complete.empty:
        tdg_weights = sorted(
            complete.loc[
                complete["reroute_method"] == "tdg_impact_reroute", "impact_weight"
            ].unique()
        )
        expected_rows_per_dataset = 1 + len(tdg_weights)
        row_counts = complete.groupby("dataset").size()
        rows.append(("observed_tdg_weights", " ".join(map(str, tdg_weights))))
        rows.append(("expected_rows_per_complete_dataset", expected_rows_per_dataset))
        rows.append(("datasets_with_all_expected_rows", int((row_counts == expected_rows_per_dataset).sum())))
        rows.append(("datasets_missing_some_rows", int((row_counts < expected_rows_per_dataset).sum())))

        missing_weights = []
        for dataset, group in complete.groupby("dataset"):
            present = set(
                group.loc[group["reroute_method"] == "tdg_impact_reroute", "impact_weight"]
            )
            absent = [str(w) for w in tdg_weights if w not in present]
            has_normal = (group["reroute_method"] == "normal_td_dijkstra").any()
            if absent or not has_normal:
                parts = []
                if not has_normal:
                    parts.append("normal")
                if absent:
                    parts.append("w=" + "/".join(absent))
                missing_weights.append(f"{dataset}:{';'.join(parts)}")
        rows.append(("examples_missing_rows", " | ".join(missing_weights[:10])))

    if expected_datasets_path and expected_datasets_path.exists():
        expected = pd.read_csv(expected_datasets_path, usecols=["dataset"])
        expected_set = set(expected["dataset"].dropna().unique())
        complete_set = set(complete["dataset"].dropna().unique())
        missing = sorted(expected_set - complete_set)
        rows.append(("expected_unique_datasets", len(expected_set)))
        rows.append(("datasets_missing_from_expected_file", len(missing)))
        rows.append(("examples_missing_datasets", " ".join(missing[:20])))

    return pd.DataFrame(rows, columns=["metric", "value"])


def print_table(df: pd.DataFrame, cols: list[str]) -> None:
    shown = df[cols].copy()
    for col in shown.columns:
        if col.startswith("mean_") or col.startswith("median_") or col.startswith("pct_"):
            if pd.api.types.is_numeric_dtype(shown[col]):
                shown[col] = shown[col].map(lambda x: f"{x:.4f}")
    if "win_rate" in shown:
        shown["win_rate"] = shown["win_rate"].map(lambda x: f"{x:.1%}")
    print(shown.to_string(index=False))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", default="python/results/mh/gro_reroute_debug.csv")
    parser.add_argument("--output-dir", default="python/results")
    parser.add_argument(
        "--expected-datasets",
        default="python/results/mh/gro_selection_debug_removal_modes.csv",
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    raw, complete = load_results(input_path)
    compared = compare_to_normal(complete)
    summary = add_selected_fraction(summarize_by_weight(compared), compared)
    by_hop_rep = add_hop_rep_fraction(summarize_by_hop_rep(compared), compared)
    best = summarize_best_weight(compared)
    completeness_df = completeness(raw, complete, Path(args.expected_datasets))

    stem = input_path.stem
    compared_path = output_dir / f"{stem}_analysis_rows.csv"
    summary_path = output_dir / f"{stem}_analysis_summary.csv"
    by_hop_rep_path = output_dir / f"{stem}_analysis_by_hop_rep.csv"
    best_path = output_dir / f"{stem}_analysis_best_weight_by_dataset.csv"
    completeness_path = output_dir / f"{stem}_analysis_completeness.csv"

    compared.to_csv(compared_path, index=False)
    summary.to_csv(summary_path, index=False)
    by_hop_rep.to_csv(by_hop_rep_path, index=False)
    best.to_csv(best_path, index=False)
    completeness_df.to_csv(completeness_path, index=False)

    print("Completeness")
    print(completeness_df.to_string(index=False))
    print()
    print("By impact_weight")
    print_table(
        summary,
        [
            "impact_weight",
            "rows",
            "win_rate",
            "mean_gain_vs_normal",
            "median_gain_vs_normal",
            "mean_pct_vs_normal_after",
            "mean_extra_total_sec",
            "mean_normal_total_sec",
            "mean_tdg_total_sec",
            "mean_tdg_prepare_sec",
            "mean_normalize_sec",
            "mean_tdg_reroute_sec",
            "mean_selected_fraction",
        ],
    )
    print()
    print("Best TDG weight per dataset")
    best_counts = best.groupby("impact_weight", as_index=False).agg(rows=("dataset", "count"))
    print(best_counts.to_string(index=False))
    print()
    print(f"rows_csv={compared_path}")
    print(f"summary_csv={summary_path}")
    print(f"by_hop_rep_csv={by_hop_rep_path}")
    print(f"best_weight_csv={best_path}")
    print(f"completeness_csv={completeness_path}")


if __name__ == "__main__":
    main()
