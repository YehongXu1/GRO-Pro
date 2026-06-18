#!/usr/bin/env python3
"""Render the hybrid component-ablation figure without the rep=1 (displayed as
"Rep #: 0") panels — final figure is 2x3 covering rep=2 (Rep #: 1) and rep=4
(Rep #: 3). Reuses the patched gated hybrid plot-data (rep=1 rows are dropped
on load, so the rep=1 data source no longer matters)."""

from __future__ import annotations

import os
import shutil

import pandas as pd

from build_component_ablation_plot_data import plot_component_ablation_labeled

ROOT = "python/results/experiments/exp1_component_ablation/bj_synthetic_capacity2_cap10e8"
PLOT_DATA_IN = os.path.join(ROOT, "analysis",
                            "plot_data_hybrid_rep1diag_gated_rep2priority_rep4diag.csv")
PLOT_DATA_OUT = os.path.join(ROOT, "analysis",
                             "plot_data_hybrid_rep2priority_rep4diag.csv")
FIG_PNG = os.path.join(ROOT, "plots",
                       "bj_component_ablation_hybrid_rep2_1x6x3_rep4diag.png")
FIG_PDF = os.path.join(ROOT, "plots",
                       "bj_component_ablation_hybrid_rep2_1x6x3_rep4diag.pdf")
OVERLEAF_PDF = ("/Users/xyh/Desktop/GRO-Paper-Workspace/1_Paper_Overleaf/figures/"
                "bj_component_ablation_hybrid_rep2_1x6x3_rep4diag.pdf")


def main() -> None:
    df = pd.read_csv(PLOT_DATA_IN)
    before = len(df)
    df = df[df["rep"] != 1.0].reset_index(drop=True)
    print(f"Dropped rep=1 rows: {before} -> {len(df)}")
    panels = df[["rep", "hop"]].drop_duplicates().sort_values(["rep", "hop"])
    print(f"Remaining panels: {len(panels)}")
    print(panels.to_string(index=False))

    os.makedirs(os.path.dirname(PLOT_DATA_OUT), exist_ok=True)
    df.to_csv(PLOT_DATA_OUT, index=False)
    print(f"Wrote plot-data -> {PLOT_DATA_OUT}")

    for out in (FIG_PNG, FIG_PDF):
        plot_component_ablation_labeled(
            df, out,
            show_selection_fraction=True,
            baseline_fraction_by_panel_from_data=True,
        )
        print(f"  -> {out}")

    if os.path.isdir(os.path.dirname(OVERLEAF_PDF)):
        shutil.copy2(FIG_PDF, OVERLEAF_PDF)
        print(f"Copied PDF -> {OVERLEAF_PDF}")


if __name__ == "__main__":
    main()
