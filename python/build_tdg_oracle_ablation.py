#!/usr/bin/env python3
"""Build diagnostic TDG-oracle ablation CSVs.

For each dataset and iteration, the oracle keeps the TDG-family run with the
lowest total_after, regardless of TDG selection variant and parameter. This is
not a fair final-method result; it is a diagnostic lower envelope for checking
whether the TDG selection family contains a good choice for each iteration.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Iterable

import pandas as pd


KEY_COLUMNS = ["dataset", "hop", "rep", "seed", "iteration"]


def read_inputs(paths: Iterable[Path]) -> pd.DataFrame:
    frames = []
    for path in paths:
        if not path.exists():
            raise FileNotFoundError(path)
        df = pd.read_csv(path)
        df["source_file"] = path.name
        frames.append(df)
    if not frames:
        raise ValueError("No input CSVs were provided")
    return pd.concat(frames, ignore_index=True)


def build_oracle(df: pd.DataFrame, reroute_method: str) -> pd.DataFrame:
    candidates = df[df["reroute_method"] == reroute_method].copy()
    if candidates.empty:
        raise ValueError(f"No rows found for reroute_method={reroute_method}")

    candidates = candidates.sort_values(
        KEY_COLUMNS
        + [
            "total_after",
            "selected_fraction",
            "selection_method",
            "gamma",
            "impact_weight",
            "source_file",
        ]
    )
    best = candidates.groupby(KEY_COLUMNS, as_index=False).first()

    best["chosen_selection_method"] = best["selection_method"]
    best["chosen_removal_mode"] = best["removal_mode"]
    best["chosen_gamma"] = best["gamma"]
    best["chosen_impact_weight"] = best["impact_weight"]
    best["chosen_source_file"] = best["source_file"]

    best["selection_method"] = "tdg_oracle"
    best["removal_mode"] = "oracle_best"
    best["selection_fraction"] = -1
    best["gamma"] = -1
    if reroute_method == "normal_td_dijkstra":
        best["impact_weight"] = 0
    return best


def write_choice_summary(oracle: pd.DataFrame, output: Path) -> None:
    summary = (
        oracle.groupby(
            [
                "hop",
                "rep",
                "iteration",
                "reroute_method",
                "chosen_selection_method",
                "chosen_removal_mode",
                "chosen_gamma",
                "chosen_impact_weight",
            ],
            as_index=False,
        )
        .agg(
            dataset_count=("dataset", "nunique"),
            mean_selected_fraction=("selected_fraction", "mean"),
            mean_total_after=("total_after", "mean"),
        )
        .sort_values(
            [
                "hop",
                "rep",
                "iteration",
                "reroute_method",
                "dataset_count",
            ],
            ascending=[True, True, True, True, False],
        )
    )
    summary.to_csv(output, index=False)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-normal", required=True)
    parser.add_argument("--output-tdg-reroute", required=True)
    parser.add_argument("--choice-summary", required=True)
    parser.add_argument("inputs", nargs="+")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    df = read_inputs(Path(path) for path in args.inputs)

    normal = build_oracle(df, "normal_td_dijkstra")
    tdg_reroute = build_oracle(df, "tdg_impact_reroute")

    normal_output = Path(args.output_normal)
    tdg_output = Path(args.output_tdg_reroute)
    summary_output = Path(args.choice_summary)
    normal_output.parent.mkdir(parents=True, exist_ok=True)
    tdg_output.parent.mkdir(parents=True, exist_ok=True)
    summary_output.parent.mkdir(parents=True, exist_ok=True)

    normal.to_csv(normal_output, index=False)
    tdg_reroute.to_csv(tdg_output, index=False)
    write_choice_summary(pd.concat([normal, tdg_reroute], ignore_index=True), summary_output)

    print(f"wrote {normal_output} rows={len(normal)}")
    print(f"wrote {tdg_output} rows={len(tdg_reroute)}")
    print(f"wrote {summary_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
