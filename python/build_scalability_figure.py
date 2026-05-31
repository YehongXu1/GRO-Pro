#!/usr/bin/env python3
"""Generate the 4-subplot scalability figure for BJ peak1h: pure-GRO runtime
under infinite parallelism, TTT reduction (best-of-3), TDG size (nodes),
and estimated TDG memory.

Reads:
  python/results/experiments/exp3_compression_scalability/bj_peak1h/tmp_.../rep*.csv

Writes:
  python/results/experiments/exp3_compression_scalability/bj_peak1h/scalability_4panel.png
  python/results/experiments/exp3_compression_scalability/bj_peak1h/scalability_4panel.pdf
  python/results/experiments/exp3_compression_scalability/bj_peak1h/scalability_4panel.csv
"""
import os
import glob

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


BJ = "python/results/experiments/exp3_compression_scalability/bj_peak1h"
OUT_DIR = BJ
DIRS = {
    "full":       f"{BJ}/tmp_gro_scalability_bj_peak1h_tdg_excess_full_conflict5000_capacity2_cap10e8",
    "compressed": f"{BJ}/tmp_gro_scalability_bj_peak1h_tdg_excess_full_compressed_conflict5000_capacity2_cap10e8",
}

# Breakdown anchors at 10k/50k/100k from logs/reroute_breakdown OMP=24, sum over 5 iters.
# T_d (ms) = rq_ms/q * 24  (sequential single-Dijkstra wall, upper bound).
BD_TD = {
    (10000,  "compressed"): 112.41, (10000,  "full"): 66.30,
    (50000,  "compressed"): 172.75, (50000,  "full"): 126.97,
    (100000, "compressed"): 130.93, (100000, "full"): 84.28,
}
# parallel fraction of reroute_sec = reroute_query / reroute_total
BD_RQFRAC = {
    (10000,  "compressed"): 93.12 / 93.63,   (10000,  "full"): 54.79 / 58.47,
    (50000,  "compressed"): 223.46 / 228.72, (50000,  "full"): 304.26 / 457.45,
    (100000, "compressed"): 567.33 / 582.27, (100000, "full"): 535.22 / 937.52,
}
ANCHOR_SIZES = [10000, 50000, 100000]


def interp(target, anchors_sizes, anchors_vals):
    pts = sorted(zip(anchors_sizes, anchors_vals))
    if target <= pts[0][0]:
        return pts[0][1]
    if target >= pts[-1][0]:
        return pts[-1][1]
    for i in range(len(pts) - 1):
        x0, y0 = pts[i]
        x1, y1 = pts[i + 1]
        if x0 <= target <= x1:
            return y0 + (y1 - y0) * (target - x0) / (x1 - x0)
    return pts[-1][1]


# Per-TDG-node memory estimate (bytes). Rough breakdown:
#   TDGNode struct                  ~ 32 B
#   edge_timelines map<Time,Event>  ~ 80 B per entry  (Time + Event + RB-tree overhead)
#   key_to_node_id unordered_map    ~ 40 B per entry
#   route_outgoing/incoming         ~ 24..48 B (small vectors, avg low degree)
# Total ~ 180..200 B per TDG node. Use 200 B as a conservative paper-ready number.
BYTES_PER_NODE = 200


def load():
    frames = []
    for m, d in DIRS.items():
        for fp in sorted(glob.glob(f"{d}/rep*.csv")):
            if os.path.getsize(fp) == 0:
                continue
            df = pd.read_csv(fp)
            df["method"] = m
            frames.append(df)
    A = pd.concat(frames, ignore_index=True)
    A = A[A.iteration <= 4]
    return A


def annotate_inf(row):
    m, q = row["method"], row["query_count"]
    if (q, m) in BD_TD:
        td = BD_TD[(q, m)]
        rqf = BD_RQFRAC[(q, m)]
    else:
        td = interp(q, ANCHOR_SIZES, [BD_TD[(s, m)] for s in ANCHOR_SIZES])
        rqf = interp(q, ANCHOR_SIZES, [BD_RQFRAC[(s, m)] for s in ANCHOR_SIZES])
    inf_dij_sec = row.batch_count * td / 1000.0
    seq_sec = row.reroute_sec * (1.0 - rqf)
    return pd.Series({"inf_reroute_sec": inf_dij_sec + seq_sec})


def summarize(A):
    A2 = A.copy()
    A2[["inf_reroute_sec"]] = A.apply(annotate_inf, axis=1)
    # per-seed sums over 5 iters, best-of-3 TTT
    rows = []
    for (m, q, ds), g in A2.groupby(["method", "query_count", "dataset"]):
        g = g.sort_values("iteration")
        init = g.total_before.iloc[0]
        after = g.total_after.tolist()
        red_best3 = 100.0 * (init - min(after[:3])) / init
        rows.append(dict(
            method=m, size=q, dataset=ds,
            red_best3=red_best3,
            inf_reroute=g.inf_reroute_sec.sum(),
            sel=g.select_sec.sum(),
            tdg_prep=g.tdg_prepare_sec.sum(),
            norm=g.normalize_sec.sum(),
            batch_alg=g.batch_sec.sum(),
            tdg_nodes=g.tdg_node_count.mean(),
            tdg_timelines=g.tdg_edge_timeline_count.mean(),
        ))
    P = pd.DataFrame(rows)
    P["pure_gro_inf"] = P.inf_reroute + P.sel + P.tdg_prep + P.norm + P.batch_alg
    P["mem_mb"] = P.tdg_nodes * BYTES_PER_NODE / 1e6
    return P


def aggregate(P):
    S = P.groupby(["method", "size"]).agg(
        runtime_mean=("pure_gro_inf", "mean"),
        runtime_std=("pure_gro_inf", "std"),
        reroute_mean=("inf_reroute", "mean"),
        reroute_std=("inf_reroute", "std"),
        ttt_mean=("red_best3", "mean"),
        ttt_std=("red_best3", "std"),
        nodes_mean=("tdg_nodes", "mean"),
        nodes_std=("tdg_nodes", "std"),
        mem_mean=("mem_mb", "mean"),
        mem_std=("mem_mb", "std"),
    ).reset_index().sort_values(["size", "method"])
    return S


def plot(S, out_png, out_pdf):
    style = {
        "full":       dict(color="#1f77b4", marker="o", label="Uncompressed (full)"),
        "compressed": dict(color="#d62728", marker="s", label="Compressed"),
    }
    fig, axes = plt.subplots(1, 4, figsize=(18, 4.2))
    sizes_k_lookup = lambda series: (series / 1000.0).values

    # 1) Reroute runtime (infinite-parallel)
    ax = axes[0]
    for m, st in style.items():
        sub = S[S.method == m].sort_values("size")
        ax.errorbar(sizes_k_lookup(sub["size"]), sub.reroute_mean,
                    yerr=sub.reroute_std, capsize=3,
                    color=st["color"], marker=st["marker"], label=st["label"],
                    linewidth=2, markersize=7)
    ax.set_yscale("log")
    ax.set_xlabel("Query count (×1000)")
    ax.set_ylabel("Reroute runtime (s, log scale)")
    ax.set_title("Reroute runtime (infinite parallel)")
    ax.grid(True, which="both", alpha=0.3)

    # 2) TTT reduction (best-of-3)
    ax = axes[1]
    for m, st in style.items():
        sub = S[S.method == m].sort_values("size")
        ax.errorbar(sizes_k_lookup(sub["size"]), sub.ttt_mean,
                    yerr=sub.ttt_std, capsize=3,
                    color=st["color"], marker=st["marker"], label=st["label"],
                    linewidth=2, markersize=7)
    ax.set_xlabel("Query count (×1000)")
    ax.set_ylabel("TTT reduction (%, best-of-3)")
    ax.set_title("Solution quality (best-of-3 iter)")
    ax.set_ylim(85, 100.5)
    ax.grid(True, alpha=0.3)

    # 3) TDG size (node count)
    ax = axes[2]
    for m, st in style.items():
        sub = S[S.method == m].sort_values("size")
        ax.errorbar(sizes_k_lookup(sub["size"]), sub.nodes_mean / 1e6,
                    yerr=sub.nodes_std / 1e6, capsize=3,
                    color=st["color"], marker=st["marker"], label=st["label"],
                    linewidth=2, markersize=7)
    ax.set_yscale("log")
    ax.set_xlabel("Query count (×1000)")
    ax.set_ylabel("TDG node count (millions, log)")
    ax.set_title("TDG size")
    ax.grid(True, which="both", alpha=0.3)

    # 4) Memory footprint (estimated)
    ax = axes[3]
    for m, st in style.items():
        sub = S[S.method == m].sort_values("size")
        ax.errorbar(sizes_k_lookup(sub["size"]), sub.mem_mean,
                    yerr=sub.mem_std, capsize=3,
                    color=st["color"], marker=st["marker"], label=st["label"],
                    linewidth=2, markersize=7)
    ax.set_yscale("log")
    ax.set_xlabel("Query count (×1000)")
    ax.set_ylabel(f"TDG memory (MB, log; ~{BYTES_PER_NODE} B/node)")
    ax.set_title("Memory footprint (estimated)")
    ax.grid(True, which="both", alpha=0.3)

    # single legend on top
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=2,
               bbox_to_anchor=(0.5, 1.02), frameon=False, fontsize=11)
    fig.tight_layout(rect=[0, 0, 1, 0.97])
    fig.savefig(out_png, dpi=160, bbox_inches="tight")
    fig.savefig(out_pdf, bbox_inches="tight")
    print(f"[wrote] {out_png}")
    print(f"[wrote] {out_pdf}")


def main():
    A = load()
    P = summarize(A)
    S = aggregate(P)
    csv_path = os.path.join(OUT_DIR, "scalability_4panel.csv")
    S.round(4).to_csv(csv_path, index=False)
    print(f"[wrote] {csv_path}")
    plot(S,
         os.path.join(OUT_DIR, "scalability_4panel.png"),
         os.path.join(OUT_DIR, "scalability_4panel.pdf"))
    # print short summary
    pd.set_option("display.width", 220)
    pd.set_option("display.max_columns", 20)
    show = S[["method", "size", "reroute_mean", "ttt_mean", "nodes_mean", "mem_mean"]]
    print("\n=== plotted values ===")
    print(show.round(2).to_string(index=False))


if __name__ == "__main__":
    main()
