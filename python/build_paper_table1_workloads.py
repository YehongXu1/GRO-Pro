#!/usr/bin/env python3
"""Build paper Table 1 workloads with W=1h fixed, intensity varied via OD distribution.

Input: a coarse_candidates CSV with columns
    origin, destination, departure_abs_seconds, taxi_id, duration_seconds, haversine_km

For each seed, this script:
  1. Deterministically shuffles the candidate list.
  2. Picks the first S unique OD pairs (S = source_count for this cell).
  3. Repeats each picked OD pair (Q // S) times and pads to Q.
  4. Rescales the picked candidates' departures linearly into [0, window_sec].
  5. Writes BJRealRep10-{seed}.txt (or with custom prefix).

Intensity is controlled by (S, copies_per_source = Q/S). Smaller S +
larger copies = more concentrated demand = heavier congestion.
"""

from __future__ import annotations

import argparse
import csv
import json
import random
from pathlib import Path
from typing import List, Tuple

QueryRow = Tuple[int, int, int]


def load_candidates(path: Path) -> List[Tuple[int, int, int]]:
    """Load candidates from a CSV (with header origin/destination/departure_*)
    or a plain whitespace-separated text file (origin destination departure).
    """
    rows: List[Tuple[int, int, int]] = []
    with path.open() as file:
        first = file.readline()
        if first.startswith("origin"):
            file.seek(0)
            reader = csv.DictReader(file)
            for row in reader:
                origin = int(row["origin"])
                destination = int(row["destination"])
                dep_col = (
                    row.get("departure_abs_seconds")
                    or row.get("departure_seconds")
                    or row.get("departure")
                )
                dep = int(float(dep_col))
                rows.append((origin, destination, dep))
        else:
            file.seek(0)
            for line in file:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split()
                if len(parts) < 3:
                    continue
                rows.append((int(parts[0]), int(parts[1]), int(parts[2])))
    return rows


def load_candidates_multi(paths: List[Path]) -> List[Tuple[int, int, int]]:
    rows: List[Tuple[int, int, int]] = []
    for path in paths:
        rows.extend(load_candidates(path))
    return rows


def rescale_departures(rows: List[QueryRow], window_sec: int) -> List[QueryRow]:
    if not rows:
        return rows
    min_dep = min(r[2] for r in rows)
    max_dep = max(r[2] for r in rows)
    span = max(1, max_dep - min_dep)
    return [
        (o, d, int(round((t - min_dep) * window_sec / span)))
        for o, d, t in rows
    ]


def amplify(rows: List[QueryRow], query_count: int) -> List[QueryRow]:
    n = len(rows)
    base = query_count // n
    extra = query_count % n
    out: List[QueryRow] = []
    for i, (o, d, t) in enumerate(rows):
        copies = base + (1 if i < extra else 0)
        for _ in range(copies):
            out.append((o, d, t))
    assert len(out) == query_count
    out.sort(key=lambda r: r[2])
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--candidates-csv",
        required=True,
        help=(
            "Comma-separated list of candidate files. Each is either a CSV "
            "(header includes 'origin', 'destination', and a departure column) "
            "or a plain 'origin destination departure' text file. Multiple "
            "files are concatenated."
        ),
    )
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--dataset-prefix", default="BJReal")
    parser.add_argument("--seeds", default="0,1,2,3,4")
    parser.add_argument("--source-count", type=int, required=True,
                        help="Number of unique OD pairs sampled per seed.")
    parser.add_argument("--query-count", type=int, default=100000)
    parser.add_argument("--window-sec", type=int, default=3600,
                        help="Departure window after linear rescale.")
    parser.add_argument("--base-scale", type=int, default=10000,
                        help="rep label = query_count / base_scale.")
    parser.add_argument("--random-seed", type=int, default=20260623)
    args = parser.parse_args()

    seeds = [int(s) for s in args.seeds.split(",") if s.strip()]
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    candidate_paths = [
        Path(p.strip()) for p in args.candidates_csv.split(",") if p.strip()
    ]
    candidates = load_candidates_multi(candidate_paths)
    if args.source_count > len(candidates):
        raise ValueError(
            f"source_count={args.source_count} > available candidates={len(candidates)}"
        )
    rep = args.query_count // args.base_scale

    summary = []
    for seed in seeds:
        rng = random.Random(args.random_seed + seed * 1000003)
        shuffled = list(range(len(candidates)))
        rng.shuffle(shuffled)
        picked_indices = shuffled[:args.source_count]
        picked = [candidates[i] for i in picked_indices]
        picked = rescale_departures(picked, args.window_sec)
        rows = amplify(picked, args.query_count)
        out_path = out_dir / f"{args.dataset_prefix}Rep{rep}-{seed}.txt"
        with out_path.open("w") as fh:
            for o, d, t in rows:
                fh.write(f"{o} {d} {t}\n")
        unique_od = len({(o, d) for o, d, _ in picked})
        summary.append({
            "dataset": out_path.stem,
            "seed": seed,
            "query_count": len(rows),
            "source_count": args.source_count,
            "unique_od_count": unique_od,
            "copies_per_source": args.query_count // args.source_count,
            "window_sec": args.window_sec,
        })
        print(
            f"wrote {out_path}: queries={len(rows)} source={args.source_count} "
            f"copies={args.query_count // args.source_count} window={args.window_sec}s"
        )

    meta = {
        "candidates_csv": [str(p) for p in candidate_paths],
        "dataset_prefix": args.dataset_prefix,
        "source_count": args.source_count,
        "query_count": args.query_count,
        "window_sec": args.window_sec,
        "seeds": seeds,
        "random_seed": args.random_seed,
        "summary": summary,
    }
    with (out_dir / "metadata.json").open("w") as fh:
        json.dump(meta, fh, indent=2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
