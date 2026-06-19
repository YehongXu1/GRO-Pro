#!/usr/bin/env python3
"""Analyse the parameter sensitivity sweeps under the user's metrics:
- best TTT within the first 4 iterations (iter 0..3 inclusive)
- runtime under the infinite-thread reroute assumption, excluding traffic
  evaluation:
      runtime_inf = method_total_sec
                    - evaluate_before_sec
                    - evaluate_after_sec
                    - reroute_sec
                    + reroute_critical_sec

Per (param, value), aggregate the 5 dataset seeds:
- mean of (best total_after across iter 0..3 per dataset)
- mean of (cumulative runtime_inf at iter 3 per dataset) [= "4-iter cost"]
- mean of (cumulative runtime_inf at the best-TTT iter per dataset)
  [= "cost to reach the reported best"]

Usage:
    python python/analyze_sensitivity_local.py [param ...]
With no arguments, reports all four params.
"""
import csv
import glob
import statistics
import sys
from pathlib import Path

RAW = Path(
    "python/results/experiments/exp4_parameter_sensitivity/"
    "bj_real_scalability_peak1h_rep1_10k/raw"
)

# Truncate at this iteration index (inclusive). 4 iterations = 0..3.
MAX_ITER = 3


def runtime_inf_per_iter(row: dict) -> float:
    """Infinite-thread reroute runtime for a single iteration row, no TE."""
    return (
        float(row["method_total_sec"])
        - float(row["evaluate_before_sec"])
        - float(row["evaluate_after_sec"])
        - float(row["reroute_sec"])
        + float(row["reroute_critical_sec"])
    )


def load_param(param: str) -> list[dict]:
    pattern = str(RAW / f"gro_sensitivity_{param}_*_capacity2_cap10e8.csv")
    files = sorted(glob.glob(pattern))
    out = []
    for f in files:
        name = Path(f).stem
        # gro_sensitivity_<param>_<value>_capacity2_cap10e8
        value = int(name.split("_")[-3])
        with open(f) as fh:
            recs = list(csv.DictReader(fh))
        # group by dataset, sort by iteration
        by_ds: dict[str, list[dict]] = {}
        for r in recs:
            by_ds.setdefault(r["dataset"], []).append(r)
        for ds, rows in by_ds.items():
            rows.sort(key=lambda r: int(r["iteration"]))

        per_ds_best_ttt = []
        per_ds_cum_at_iter3 = []
        per_ds_cum_at_best = []
        per_ds_best_iter = []
        per_ds_initial_ttt = []
        for ds, rows in by_ds.items():
            truncated = [r for r in rows if int(r["iteration"]) <= MAX_ITER]
            if not truncated:
                continue
            per_ds_initial_ttt.append(float(truncated[0]["total_before"]))
            # cumulative runtime_inf over the truncated window (per iter)
            cum = 0.0
            cum_seq = []
            for r in truncated:
                cum += runtime_inf_per_iter(r)
                cum_seq.append(cum)
            # best TTT = min(total_after) across truncated iters
            ttt_seq = [float(r["total_after"]) for r in truncated]
            best_idx = min(range(len(ttt_seq)), key=lambda i: ttt_seq[i])
            per_ds_best_ttt.append(ttt_seq[best_idx])
            per_ds_best_iter.append(int(truncated[best_idx]["iteration"]))
            per_ds_cum_at_best.append(cum_seq[best_idx])
            per_ds_cum_at_iter3.append(cum_seq[-1])

        out.append(
            {
                "value": value,
                "n_datasets": len(per_ds_best_ttt),
                "mean_initial_ttt": statistics.mean(per_ds_initial_ttt),
                "mean_best_ttt_4iter": statistics.mean(per_ds_best_ttt),
                "mean_reduction_pct": (
                    statistics.mean(per_ds_initial_ttt)
                    - statistics.mean(per_ds_best_ttt)
                ) / statistics.mean(per_ds_initial_ttt) * 100,
                "mean_cum_inf_at_iter3": statistics.mean(per_ds_cum_at_iter3),
                "mean_cum_inf_at_best": statistics.mean(per_ds_cum_at_best),
                "mean_best_iter": statistics.mean(per_ds_best_iter),
            }
        )
    out.sort(key=lambda r: r["value"])
    return out


def fmt(rows, param):
    print(f"\n=== {param} sweep — best within first 4 iterations ===")
    print(
        f"{'val':>7} {'ds':>3} {'init_ttt':>14} {'best_ttt':>14} "
        f"{'red%':>6} {'best@':>6} {'rt_inf_4i':>10} {'rt_inf_best':>11}"
    )
    for r in rows:
        print(
            f"{r['value']:>7} {r['n_datasets']:>3} "
            f"{r['mean_initial_ttt']:>14.0f} {r['mean_best_ttt_4iter']:>14.0f} "
            f"{r['mean_reduction_pct']:>6.2f} {r['mean_best_iter']:>6.2f} "
            f"{r['mean_cum_inf_at_iter3']:>10.2f} {r['mean_cum_inf_at_best']:>11.2f}"
        )


def main():
    params = sys.argv[1:] or ["epsilon", "conflict", "gamma", "impact"]
    for p in params:
        rows = load_param(p)
        if not rows:
            print(f"WARN: no CSVs found for {p}")
            continue
        fmt(rows, p)


if __name__ == "__main__":
    main()
