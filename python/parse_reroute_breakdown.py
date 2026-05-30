#!/usr/bin/env python3
"""Parse stderr timing lines from scripts/run_reroute_breakdown.sh logs.

For each log <mode>_<size>_<dataset>.log, extract SELECT / CANDIDATE / BATCH /
REROUTE / TDG / IMPACT / EVALUATE per-iteration times and produce:

  reroute_breakdown.csv  -- one row per (method, size, dataset, iteration)
  printed to stdout      -- per-iteration table, totals over iterations,
                            and reroute sub-share (% of reroute_total)

Override defaults via env vars: LOG_DIR, OUT_CSV.
"""
import glob
import os
import re
import sys

import pandas as pd

LOG_DIR = os.environ.get("LOG_DIR", "logs/reroute_breakdown")
OUT_CSV = os.environ.get("OUT_CSV", "reroute_breakdown.csv")

# breakdown_<mode>_<size>_<dataset>.log
FN_RE = re.compile(r"breakdown_(?P<mode>fine|compressed)_(?P<size>\d+k)_(?P<dataset>[A-Za-z0-9-]+)\.log$")
KV_RE = re.compile(r"(\w+)=([0-9.eE+\-]+)")


def parse_line(line: str):
    """Return (tag, dict_of_floats) or (None, None)."""
    head, _, _ = line.partition(",")
    head = head.strip()
    if head not in ("REROUTE", "BATCH", "SELECT", "CANDIDATE",
                    "TDG", "IMPACT", "EVALUATE"):
        return None, None
    d = {k: float(v) for k, v in KV_RE.findall(line)}
    return head, d


def main():
    rows = []
    for path in sorted(glob.glob(os.path.join(LOG_DIR, "*.log"))):
        m = FN_RE.search(os.path.basename(path))
        if not m:
            print(f"skip (name): {path}", file=sys.stderr)
            continue
        mode = m.group("mode")
        size = m.group("size")
        dataset = m.group("dataset")

        by_iter = {}
        with open(path) as f:
            for line in f:
                tag, d = parse_line(line)
                if tag is None:
                    continue
                it = int(d.get("iteration", -1))
                if it < 0:
                    # skip non-iteration timings (initial_routes / final_evaluate / run_total)
                    continue
                slot = by_iter.setdefault(it, {})
                if tag == "REROUTE":
                    slot["reroute_query"]      = d.get("reroute_query_sec", 0.0)
                    slot["insert_trajectory"]  = d.get("insert_trajectory_sec", 0.0)
                    slot["recompute_impact"]   = d.get("recompute_impact_sec", 0.0)
                    slot["reroute_total"]      = d.get("reroute_total_sec", 0.0)
                    slot["reroute_query_count"] = int(d.get("reroute_query_count", 0))
                elif tag == "BATCH":
                    slot["batching"]           = d.get("batch_queries_sec", 0.0)
                    slot["batch_count"]        = int(d.get("batch_count", 0))
                    slot["selected_count"]     = int(d.get("selected_count", 0))
                elif tag == "SELECT":
                    slot["select_total"]       = d.get("select_total_sec", 0.0)
                elif tag == "CANDIDATE":
                    slot["candidate"]          = d.get("candidate_sec", 0.0)
                elif tag == "TDG":
                    # build_tdg or compress_tdg
                    slot["tdg_build"] = slot.get("tdg_build", 0.0) + d.get("time_sec", 0.0)
                elif tag == "IMPACT":
                    slot["compute_impact"]     = d.get("time_sec", 0.0)
                elif tag == "EVALUATE":
                    slot["evaluate"] = slot.get("evaluate", 0.0) + d.get("time_sec", 0.0)

        for it, sub in by_iter.items():
            rows.append({
                "method": mode, "size": size, "dataset": dataset, "iteration": it,
                **sub,
            })

    if not rows:
        print("No timing lines parsed. Check that runs used the timing-on config "
              "(config/config_bj_capacity2_cap10e8_iter5_timing.yaml).", file=sys.stderr)
        sys.exit(1)

    df = pd.DataFrame(rows)
    for c in ("batching", "reroute_query", "insert_trajectory",
              "recompute_impact", "reroute_total", "select_total",
              "candidate", "tdg_build", "compute_impact", "evaluate"):
        if c not in df.columns:
            df[c] = 0.0
    df["iteration"] = df["iteration"].astype(int)
    # impose a consistent size ordering
    size_order = {"10k": 0, "20k": 1, "30k": 2, "50k": 3, "70k": 4, "100k": 5}
    df["_so"] = df["size"].map(size_order).fillna(99)
    df = df.sort_values(["method", "_so", "dataset", "iteration"]).drop(columns="_so")
    df.to_csv(OUT_CSV, index=False)

    pd.set_option("display.width", 220)
    pd.set_option("display.max_columns", 40)

    print(f"\n=== per-iteration breakdown ({OUT_CSV}) ===")
    cols_iter = ["method", "size", "dataset", "iteration",
                 "batch_count", "selected_count", "reroute_query_count",
                 "batching", "reroute_query", "insert_trajectory",
                 "recompute_impact", "reroute_total"]
    print(df[cols_iter].round(3).to_string(index=False))

    print("\n=== totals over iterations (per method, size) ===")
    tot_cols = ["batching", "reroute_query", "insert_trajectory",
                "recompute_impact", "reroute_total"]
    tot = df.groupby(["method", "size"], sort=False)[tot_cols].sum().round(2)
    print(tot.to_string())

    print("\n=== reroute sub-share (% of reroute_total) ===")
    share = tot.div(tot["reroute_total"].replace(0, float("nan")), axis=0)
    share = share.drop(columns=["reroute_total"]).mul(100.0).round(1)
    print(share.to_string())

    print("\n=== context: select/candidate/tdg_build/compute_impact/evaluate per (method, size) ===")
    ctx_cols = ["select_total", "candidate", "tdg_build", "compute_impact", "evaluate"]
    ctx = df.groupby(["method", "size"], sort=False)[ctx_cols].sum().round(2)
    print(ctx.to_string())


if __name__ == "__main__":
    main()
