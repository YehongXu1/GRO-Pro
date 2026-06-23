#!/usr/bin/env python3
"""Inspect IterR results in python/results/table1/iterr_*.csv.

For each cell file, prints per-seed iter trajectory and the seed-averaged
IterR best/Best and IterR last/Best ratios. Lightweight picker so we can
decide which workloads satisfy paper Table 1 targets:

  Low:    Local ~= Best, IterR_best/Best < 1.5x
  Normal: IterR_best/Best in [1.5x, 2x]
  High:   IterR_best/Best < 2.5x
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path
from statistics import mean


def read_iterr(path: Path):
    seeds = defaultdict(list)
    with path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            seed = row["seed"]
            iter_ = int(row["iteration"])
            ta = float(row["total_after"])
            tb = float(row["total_before"])
            seeds[seed].append((iter_, ta, tb))
    for seed in seeds:
        seeds[seed].sort()
    return seeds


def read_best_from_diag(city: str, S: int):
    diag = Path("python/results/diag_pilot") / f"diag_{city}_t1_S{S}.csv"
    if not diag.is_file():
        return None
    with diag.open() as f:
        for row in csv.DictReader(f):
            return float(row["avg_free_flow_tt"])  # seconds per query
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-dir", default="python/results/table1")
    parser.add_argument("--query-count", type=int, default=100000)
    args = parser.parse_args()

    results_dir = Path(args.results_dir)
    files = sorted(results_dir.glob("iterr_*.csv"))
    if not files:
        print("no iterr_*.csv files found")
        return 0

    rows = []
    for f in files:
        # parse filename: iterr_<city>_<cell>_S<S>.csv
        parts = f.stem.split("_")
        if len(parts) != 4:
            continue
        _, city, cell, S_tag = parts
        S = int(S_tag.lstrip("S"))
        seeds = read_iterr(f)
        if not seeds:
            continue
        best_secs_per_q = read_best_from_diag(city, S)
        best_min = (best_secs_per_q / 60.0) if best_secs_per_q else None
        bests = []
        locals_ = []
        for seed, traj in seeds.items():
            tas = [r[1] for r in traj]
            tbs = [r[2] for r in traj]
            bests.append(min(tas))
            locals_.append(tbs[0])  # total_before at iter 0
        iterr_best_min = (mean(bests) / args.query_count) / 60.0
        local_min = (mean(locals_) / args.query_count) / 60.0
        ratio_best = iterr_best_min / best_min if best_min else None
        ratio_local = local_min / best_min if best_min else None
        rows.append({
            "city": city, "cell": cell, "S": S,
            "seeds": len(seeds),
            "best_min": best_min,
            "local_min": local_min,
            "iterr_best_min": iterr_best_min,
            "ratio_local_to_best": ratio_local,
            "ratio_iterr_to_best": ratio_best,
        })

    # Print
    print(f"{'cell':<16} {'seeds':>5} {'Best':>8} {'Local':>10} {'IterR':>10} {'Local/B':>9} {'IterR/B':>9}")
    rows.sort(key=lambda r: (r["city"], r["S"]))
    for r in rows:
        cell = f"{r['city']}_{r['cell']}_S{r['S']}"
        def f(v, w=10, p=2):
            return f"{v:>{w}.{p}f}" if v is not None and v == v else f"{'-':>{w}}"
        print(
            f"{cell:<16} {r['seeds']:>5} "
            f"{f(r['best_min'], 8)} {f(r['local_min'])} {f(r['iterr_best_min'])} "
            f"{f(r['ratio_local_to_best'], 9, 2)+'x' if r['ratio_local_to_best'] else '-':>9} "
            f"{f(r['ratio_iterr_to_best'], 9, 2)+'x' if r['ratio_iterr_to_best'] else '-':>9}"
        )

    print()
    print("Paper Table 1 targets:")
    print("  Low:    IterR/B < 1.5x  AND  Local same level as Best")
    print("  Normal: IterR/B in [1.5x, 2x]")
    print("  High:   IterR/B < 2.5x")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
