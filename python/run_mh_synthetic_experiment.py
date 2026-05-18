#!/usr/bin/env python3
import argparse
import csv
import re
import subprocess
import sys
import time
from pathlib import Path


TOTAL_RE = re.compile(
    r"^TOTAL_TRAVEL_TIME,iteration=([^,]+),query_count=([^,]+),value=([^,]+)")
GRO_RUN_RE = re.compile(
    r"^RUN,iteration=-1,query_count=([^,]+),time_sec=([^,]+)")
GRO_CANDIDATE_RE = re.compile(
    r"^CANDIDATE,iteration=([^,]+),candidate_count=([^,]+),candidate_sec=([^,]+)")
GRO_SELECT_RE = re.compile(
    r"^SELECT,iteration=([^,]+),candidate_count=([^,]+),selected_count=([^,]+),"
    r"select_impact_sec=([^,]+),select_rank_sec=([^,]+),select_scan_sec=([^,]+),"
    r"select_remove_sec=([^,]+),select_total_sec=([^,]+)")
GRO_BATCH_RE = re.compile(
    r"^BATCH,iteration=([^,]+),selected_count=([^,]+),batch_count=([^,]+),batch_queries_sec=([^,]+)")
GRO_REROUTE_RE = re.compile(
    r"^REROUTE,iteration=([^,]+),reroute_query_count=([^,]+),reroute_query_sec=([^,]+),"
    r"insert_trajectory_sec=([^,]+),reroute_total_sec=([^,]+)")
BASELINE_RE = re.compile(
    r"^BASELINE,iteration=([^,]+),selected_count=([^,]+),reroute_count=([^,]+),"
    r"total_travel_time=([^,]+),evaluate_sec=([^,]+),reroute_sec=([^,]+),iteration_sec=([^,]+)")
BASELINE_FINAL_RE = re.compile(
    r"^BASELINE_FINAL,total_travel_time=([^,]+),evaluate_sec=([^,]+),run_sec=([^,]+)")


CSV_FIELDS = [
    "method",
    "dataset",
    "hop",
    "rep",
    "seed",
    "iteration",
    "total_travel_time",
    "candidate_count",
    "selected_count",
    "reroute_count",
    "batch_count",
    "evaluate_sec",
    "candidate_sec",
    "select_sec",
    "batch_sec",
    "reroute_sec",
    "iteration_sec",
    "run_sec",
]


def parse_int(value):
    return int(float(value))


def parse_gro_log(text, method, dataset, hop, rep, seed):
    rows = {}
    run_sec = ""

    def row_for(iteration):
        return rows.setdefault(iteration, {
            "method": method,
            "dataset": dataset,
            "hop": hop,
            "rep": rep,
            "seed": seed,
            "iteration": iteration,
            "total_travel_time": "",
            "candidate_count": "",
            "selected_count": "",
            "reroute_count": "",
            "batch_count": "",
            "evaluate_sec": "",
            "candidate_sec": "",
            "select_sec": "",
            "batch_sec": "",
            "reroute_sec": "",
            "iteration_sec": "",
            "run_sec": "",
        })

    for line in text.splitlines():
        line = line.strip()

        match = TOTAL_RE.match(line)
        if match:
            iteration = parse_int(match.group(1))
            row_for(iteration)["total_travel_time"] = match.group(3)
            continue

        match = GRO_CANDIDATE_RE.match(line)
        if match:
            row = row_for(parse_int(match.group(1)))
            row["candidate_count"] = match.group(2)
            row["candidate_sec"] = match.group(3)
            continue

        match = GRO_SELECT_RE.match(line)
        if match:
            row = row_for(parse_int(match.group(1)))
            row["candidate_count"] = match.group(2)
            row["selected_count"] = match.group(3)
            row["select_sec"] = match.group(8)
            continue

        match = GRO_BATCH_RE.match(line)
        if match:
            row = row_for(parse_int(match.group(1)))
            row["selected_count"] = match.group(2)
            row["batch_count"] = match.group(3)
            row["batch_sec"] = match.group(4)
            continue

        match = GRO_REROUTE_RE.match(line)
        if match:
            row = row_for(parse_int(match.group(1)))
            row["reroute_count"] = match.group(2)
            row["reroute_sec"] = match.group(5)
            continue

        match = GRO_RUN_RE.match(line)
        if match:
            run_sec = match.group(2)

    if -1 in rows:
        rows[-1]["run_sec"] = run_sec
    return [rows[key] for key in sorted(rows)]


def parse_baseline_log(text, method, dataset, hop, rep, seed):
    rows = {}
    final_total = ""
    final_run_sec = ""

    for line in text.splitlines():
        line = line.strip()

        match = BASELINE_RE.match(line)
        if match:
            iteration = parse_int(match.group(1))
            rows[iteration] = {
                "method": method,
                "dataset": dataset,
                "hop": hop,
                "rep": rep,
                "seed": seed,
                "iteration": iteration,
                "total_travel_time": match.group(4),
                "candidate_count": "",
                "selected_count": match.group(2),
                "reroute_count": match.group(3),
                "batch_count": "",
                "evaluate_sec": match.group(5),
                "candidate_sec": "",
                "select_sec": "",
                "batch_sec": "",
                "reroute_sec": match.group(6),
                "iteration_sec": match.group(7),
                "run_sec": "",
            }
            continue

        match = BASELINE_FINAL_RE.match(line)
        if match:
            final_total = match.group(1)
            final_run_sec = match.group(3)

    if final_total:
        rows[-1] = {
            "method": method,
            "dataset": dataset,
            "hop": hop,
            "rep": rep,
            "seed": seed,
            "iteration": -1,
            "total_travel_time": final_total,
            "candidate_count": "",
            "selected_count": "",
            "reroute_count": "",
            "batch_count": "",
            "evaluate_sec": "",
            "candidate_sec": "",
            "select_sec": "",
            "batch_sec": "",
            "reroute_sec": "",
            "iteration_sec": "",
            "run_sec": final_run_sec,
        }

    return [rows[key] for key in sorted(rows)]


def write_config(path, repo, query_file, max_iterations):
    path.write_text(
        "\n".join([
            f"graph_path: {repo / 'data' / 'MH.txt'}",
            f"coordinates_path: {repo / 'data' / 'MH_NodeIDLonLat.txt'}",
            f"queries_path: {query_file}",
            "alpha: 0.15",
            "beta: 4.0",
            "max_time: 3600",
            f"max_iterations: {max_iterations}",
            "enable_timing_log: true",
            "lambda: 0.45",
            "gamma: 0.5",
            "impact_weight: 0.15",
            "theta_percentile: 0.90",
            "min_slot_width: 600",
            "",
        ]),
        encoding="utf-8",
    )


def run_one(repo, output_dir, method, hop, rep, seed, max_iterations):
    dataset = f"Hop{hop}Rep{rep}-{seed}"
    query_file = repo / "data" / "MH_Synthetic_query_sets" / f"{dataset}.txt"
    executable = repo / ("gro_test" if method == "gro" else "gro_baseline_test")
    config_path = output_dir / "configs" / f"{method}_{dataset}.yaml"
    log_path = output_dir / "logs" / f"{method}_{dataset}.log"

    if not query_file.exists():
        raise FileNotFoundError(query_file)
    if not executable.exists():
        raise FileNotFoundError(executable)

    write_config(config_path, repo, query_file, max_iterations)
    command = [str(executable), str(config_path)]

    started = time.time()
    print(f"[run] {method} {dataset}", flush=True)
    completed = subprocess.run(
        command,
        cwd=repo,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    elapsed = time.time() - started
    log_path.write_text(completed.stdout, encoding="utf-8")

    if completed.returncode != 0:
        print(completed.stdout, file=sys.stderr)
        raise RuntimeError(f"{method} {dataset} failed with exit code {completed.returncode}")

    if method == "gro":
        rows = parse_gro_log(completed.stdout, method, dataset, hop, rep, seed)
    else:
        rows = parse_baseline_log(completed.stdout, method, dataset, hop, rep, seed)

    print(f"[done] {method} {dataset} wall_sec={elapsed:.2f}", flush=True)
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default=Path(__file__).resolve().parents[1], type=Path)
    parser.add_argument("--output-dir", default=Path("tmp/mh_synthetic_results"), type=Path)
    parser.add_argument("--hops", nargs="+", type=int, default=[10, 20, 40])
    parser.add_argument("--reps", nargs="+", type=int, default=[1, 2, 4])
    parser.add_argument("--seeds", nargs="+", type=int, default=[0])
    parser.add_argument("--methods", nargs="+", choices=["gro", "gro_baseline"], default=["gro", "gro_baseline"])
    parser.add_argument("--max-iterations", type=int, default=10)
    parser.add_argument("--append", action="store_true")
    args = parser.parse_args()

    repo = args.repo.resolve()
    output_dir = args.output_dir.resolve()
    (output_dir / "configs").mkdir(parents=True, exist_ok=True)
    (output_dir / "logs").mkdir(parents=True, exist_ok=True)
    csv_path = output_dir / "mh_synthetic_results.csv"

    all_rows = []
    for seed in args.seeds:
        for hop in args.hops:
            for rep in args.reps:
                for method in args.methods:
                    all_rows.extend(run_one(repo, output_dir, method, hop, rep, seed, args.max_iterations))

    mode = "a" if args.append and csv_path.exists() else "w"
    with csv_path.open(mode, newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=CSV_FIELDS)
        if mode == "w":
            writer.writeheader()
        writer.writerows(all_rows)

    print(f"[csv] {csv_path}")


if __name__ == "__main__":
    main()
