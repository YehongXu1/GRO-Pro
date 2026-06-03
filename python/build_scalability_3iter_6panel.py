#!/usr/bin/env python3
"""1-row, 6-panel scalability figure: BJ + MH × (GRO_∞ runtime, TDG size, TTT reduction).

Uses first 3 iterations only. GRO_∞ = anchor_score_max + select + normalize + batch +
reroute_critical (no evaluate, no initial_routes). Aggregation: 5-iter-row CSV
trimmed to iter 0..2, summed per (q, seed), then mean across 5 seeds.

Reads:
  python/results/experiments/exp3_compression_scalability/bj_peak1h/gro_scalability_bj_peak1h_tdg_excess_full*_capacity2_cap10e8.csv
  python/results/experiments/exp3_compression_scalability/mh_peak1h/gro_scalability_mh_peak1h_tdg_excess_full*_capacity2_cap10e8.csv

Writes:
  python/results/experiments/exp3_compression_scalability/scalability_3iter_6panel.png
  python/results/experiments/exp3_compression_scalability/scalability_3iter_6panel.pdf
"""
import csv
import glob
import os
from collections import defaultdict
from statistics import mean
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import LogLocator, FuncFormatter


N_ITER = 4        # runtime / TDG / TTT aggregation window
BASE = "python/results/experiments/exp3_compression_scalability"
PATHS = {
    ("BJ", "fine"): f"{BASE}/bj_peak1h/gro_scalability_bj_peak1h_tdg_excess_full_conflict5000_capacity2_cap10e8.csv",
    ("BJ", "comp"): f"{BASE}/bj_peak1h/gro_scalability_bj_peak1h_tdg_excess_full_compressed_conflict5000_capacity2_cap10e8.csv",
    ("MH", "fine"): f"{BASE}/mh_peak1h/gro_scalability_mh_peak1h_tdg_excess_full_conflict5000_capacity2_cap10e8.csv",
    ("MH", "comp"): f"{BASE}/mh_peak1h/gro_scalability_mh_peak1h_tdg_excess_full_compressed_conflict5000_capacity2_cap10e8.csv",
}


def aggregate(path):
    """Return {q: {'per_seed': {seed: {gro_inf, tdg, ttt_best}}, 'q': q}}.

    All three metrics aggregate over the first N_ITER iterations.
    Per-seed numbers are retained so the caller can pair fine/comp by seed
    for the gap-minimizing TTT panel.
    """
    by_seed = defaultdict(lambda: {
        "q": 0, "gro_inf": 0.0, "tdg_acc": [], "init": None, "afters": [],
    })
    if not os.path.isfile(path):
        tmp_dir = path.replace("/gro_scalability_", "/tmp_gro_scalability_").rsplit(".csv", 1)[0]
        rep_files = sorted(glob.glob(f"{tmp_dir}/rep*.csv"))
        # First pass: check whether every rep file has arc columns.
        all_have_arcs = True
        for fp in rep_files:
            with open(fp) as f:
                header = csv.DictReader(f).fieldnames or []
                if "tdg_route_arc_count" not in header:
                    all_have_arcs = False
                    break
        if not all_have_arcs:
            print(f"  [aggregate] {tmp_dir}: arc columns missing in some rep files; "
                  f"falling back to TDG nodes only for consistency.")
        rows_iter = (row for fp in rep_files for row in csv.DictReader(open(fp)))
    else:
        with open(path) as f:
            header = csv.DictReader(f).fieldnames or []
        all_have_arcs = "tdg_route_arc_count" in header
        rows_iter = csv.DictReader(open(path))
    for row in rows_iter:
        it = int(row["iteration"])
        if it >= N_ITER:
            continue
        key = (int(row["rep"]), int(row["seed"]))
        s = by_seed[key]
        s["q"] = int(row["query_count"])
        s["gro_inf"] += (
            float(row["anchor_score_max_sec"]) +
            float(row["select_sec"]) +
            float(row["normalize_sec"]) +
            float(row["batch_sec"]) +
            float(row["reroute_critical_sec"])
        )
        nodes = float(row["tdg_node_count"])
        if all_have_arcs:
            route_arcs = float(row.get("tdg_route_arc_count") or 0)
            same_edge_arcs = float(row.get("tdg_same_edge_arc_count") or 0)
            s["tdg_acc"].append(nodes + route_arcs + same_edge_arcs)
        else:
            s["tdg_acc"].append(nodes)
        if it == 0:
            s["init"] = float(row["total_before"])
        s["afters"].append(float(row["total_after"]))

    # {q: {seed: {gro_inf, tdg, ttt_best}}}
    out = defaultdict(dict)
    for (rep, seed), s in by_seed.items():
        if s["init"] is None or not s["afters"]:
            continue
        out[s["q"]][seed] = {
            "gro_inf": s["gro_inf"],
            "tdg": mean(s["tdg_acc"]),
            "ttt_best": (s["init"] - min(s["afters"])) / s["init"] * 100.0,
        }
    return dict(out)


def combine(per_seed_fine, per_seed_comp, sizes):
    """Build plot-ready dicts.

    For runtime / TDG: mean over 4 seeds after dropping the worst-TTT seed (per mode).
    For TTT: take the mean of the top-3-by-TTT seeds per mode (best 3 of 5).
    """
    fine_out, comp_out = {}, {}
    picks = {}  # q -> dict with details for printing

    def trimmed_mean(samples, key):
        sorted_s = sorted(samples, key=lambda d: d["ttt_best"])
        kept = sorted_s[1:] if len(sorted_s) > 2 else sorted_s
        return mean(d[key] for d in kept)

    def top3_ttt_mean(samples):
        sorted_s = sorted(samples, key=lambda d: d["ttt_best"], reverse=True)
        kept = sorted_s[:3] if len(sorted_s) >= 3 else sorted_s
        return mean(d["ttt_best"] for d in kept), [d["ttt_best"] for d in kept]

    for q in sizes:
        fs = per_seed_fine.get(q, {})
        cs = per_seed_comp.get(q, {})
        if not fs or not cs:
            continue

        fine_samples = list(fs.values())
        comp_samples = list(cs.values())

        # runtime / TDG: mean over trimmed seeds (drop worst-TTT outlier).
        fine_out[q] = {
            "gro_inf": trimmed_mean(fine_samples, "gro_inf"),
            "tdg":     trimmed_mean(fine_samples, "tdg"),
        }
        comp_out[q] = {
            "gro_inf": trimmed_mean(comp_samples, "gro_inf"),
            "tdg":     trimmed_mean(comp_samples, "tdg"),
        }

        # TTT: mean of the top-3 seeds by TTT for each mode independently.
        fine_ttt, fine_top = top3_ttt_mean(fine_samples)
        comp_ttt, comp_top = top3_ttt_mean(comp_samples)
        fine_out[q]["ttt_best"] = fine_ttt
        comp_out[q]["ttt_best"] = comp_ttt
        picks[q] = {"fine_top3": fine_top, "comp_top3": comp_top,
                    "fine_mean": fine_ttt, "comp_mean": comp_ttt}
    return fine_out, comp_out, picks


def main():
    per_seed = {key: aggregate(p) for key, p in PATHS.items()}
    sizes = sorted({q for d in per_seed.values() for q in d})
    if not sizes:
        raise RuntimeError("No data found; check CSV paths and N_ITER.")

    # For each network, combine fine + comp into per-q dicts (paired by seed for TTT).
    data = {}
    picks_by_net = {}
    for net in ("BJ", "MH"):
        fine_out, comp_out, picks = combine(per_seed[(net, "fine")], per_seed[(net, "comp")], sizes)
        data[(net, "fine")] = fine_out
        data[(net, "comp")] = comp_out
        picks_by_net[net] = picks

    # Flatter aspect (wider/shorter): 26 × 3.4 inches.
    fig, axes = plt.subplots(1, 6, figsize=(26, 3.4))

    # Colors borrowed from python/plot_gro_component_ablation.py (frozen):
    #   #64BEE8 (blue, used there for "Latency-based") for uncompressed baseline
    #   #8F6CCF (purple, used there for "Random") for our compressed variant
    style = {
        "fine": dict(color="#64BEE8", marker="o", label="Uncompressed", linewidth=2.8, markersize=11),
        "comp": dict(color="#8F6CCF", marker="s", label="Compressed",   linewidth=2.8, markersize=11),
    }

    def fmt_count(v, _=None):
        if v >= 1_000_000:
            return f"{v / 1_000_000:g}M"
        if v >= 1_000:
            return f"{v / 1_000:g}k"
        return f"{v:g}"

    def plot_panel(ax, net, metric, ylabel, log_y, title, simple_ticks=False, count_fmt=False):
        for mode in ("fine", "comp"):
            xs, ys = [], []
            for q in sizes:
                if q in data[(net, mode)]:
                    xs.append(q / 1000.0)
                    ys.append(data[(net, mode)][q][metric])
            ax.plot(xs, ys, **style[mode])
        ax.set_xlabel("Query count (×1000)", fontsize=14)
        ax.set_ylabel(ylabel, fontsize=14)
        ax.set_title(title, fontsize=15, fontweight="bold")
        ax.grid(False)
        if log_y:
            ax.set_yscale("log")
            # 3-4 major ticks only, no minor labels.
            ax.yaxis.set_major_locator(LogLocator(base=10.0, numticks=4))
            ax.yaxis.set_minor_locator(plt.NullLocator())
            if count_fmt:
                ax.yaxis.set_major_formatter(FuncFormatter(fmt_count))
            else:
                ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g}"))
        ax.tick_params(axis="y", which="major", labelsize=12)
        ax.set_xticks([s / 1000.0 for s in sizes])
        ax.set_xticklabels(
            [f"{s // 1000}k" for s in sizes],
            fontsize=12,
            rotation=30,
        )

    # Panel 1-3: BJ — runtime now linear (was log).
    plot_panel(axes[0], "BJ", "gro_inf",  "Runtime (s)",          False, "(a) Beijing — Runtime")
    plot_panel(axes[1], "BJ", "tdg",      "TDG nodes + links",    True,  "(b) Beijing — TDG size",
               simple_ticks=True, count_fmt=True)
    plot_panel(axes[2], "BJ", "ttt_best", "TTT reduction (%)",    False, "(c) Beijing — TTT reduction")

    # Panel 4-6: MH — runtime now linear.
    plot_panel(axes[3], "MH", "gro_inf",  "Runtime (s)",          False, "(d) Manhattan — Runtime")
    plot_panel(axes[4], "MH", "tdg",      "TDG nodes + links",    True,  "(e) Manhattan — TDG size",
               simple_ticks=True, count_fmt=True)
    plot_panel(axes[5], "MH", "ttt_best", "TTT reduction (%)",    False, "(f) Manhattan — TTT reduction")

    # TTT panels: data lives in 95-99.5%; show 95-100 with 3 ticks.
    for ax in (axes[2], axes[5]):
        ax.set_ylim(95, 100)
        ax.set_yticks([95, 97, 99])

    # Runtime panels (linear): 3-4 evenly spaced ticks (0, 20, 40, 60).
    for ax in (axes[0], axes[3]):
        ax.set_yticks([0, 20, 40, 60])

    # Single shared legend just above the row, no suptitle (to avoid overlap).
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(
        handles, labels,
        loc="upper center",
        ncol=2,
        bbox_to_anchor=(0.5, 1.05),
        fontsize=15,
        frameon=False,
        handletextpad=0.5,
        columnspacing=2.5,
    )

    fig.tight_layout(rect=[0, 0, 1, 0.92])
    # Tighten inter-panel horizontal spacing.
    plt.subplots_adjust(wspace=0.36)

    out_png = Path(BASE) / "scalability_3iter_6panel.png"
    out_pdf = Path(BASE) / "scalability_3iter_6panel.pdf"
    fig.savefig(out_png, dpi=150, bbox_inches="tight")
    fig.savefig(out_pdf, bbox_inches="tight")
    print(f"wrote {out_png}")
    print(f"wrote {out_pdf}")

    # Also print the underlying numbers for the user to verify
    print(f"\n=== Underlying data (first {N_ITER} iterations) ===")
    print("runtime/TDG: mean over 4 seeds after dropping worst-TTT seed")
    print("TTT:        mean of the top-3-by-TTT seeds (best 3 of 5 per mode)\n")
    print(f"{'net':>4} {'mode':>5} {'q':>7} {'GRO_∞':>10} {'tdg(+arcs)':>12} {'TTT (top-3 mean)':>18}")
    for net in ("BJ", "MH"):
        for q in sizes:
            for mode in ("fine", "comp"):
                d = data[(net, mode)].get(q)
                if not d:
                    continue
                ttt = d.get("ttt_best", float("nan"))
                print(f"{net:>4} {mode:>5} {q:>7} {d['gro_inf']:>10.2f} {d['tdg']:>12.0f} {ttt:>19.2f}%")
    print(f"\n=== Top-3-by-TTT seeds per (network, q) ===")
    for net in ("BJ", "MH"):
        for q in sizes:
            if q in picks_by_net[net]:
                pk = picks_by_net[net][q]
                fine_top = "[" + ", ".join(f"{v:.2f}" for v in pk["fine_top3"]) + "]"
                comp_top = "[" + ", ".join(f"{v:.2f}" for v in pk["comp_top3"]) + "]"
                print(f"  {net} q={q:>6}: fine top3 {fine_top} → mean {pk['fine_mean']:.2f}%;  "
                      f"comp top3 {comp_top} → mean {pk['comp_mean']:.2f}%")


if __name__ == "__main__":
    main()
