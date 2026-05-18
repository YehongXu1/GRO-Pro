#!/usr/bin/env python3
"""Run GRO experiments on MH synthetic query sets.

The script runs the C++ executables, captures their logs, and writes a compact
CSV that can be plotted by display_mh_synthetic.py.
"""

from __future__ import annotations

import argparse
import csv
import re
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / "data" / "MH_Synthetic_query_sets"
RESULT_DIR = ROOT / "python" / "results"
LOG_DIR = RESULT_DIR / "logs"

DEFAULT_HOPS = [10, 20, 40]
DEFAULT_REPS = [1, 2, 4]

ALGORITHMS = {
    "gro": ROOT / "gro_test",
    "baseline": ROOT / "gro_baseline_test",
}


def parse_key_values(line: str) -> tuple[str, dict[str, str]]:
    parts = [part.strip() for part in line.strip().split(",")]
    if not parts:
        return "", {}
    label = parts[0]
    values: dict[str, str] = {}
    for part in parts[1:]:
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        values[key.strip()] = value.strip()
    return label, values


def dataset_name(hop: int, rep: int, seed: int) -> str:
    return f"Hop{hop}Rep{rep}-{seed}"


def write_config(query_path: Path, max_iterations: int) -> Path:
    config = tempfile.NamedTemporaryFile(
        mode="w",
        suffix=".yaml",
        prefix="mh_synthetic_",
        dir=RESULT_DIR,
        delete=False,
    )
    with config:
        config.write(f"graph_path: {ROOT / 'data' / 'MH.txt'}\n")
        config.write(f"coordinates_path: {ROOT / 'data' / 'MH_NodeIDLonLat.txt'}\n")
        config.write(f"queries_path: {query_path}\n")
        config.write("alpha: 0.15\n")
        config.write("beta: 4.0\n")
        config.write("max_time: 3600\n")
        config.write(f"max_iterations: {max_iterations}\n")
        config.write("enable_timing_log: true\n")
        config.write("lambda: 0.45\n")
        config.write("gamma: 0.5\n")
        config.write("impact_weight: 0.15\n")
        config.write("theta_percentile: 0.90\n")
        config.write("min_slot_width: 600\n")
    return Path(config.name)


def run_one(
    algorithm: str,
    hop: int,
    rep: int,
    seed: int,
    max_iterations: int,
) -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    query_path = DATA_DIR / f"{dataset_name(hop, rep, seed)}.txt"
    if not query_path.exists():
        raise FileNotFoundError(query_path)

    executable = ALGORITHMS[algorithm]
    if not executable.exists():
        raise FileNotFoundError(f"Missing executable: {executable}")

    config_path = write_config(query_path, max_iterations)
    name = dataset_name(hop, rep, seed)
    log_path = LOG_DIR / f"{name}_{algorithm}.log"

    series_rows: list[dict[str, str]] = []
    event_rows: list[dict[str, str]] = []

    try:
        print(f"[run] {algorithm} {name}", flush=True)
        with log_path.open("w") as log_file:
            process = subprocess.Popen(
                [str(executable), str(config_path)],
                cwd=ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
            assert process.stdout is not None
            for line in process.stdout:
                print(line, end="", flush=True)
                log_file.write(line)
                label, values = parse_key_values(line)
                if not label:
                    continue

                base = {
                    "algorithm": algorithm,
                    "dataset": name,
                    "hop": str(hop),
                    "rep": str(rep),
                    "seed": str(seed),
                    "label": label,
                }

                if label == "TOTAL_TRAVEL_TIME":
                    iteration = values.get("iteration", "")
                    plot_iteration = str(max_iterations) if iteration == "-1" else iteration
                    series_rows.append({
                        **base,
                        "iteration": plot_iteration,
                        "total_travel_time": values.get("value", ""),
                    })
                elif label == "BASELINE":
                    series_rows.append({
                        **base,
                        "iteration": values.get("iteration", ""),
                        "total_travel_time": values.get("total_travel_time", ""),
                    })
                elif label == "BASELINE_FINAL":
                    series_rows.append({
                        **base,
                        "iteration": str(max_iterations),
                        "total_travel_time": values.get("total_travel_time", ""),
                    })
                elif label in {
                    "INITIAL_ROUTES",
                    "EVALUATE",
                    "TDG",
                    "IMPACT",
                    "CANDIDATE",
                    "SELECT",
                    "BATCH",
                    "REROUTE",
                    "ITERATION",
                    "RUN",
                    "BASELINE_INITIAL",
                    "BASELINE",
                    "BASELINE_FINAL",
                }:
                    event_rows.append({**base, **values})

            return_code = process.wait()
            if return_code != 0:
                raise RuntimeError(f"{algorithm} {name} failed with exit code {return_code}")
    finally:
        config_path.unlink(missing_ok=True)

    return series_rows, event_rows


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    if not rows:
        return
    keys: list[str] = []
    seen = set()
    for row in rows:
        for key in row:
            if key not in seen:
                seen.add(key)
                keys.append(key)

    with path.open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--max-iterations", type=int, default=10)
    parser.add_argument("--seeds", type=int, nargs="+", default=[0])
    parser.add_argument("--hops", type=int, nargs="+", default=DEFAULT_HOPS)
    parser.add_argument("--reps", type=int, nargs="+", default=DEFAULT_REPS)
    parser.add_argument(
        "--algorithms",
        nargs="+",
        choices=sorted(ALGORITHMS),
        default=["gro", "baseline"],
    )
    parser.add_argument(
        "--output-prefix",
        default="mh_synthetic_seed0",
        help="CSV prefix under python/results.",
    )
    args = parser.parse_args()

    RESULT_DIR.mkdir(parents=True, exist_ok=True)
    LOG_DIR.mkdir(parents=True, exist_ok=True)

    all_series: list[dict[str, str]] = []
    all_events: list[dict[str, str]] = []

    for seed in args.seeds:
        for hop in args.hops:
            for rep in args.reps:
                for algorithm in args.algorithms:
                    series, events = run_one(
                        algorithm=algorithm,
                        hop=hop,
                        rep=rep,
                        seed=seed,
                        max_iterations=args.max_iterations,
                    )
                    all_series.extend(series)
                    all_events.extend(events)

                    write_csv(RESULT_DIR / f"{args.output_prefix}.csv", all_series)
                    write_csv(RESULT_DIR / f"{args.output_prefix}_events.csv", all_events)

    print(f"[done] wrote {RESULT_DIR / f'{args.output_prefix}.csv'}")
    print(f"[done] wrote {RESULT_DIR / f'{args.output_prefix}_events.csv'}")


if __name__ == "__main__":
    main()
