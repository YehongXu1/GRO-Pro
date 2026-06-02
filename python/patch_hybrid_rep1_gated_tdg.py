#!/usr/bin/env python3
"""Patch the hybrid 3x3 plot-data CSV so rep=1's Batched TDG-Dijkstra curves
(triangle markers) come from raw_gated80/ (gate=80, sealed config) instead of
the original ungated DIAGNOSTIC oracle. Normal TD-Dijkstra (circle) curves and
the rep=2/rep=4 panels are left untouched.

For rep=1 / Batched TDG-Dijkstra:
  - Random + Batched TDG-Dij     <- gated, selection_fraction=1, impact_weight=30
  - Most-delayed + Batched TDG-Dij <- gated, selection_fraction=1, impact_weight=30
  - TDG-guided + Batched TDG-Dij <- gated, per-(dataset,iteration) oracle across
                                    gamma=25 x {impact_weight=10, 30}
"""

from __future__ import annotations

import os
import pandas as pd

from build_component_ablation_plot_data import (
    LATENCY_LABEL,
    TDG_REROUTE_LABEL,
    label,
    load_clean,
    tdg_best_param,
    plot_component_ablation_labeled,
)
from plot_gro_component_ablation import build_plot_dataframe

ROOT = "python/results/experiments/exp1_component_ablation/bj_synthetic_capacity2_cap10e8"
GATED_CSV = os.path.join(ROOT, "raw_gated80",
                         "gro_ablation_selection_random__most_delayed__tdg_excess_reroute_tdg_rep1.csv")
HYBRID_IN = os.path.join(ROOT, "analysis",
                         "plot_data_hybrid_rep1diag_rep2priority_rep4diag.csv")
HYBRID_OUT = os.path.join(ROOT, "analysis",
                          "plot_data_hybrid_rep1diag_gated_rep2priority_rep4diag.csv")
FIG_PNG = os.path.join(ROOT, "plots",
                       "bj_component_ablation_hybrid_rep1diag_gated_rep2_1x6x3_rep4diag.png")
FIG_PDF = os.path.join(ROOT, "plots",
                       "bj_component_ablation_hybrid_rep1diag_gated_rep2_1x6x3_rep4diag.pdf")
OVERLEAF_PDF = ("/Users/xyh/Desktop/GRO-Paper-Workspace/1_Paper_Overleaf/figures/"
                "bj_component_ablation_hybrid_rep1diag_gated_rep2_1x6x3_rep4diag.pdf")

REP1_BASELINE_FRACTION = 1   # rep=1 -> 1% selection
REP1_IMPACT_WEIGHT = 30      # baselines + latency reroute use weight=30


def build_rep1_gated_tdg_frames(gated_csv: str) -> list[pd.DataFrame]:
    df = load_clean(gated_csv)
    # All gated rows are rep=1, reroute=tdg_impact_reroute already.
    assert set(df["rep"].unique()) == {1}, df["rep"].unique()
    assert set(df["reroute_method"].unique()) == {"tdg_impact_reroute"}

    rand = df[(df["selection_method"] == "random")
              & (df["selection_fraction"] == REP1_BASELINE_FRACTION)
              & (df["impact_weight"] == REP1_IMPACT_WEIGHT)]
    dely = df[(df["selection_method"] == "most_delayed")
              & (df["selection_fraction"] == REP1_BASELINE_FRACTION)
              & (df["impact_weight"] == REP1_IMPACT_WEIGHT)]
    ext = df[(df["selection_method"] == "tdg_excess")]

    frames = [
        label(rand, "Random", TDG_REROUTE_LABEL),
        label(dely, LATENCY_LABEL, TDG_REROUTE_LABEL),
        label(tdg_best_param(ext, "tdg_impact_reroute"), "TDG-guided", TDG_REROUTE_LABEL),
    ]
    for f in frames:
        if f.empty:
            raise SystemExit(f"empty frame in gated rep=1 build")
    return frames


def main() -> None:
    print(f"Loading hybrid plot-data: {HYBRID_IN}")
    hybrid = pd.read_csv(HYBRID_IN)
    print(f"  rows={len(hybrid)}")

    print(f"Building gated rep=1 TDG-Dij curves from: {GATED_CSV}")
    gated_frames = build_rep1_gated_tdg_frames(GATED_CSV)
    gated_plot = build_plot_dataframe(gated_frames)
    # Only keep rep=1 / Batched TDG-Dij rows (sanity).
    gated_plot = gated_plot[(gated_plot["rep"] == 1)
                            & (gated_plot["reroute_label"] == TDG_REROUTE_LABEL)].copy()
    print(f"  gated rows={len(gated_plot)}")
    print(gated_plot.groupby(["hop", "selection_label"]).size().rename("rows"))

    # Drop the existing rep=1 / Batched TDG-Dij rows from the hybrid, then append.
    keep_mask = ~((hybrid["rep"] == 1) & (hybrid["reroute_label"] == TDG_REROUTE_LABEL))
    dropped = (~keep_mask).sum()
    print(f"  dropping {dropped} existing rep=1 Batched TDG-Dij rows from hybrid")
    patched = pd.concat([hybrid[keep_mask], gated_plot], ignore_index=True)
    patched = patched.sort_values(
        ["rep", "hop", "selection_label", "reroute_label", "plot_iteration"]
    ).reset_index(drop=True)
    patched.to_csv(HYBRID_OUT, index=False)
    print(f"Wrote patched plot-data -> {HYBRID_OUT}  rows={len(patched)}")

    print("Rendering PNG ...")
    plot_component_ablation_labeled(
        patched, FIG_PNG,
        show_selection_fraction=True,
        baseline_fraction_by_panel_from_data=True,
    )
    print(f"  -> {FIG_PNG}")

    print("Rendering PDF ...")
    plot_component_ablation_labeled(
        patched, FIG_PDF,
        show_selection_fraction=True,
        baseline_fraction_by_panel_from_data=True,
    )
    print(f"  -> {FIG_PDF}")

    if os.path.isdir(os.path.dirname(OVERLEAF_PDF)):
        import shutil
        shutil.copy2(FIG_PDF, OVERLEAF_PDF)
        print(f"Copied PDF -> {OVERLEAF_PDF}")
    else:
        print(f"(Overleaf dir not present, skipped copy: {OVERLEAF_PDF})")


if __name__ == "__main__":
    main()
