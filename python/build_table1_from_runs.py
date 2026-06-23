#!/usr/bin/env python3
"""Build paper Table 1 (a) Avg Travel Time and (b) Total Optimization Time
from python/results/table1/ CSVs.

Per cell we expect three CSV families:
  iterr_<city>_<cell>_S<S>.csv     - IterR Random phi=10% trajectory
  gro_<city>_<cell>_S<S>.csv       - GRO (tdg_excess + tdg) trajectory
  baselines_<city>_<cell>_S<S>.csv - SVP / GOR / SOR / FAHL one-shot results

It averages across seeds and reports:
  Local (avg total_after / |Q| at iter 0 -- after one BPR evaluation of
         shortest-path routes; this is the unoptimised TTT)
  Best  (avg free_flow / |Q|)
  IterR (min total_after across iter 0..K-1 / |Q|, averaged across seeds)
  GRO   (min total_after across iter 0..K-1 / |Q|, averaged across seeds)
  SVP/GOR/SOR/FAHL (total_travel_time / |Q| from the one-shot run)
"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from collections import defaultdict
from statistics import mean


def read_iterr_or_gro(path: Path):
    if not path.is_file():
        return None
    seeds = defaultdict(list)
    with path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            seed = row["seed"]
            iter_ = int(row["iteration"])
            ta = float(row["total_after"])
            tb = float(row["total_before"])
            ms = float(row.get("method_total_sec", 0) or 0)
            seeds[seed].append((iter_, ta, tb, ms))
    return seeds


def read_baselines(path: Path):
    if not path.is_file():
        return {}
    by_method = defaultdict(list)
    with path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            method = row.get("method", "?").lower()
            try:
                tt = float(row["total_travel_time"])
            except (KeyError, ValueError):
                continue
            sec = float(row.get("method_total_sec", 0) or 0)
            by_method[method].append((tt, sec))
    return by_method


def per_query_minutes(total_sec: float, query_count: int) -> float:
    return (total_sec / query_count) / 60.0 if query_count else float("nan")


def summarize_cell(results_dir: Path, city: str, cell: str, S: int, query_count: int = 100000):
    out = {"city": city, "cell": cell, "S": S}
    iterr_path = results_dir / f"iterr_{city}_{cell}_S{S}.csv"
    gro_path = results_dir / f"gro_{city}_{cell}_S{S}.csv"
    bl_path = results_dir / f"baselines_{city}_{cell}_S{S}.csv"

    iterr_seeds = read_iterr_or_gro(iterr_path)
    gro_seeds = read_iterr_or_gro(gro_path)

    if iterr_seeds:
        # Local = iter 0 total_before (since pre-iter eval shows the
        # unoptimised TTT). best = min(total_after across iters).
        local_seeds = []
        free_flow_seeds = []
        best_seeds = []
        time_seeds = []
        for seed, rows in iterr_seeds.items():
            rows.sort()
            ta_list = [r[1] for r in rows]
            tb_list = [r[2] for r in rows]
            best_seeds.append(min(ta_list))
            local_seeds.append(tb_list[0])
            time_seeds.append(sum(r[3] for r in rows))
        out["iterr_best_min"] = per_query_minutes(mean(best_seeds), query_count)
        out["iterr_time_sec"] = mean(time_seeds)
        out["local_min"] = per_query_minutes(mean(local_seeds), query_count)
    else:
        out["iterr_best_min"] = float("nan")
        out["iterr_time_sec"] = float("nan")
        out["local_min"] = float("nan")

    if gro_seeds:
        best_seeds = []
        time_seeds = []
        for seed, rows in gro_seeds.items():
            rows.sort()
            ta_list = [r[1] for r in rows]
            best_seeds.append(min(ta_list))
            time_seeds.append(sum(r[3] for r in rows))
        out["gro_best_min"] = per_query_minutes(mean(best_seeds), query_count)
        out["gro_time_sec"] = mean(time_seeds)
    else:
        out["gro_best_min"] = float("nan")
        out["gro_time_sec"] = float("nan")

    bl_methods = read_baselines(bl_path)
    for method in ("svp", "gor", "sor", "fahl"):
        rows = bl_methods.get(method, [])
        if rows:
            tts = [r[0] for r in rows]
            secs = [r[1] for r in rows]
            out[f"{method}_min"] = per_query_minutes(mean(tts), query_count)
            out[f"{method}_time_sec"] = mean(secs)
        else:
            out[f"{method}_min"] = float("nan")
            out[f"{method}_time_sec"] = float("nan")

    # Best (free-flow) — approximated via the shortest-path diagnostic CSV;
    # if missing, leave nan.
    out["best_min"] = float("nan")
    diag = Path("python/results/diag_pilot") / f"diag_{city}_t1_S{S}.csv"
    if diag.is_file():
        try:
            with diag.open() as f:
                reader = csv.DictReader(f)
                for row in reader:
                    ff = float(row["avg_free_flow_tt"])
                    out["best_min"] = ff / 60.0
                    break
        except Exception:
            pass
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-dir", default="python/results/table1")
    parser.add_argument(
        "--cells",
        default=(
            "bj:low:10000,bj:normal:5000,bj:high:2000,"
            "mh:low:50000,mh:normal:10000,mh:high:1000"
        ),
        help="Comma list of city:cell:S triples.",
    )
    parser.add_argument("--query-count", type=int, default=100000)
    parser.add_argument("--output-json", default="python/results/table1/summary.json")
    args = parser.parse_args()

    rows = []
    for triple in args.cells.split(","):
        city, cell, S = triple.split(":")
        rows.append(summarize_cell(Path(args.results_dir), city, cell, int(S), args.query_count))

    print(f"{'Cell':<24} {'Best':>7} {'Local':>9} {'IterR':>9} {'GRO':>9} {'SVP':>9} {'GOR':>9} {'SOR':>9} {'FAHL':>9}")
    for r in rows:
        cell = f"{r['city']}_{r['cell']}_S{r['S']}"
        def f(k):
            v = r.get(k, float("nan"))
            return f"{v:>9.2f}" if v == v else f"{'-':>9}"
        b = f("best_min")
        L = f("local_min")
        I = f("iterr_best_min")
        G = f("gro_best_min")
        s = f("svp_min"); o = f("gor_min"); so = f("sor_min"); fa = f("fahl_min")
        print(f"{cell:<24} {b:>7} {L} {I} {G} {s} {o} {so} {fa}")

    Path(args.output_json).parent.mkdir(parents=True, exist_ok=True)
    with Path(args.output_json).open("w") as f:
        json.dump(rows, f, indent=2)
    print(f"\nWrote {args.output_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
