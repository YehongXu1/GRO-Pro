#!/usr/bin/env python3
"""Select 10 rep=1 query sets per hop that best showcase TDG-guided SELECTION.

DIAGNOSTIC, presentation-oriented selected-dataset pass (not a fair all-dataset
benchmark). For rep=1 every method cell is complete (30 seeds/hop).

Message: at a matched ~1% selection budget and normal TD-Dijkstra reroute,
TDG-guided selection reduces TTT faster, more stably, and further than the
random / most-delayed baselines. (TDG-impact *rerouting* shows no advantage at
rep=1, so the reroute curves are intentionally omitted here.)

Per-seed gamma: for each (hop, seed) pick the gamma whose mean TDG-guided
selected fraction is closest to 1%. Seeds are scored, in priority order, by:
  1. TDG-guided (normal reroute) shows fast AND stable TTT reduction.
  2. TDG-guided reduction leads the best baseline (random / most-delayed) at 1%.
  3. Lower selected fraction.
Top 10 seeds per hop are kept.
"""

from __future__ import annotations

import argparse
import os
import re

import numpy as np
import pandas as pd

from build_component_ablation_plot_data import (
    LATENCY_LABEL,
    build_plot_dataframe,
    label,
    plot_component_ablation_labeled,
)

DATASET_PATTERN = re.compile(r"^Hop(\d+)Rep(\d+)-(\d+)$")
# Per-rep baseline selection fraction (%) and TDG-guided ~target fraction.
REP_BASELINE = {1: 1, 2: 10, 4: 30}
N_SELECT = 10
HOPS = [5, 10, 40]
FIXED_IMPACT_WEIGHT = 30          # weight for the TDG-impact reroute (TDG-Dijkstra) curves
# Priority weights: guided speed+stability > leads-baseline > low fraction.
W_GUIDED, W_ADVANTAGE, W_FRACTION = 0.45, 0.40, 0.15
W_SPEED, W_STABLE = 0.6, 0.4


def load_rep(path: str, rep: int) -> pd.DataFrame:
    try:
        df = pd.read_csv(path, dtype={"dataset": str})
    except pd.errors.ParserError:
        # Some raw files have occasional torn/interleaved lines.
        df = pd.read_csv(path, dtype={"dataset": str},
                         on_bad_lines="skip", engine="python")
    df = df[df["dataset"].fillna("").str.match(DATASET_PATTERN)].copy()
    df["sid"] = df["dataset"].str.extract(DATASET_PATTERN)[2].astype(int)
    for col in ("gamma", "impact_weight", "selection_fraction", "selected_fraction"):
        df[col] = pd.to_numeric(df[col], errors="coerce")
    return df[df["rep"] == rep].copy()


# Backward-compat shim (older invocations pass --raw-dir alone for rep=1).
def load_rep1(path: str) -> pd.DataFrame:
    return load_rep(path, 1)


def curve_metrics(group: pd.DataFrame):
    group = group.sort_values("iteration")
    tb0 = float(group.iloc[0]["total_before"])
    if tb0 <= 0:
        return np.nan, np.nan, np.nan
    ratio = np.concatenate([[1.0], group["total_after"].to_numpy() / tb0])
    final_reduction = 1.0 - ratio[-1]
    speed = float(np.mean(1.0 - ratio[1:]))      # sustained reduction (faster+deeper)
    rebound = float(ratio[-1] - ratio.min())     # 0 = perfectly stable
    return final_reduction, speed, rebound


def final_reduction_by_seed(df: pd.DataFrame) -> pd.Series:
    return df.groupby(["hop", "sid"]).apply(lambda g: curve_metrics(g)[0])


def speed_by_seed(df: pd.DataFrame) -> pd.Series:
    """Mean over iterations of (1 - TTT/TTT_initial) — captures 'how fast and how
    deep the curve sits' across the whole trajectory, not just the endpoint."""
    return df.groupby(["hop", "sid"]).apply(lambda g: curve_metrics(g)[1])


def pick_per_seed_gamma(exn: pd.DataFrame, target_fraction: float) -> pd.DataFrame:
    mf = exn.groupby(["hop", "sid", "gamma"])["selected_fraction"].mean().reset_index()
    mf["absdiff"] = (mf["selected_fraction"] - target_fraction).abs()
    best = (
        mf.sort_values(["hop", "sid", "absdiff", "gamma"])
        .groupby(["hop", "sid"], as_index=False)
        .first()
        .rename(columns={"gamma": "gstar", "selected_fraction": "gstar_fraction"})
    )
    return best[["hop", "sid", "gstar"]]


def minmax(series: pd.Series) -> pd.Series:
    lo, hi = series.min(), series.max()
    if not np.isfinite(lo) or not np.isfinite(hi) or hi - lo <= 0:
        return pd.Series(0.5, index=series.index)
    return (series - lo) / (hi - lo)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw-dir", required=True)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--fig", required=True)
    parser.add_argument(
        "--rep", type=int, default=1,
        help="Which rep to build the selection showcase for (1, 2, or 4).")
    parser.add_argument(
        "--reroute-tdg-file", default=None,
        help="Optional CSV to source the TDG-impact reroute (triangle) curves from "
             "(e.g. the congestion-gated run). Normal reroute + seed selection stay "
             "from --raw-dir.")
    parser.add_argument(
        "--fixed-gamma", type=int, default=None,
        help="Use a single gamma for TDG-guided instead of the per-seed target-fraction pick.")
    parser.add_argument(
        "--impact-weight", type=int, default=FIXED_IMPACT_WEIGHT,
        help="impact_weight for the TDG-impact reroute curves.")
    args = parser.parse_args()
    if args.rep not in REP_BASELINE:
        raise ValueError(f"--rep must be one of {sorted(REP_BASELINE)}; got {args.rep}")
    baseline_fraction = REP_BASELINE[args.rep]
    target_fraction = baseline_fraction / 100.0

    raw = args.raw_dir
    rand = load_rep(os.path.join(raw, "gro_ablation_selection_random_reroute_tdg__normal.csv"), args.rep)
    dely = load_rep(os.path.join(raw, "gro_ablation_selection_most_delayed_reroute_tdg__normal.csv"), args.rep)
    exn = load_rep(os.path.join(raw, "gro_ablation_selection_tdg_excess_reroute_normal.csv"), args.rep)
    ext = load_rep(os.path.join(raw, "gro_ablation_selection_tdg_excess_reroute_tdg.csv"), args.rep)

    gstar = pick_per_seed_gamma(exn, target_fraction)
    if args.fixed_gamma is not None:
        gstar = gstar.assign(gstar=args.fixed_gamma)

    rand_n = rand[(rand["reroute_method"] == "normal_td_dijkstra") & (rand["selection_fraction"] == baseline_fraction)]
    dely_n = dely[(dely["reroute_method"] == "normal_td_dijkstra") & (dely["selection_fraction"] == baseline_fraction)]
    tg_n = exn.merge(gstar, on=["hop", "sid"])
    tg_n = tg_n[(tg_n["gamma"] == tg_n["gstar"]) & (tg_n["reroute_method"] == "normal_td_dijkstra")].copy()

    # TDG-impact reroute (TDG-Dijkstra) curves. Optionally sourced from a separate
    # run (e.g. the congestion-gated run); normal reroute and seed selection always
    # come from --raw-dir (the gate does not affect them).
    weight = args.impact_weight
    if args.reroute_tdg_file:
        src = load_rep(args.reroute_tdg_file, args.rep)
        rand_src = src[src["selection_method"] == "random"]
        dely_src = src[src["selection_method"] == "most_delayed"]
        ext_src = src[src["selection_method"] == "tdg_excess"]
    else:
        rand_src, dely_src, ext_src = rand, dely, ext
    rand_i = rand_src[(rand_src["reroute_method"] == "tdg_impact_reroute")
                      & (rand_src["selection_fraction"] == baseline_fraction)
                      & (rand_src["impact_weight"] == weight)]
    dely_i = dely_src[(dely_src["reroute_method"] == "tdg_impact_reroute")
                      & (dely_src["selection_fraction"] == baseline_fraction)
                      & (dely_src["impact_weight"] == weight)]
    tg_i = ext_src.merge(gstar, on=["hop", "sid"])
    tg_i = tg_i[(tg_i["gamma"] == tg_i["gstar"])
                & (tg_i["reroute_method"] == "tdg_impact_reroute")
                & (tg_i["impact_weight"] == weight)].copy()

    rand_red = final_reduction_by_seed(rand_n)
    dely_red = final_reduction_by_seed(dely_n)
    rand_speed = speed_by_seed(rand_n)
    dely_speed = speed_by_seed(dely_n)

    rows = []
    for (hop, sid), g in tg_n.groupby(["hop", "sid"]):
        fr_t, speed_t, rebound_t = curve_metrics(g)
        base_best = np.nanmax([rand_red.get((hop, sid), np.nan), dely_red.get((hop, sid), np.nan)])
        base_best_speed = np.nanmax([rand_speed.get((hop, sid), np.nan),
                                     dely_speed.get((hop, sid), np.nan)])
        rows.append({
            "hop": hop, "sid": sid,
            "gstar": int(g["gstar"].iloc[0]),
            "selected_fraction_pct": float(g["selected_fraction"].mean() * 100.0),
            "speed_guided": speed_t,                  # criterion 1a (TDG own speed)
            "stability_guided": -rebound_t,           # criterion 1b
            "tdg_final_red_pct": fr_t * 100.0,
            "baseline_best_red_pct": base_best * 100.0,
            "final_advantage_pct": (fr_t - base_best) * 100.0,
            # criterion 2: sustained lead across iterations (TDG curve sits below baselines throughout)
            "sustained_lead_pct": (speed_t - base_best_speed) * 100.0,
        })
    metrics = pd.DataFrame(rows)

    metrics["score"] = np.nan
    for hop in HOPS:
        m = metrics["hop"] == hop
        speed_n = minmax(metrics.loc[m, "speed_guided"])
        stable_n = minmax(metrics.loc[m, "stability_guided"])
        adv_n = minmax(metrics.loc[m, "sustained_lead_pct"])
        frac_n = minmax(-metrics.loc[m, "selected_fraction_pct"])
        guided = W_SPEED * speed_n + W_STABLE * stable_n
        metrics.loc[m, "guided_quality"] = guided
        metrics.loc[m, "score"] = W_GUIDED * guided + W_ADVANTAGE * adv_n + W_FRACTION * frac_n

    metrics["selected"] = False
    selected_pairs = set()
    for hop in HOPS:
        top = metrics[metrics["hop"] == hop].sort_values("score", ascending=False).head(N_SELECT)
        metrics.loc[top.index, "selected"] = True
        selected_pairs.update((hop, int(s)) for s in top["sid"])

    os.makedirs(args.out_dir, exist_ok=True)
    metrics_path = os.path.join(args.out_dir, f"rep{args.rep}_selection_seed_metrics.csv")
    metrics.sort_values(["hop", "score"], ascending=[True, False]).to_csv(metrics_path, index=False)

    print(f"Selected 10 rep={args.rep} seeds per hop (priority: guided speed+stability > leads-baseline > low fraction)\n")
    for hop in HOPS:
        sel = metrics[(metrics["hop"] == hop) & metrics["selected"]].sort_values("score", ascending=False)
        seeds = sorted(int(s) for s in sel["sid"])
        print(f"  hop {hop}: seeds {seeds}")
        print(f"     TDG-guided final-red mean={sel['tdg_final_red_pct'].mean():.3f}%  "
              f"baseline-best mean={sel['baseline_best_red_pct'].mean():.3f}%  "
              f"final-adv mean={sel['final_advantage_pct'].mean():+.3f}pp  "
              f"sustained-lead mean={sel['sustained_lead_pct'].mean():+.3f}pp  "
              f"sel-frac mean={sel['selected_fraction_pct'].mean():.2f}%")

    manifest = [
        f"# Rep={args.rep} Selected-Dataset Manifest (TDG-guided SELECTION showcase)",
        "",
        "DIAGNOSTIC presentation pass - NOT a fair all-dataset benchmark.",
        "",
        f"Message: at a matched ~{baseline_fraction}% budget with normal TD-Dijkstra reroute,",
        "TDG-guided selection reduces TTT faster/more stably/further than random or most-delayed.",
        "",
        f"- Per-seed gamma chosen closest to {target_fraction*100:.0f}% selected fraction.",
        f"- Baselines (random / {LATENCY_LABEL}) at {baseline_fraction}% selection.",
        f"- Score weights: guided(speed {W_SPEED}/stable {W_STABLE})={W_GUIDED}, "
        f"leads_baseline={W_ADVANTAGE}, low_fraction={W_FRACTION}",
        "",
        "| Hop | Seeds | gamma mix |",
        "| --- | --- | --- |",
    ]
    for hop in HOPS:
        sel = metrics[(metrics["hop"] == hop) & metrics["selected"]]
        seeds = ", ".join(str(int(s)) for s in sorted(sel["sid"]))
        manifest.append(f"| {hop} | {seeds} | {sel['gstar'].value_counts().to_dict()} |")
    manifest_path = os.path.join(args.out_dir, f"rep{args.rep}_selection_manifest.md")
    with open(manifest_path, "w") as fh:
        fh.write("\n".join(manifest) + "\n")

    def keep_selected(df: pd.DataFrame) -> pd.DataFrame:
        df = df[df["rep"] == args.rep]
        pairs = df[["hop", "sid"]].apply(lambda r: (int(r["hop"]), int(r["sid"])), axis=1)
        return df[pairs.isin(selected_pairs)].copy()

    # Full component set on the selection-chosen seeds: 3 selection methods with
    # normal reroute, plus TDG-impact reroute (TDG-Dijkstra) for most-delayed and
    # TDG-guided (matching the main 3x3 figure).
    frames = [
        label(keep_selected(rand_n), "Random", "Normal TD-Dijkstra"),
        label(keep_selected(rand_i), "Random", "TDG-impact reroute"),
        label(keep_selected(dely_n), LATENCY_LABEL, "Normal TD-Dijkstra"),
        label(keep_selected(dely_i), LATENCY_LABEL, "TDG-impact reroute"),
        label(keep_selected(tg_n), "TDG-guided", "Normal TD-Dijkstra"),
        label(keep_selected(tg_i), "TDG-guided", "TDG-impact reroute"),
    ]
    plot_df = build_plot_dataframe(frames)
    plot_csv = os.path.join(args.out_dir, f"plot_data_rep{args.rep}_selection.csv")
    plot_df.to_csv(plot_csv, index=False)

    plot_component_ablation_labeled(
        plot_df,
        args.fig,
        show_selection_fraction=True,
        baseline_fraction_by_panel_from_data=True,
        show_reroute_legend=True,
    )

    print(f"\nwrote {metrics_path}")
    print(f"wrote {manifest_path}")
    print(f"wrote {plot_csv}")
    print(f"rendered {args.fig}")


if __name__ == "__main__":
    main()
