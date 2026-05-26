import argparse
import os
import re
from typing import Optional

import pandas as pd


def infer_group(dataset: str, hop: int, rep: int) -> str:
    synthetic = re.match(r"Hop(\d+)Rep(\d+)-\d+$", dataset)
    if synthetic:
        return f"Hop{synthetic.group(1)}Rep{synthetic.group(2)}"

    real = re.match(r"BJRealRep(\d+)-\d+$", dataset)
    if real:
        return f"BJRealRep{real.group(1)}"

    real = re.match(r"MHRealRep(\d+)-\d+$", dataset)
    if real:
        return f"MHRealRep{real.group(1)}"

    if hop >= 0 and rep >= 0:
        return f"Hop{hop}Rep{rep}"
    if rep >= 0:
        return f"Rep{rep}"
    return "unknown"


def load_input(path: str) -> pd.DataFrame:
    df = pd.read_csv(path)
    required = {
        "dataset",
        "hop",
        "rep",
        "seed",
        "query_count",
        "free_flow_ttt",
        "evaluated_ttt",
        "congestion_extra_ttt",
        "inflation_ratio",
        "avg_free_flow_tt",
        "avg_evaluated_tt",
    }
    missing = sorted(required - set(df.columns))
    if missing:
        raise ValueError(f"{path} is missing columns: {missing}")

    df["group"] = [
        infer_group(str(row.dataset), int(row.hop), int(row.rep))
        for row in df.itertuples(index=False)
    ]
    return df


def build_summary(df: pd.DataFrame) -> pd.DataFrame:
    group_columns = ["group", "hop", "rep"]
    sort_columns = ["rep", "hop", "group"]
    if "window_label" in df.columns:
        group_columns = ["window_label"] + group_columns
        sort_columns = ["window_label"] + sort_columns

    summary = (
        df.groupby(group_columns, as_index=False)
        .agg(
            files=("dataset", "count"),
            mean_query_count=("query_count", "mean"),
            mean_free_flow_ttt=("free_flow_ttt", "mean"),
            mean_evaluated_ttt=("evaluated_ttt", "mean"),
            mean_congestion_extra_ttt=("congestion_extra_ttt", "mean"),
            mean_inflation_ratio=("inflation_ratio", "mean"),
            median_inflation_ratio=("inflation_ratio", "median"),
            max_inflation_ratio=("inflation_ratio", "max"),
            mean_avg_free_flow_tt=("avg_free_flow_tt", "mean"),
            mean_avg_evaluated_tt=("avg_evaluated_tt", "mean"),
        )
        .sort_values(sort_columns)
    )
    return summary


def maybe_write(df: pd.DataFrame, output: Optional[str]) -> None:
    if not output:
        return
    output_dir = os.path.dirname(output)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)
    df.to_csv(output, index=False)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--summary-output")
    parser.add_argument("--dataset-output")
    args = parser.parse_args()

    df = load_input(args.input)
    summary = build_summary(df)

    maybe_write(summary, args.summary_output)
    maybe_write(df, args.dataset_output)

    print("Grouped congestion summary:")
    print(summary.to_string(index=False, float_format=lambda value: f"{value:.4f}"))
    if args.summary_output:
        print(f"summary_csv={args.summary_output}")
    if args.dataset_output:
        print(f"dataset_csv={args.dataset_output}")


if __name__ == "__main__":
    main()
