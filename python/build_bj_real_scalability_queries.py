#!/usr/bin/env python3
"""Build BJ real scalability query sets from base Rep1 files.

The output keeps the original OD and departure-time distribution of each base
set, then repeats every base query `rep` times to create controlled query-count
scales. This is intended for scalability experiments, not as a new raw-data
source.
"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Iterable, List, Tuple


QueryRow = Tuple[int, int, int]


def parse_int_list(text: str) -> List[int]:
    values: List[int] = []
    for item in text.split(","):
        item = item.strip()
        if item:
            values.append(int(item))
    return values


def read_queries(path: Path) -> List[QueryRow]:
    rows: List[QueryRow] = []
    with path.open() as file:
        for line_number, line in enumerate(file, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) != 3:
                raise ValueError(f"Unexpected query format at {path}:{line_number}")
            rows.append((int(parts[0]), int(parts[1]), int(parts[2])))
    return rows


def write_queries(path: Path, rows: Iterable[QueryRow]) -> int:
    count = 0
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as file:
        for origin, destination, departure in rows:
            file.write(f"{origin} {destination} {departure}\n")
            count += 1
    return count


def repeated_rows(base_rows: List[QueryRow], rep: int) -> Iterable[QueryRow]:
    for row in base_rows:
        for _ in range(rep):
            yield row


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--rep-values", default="1,2,3,5,7,10")
    parser.add_argument("--seeds", default="0,1,2,3,4")
    parser.add_argument(
        "--source-label",
        default="bj_real",
        help="Human-readable source label for metadata.",
    )
    args = parser.parse_args()

    input_dir = Path(args.input_dir)
    output_dir = Path(args.output_dir)
    rep_values = parse_int_list(args.rep_values)
    seeds = parse_int_list(args.seeds)
    if not rep_values:
        raise ValueError("rep-values must not be empty")
    if not seeds:
        raise ValueError("seeds must not be empty")

    output_dir.mkdir(parents=True, exist_ok=True)
    summary_rows = []

    for seed in seeds:
        base_path = input_dir / f"BJRealRep1-{seed}.txt"
        if not base_path.exists():
            raise FileNotFoundError(base_path)
        base_rows = read_queries(base_path)
        if not base_rows:
            raise ValueError(f"Empty base query file: {base_path}")

        min_departure = min(row[2] for row in base_rows)
        max_departure = max(row[2] for row in base_rows)
        for rep in rep_values:
            output_path = output_dir / f"BJRealRep{rep}-{seed}.txt"
            query_count = write_queries(output_path, repeated_rows(base_rows, rep))
            summary_rows.append(
                {
                    "dataset": output_path.stem,
                    "source_file": str(base_path),
                    "rep": rep,
                    "seed": seed,
                    "base_query_count": len(base_rows),
                    "query_count": query_count,
                    "min_departure": min_departure,
                    "max_departure": max_departure,
                    "departure_span_sec": max_departure - min_departure,
                }
            )
            print(f"wrote {output_path}: {query_count} queries")

    with (output_dir / "scalability_summary.csv").open("w", newline="") as file:
        fieldnames = [
            "dataset",
            "source_file",
            "rep",
            "seed",
            "base_query_count",
            "query_count",
            "min_departure",
            "max_departure",
            "departure_span_sec",
        ]
        writer = csv.DictWriter(file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(summary_rows)

    metadata = {
        "source_label": args.source_label,
        "input_dir": str(input_dir),
        "output_dir": str(output_dir),
        "derivation": (
            "Each BJRealRep1 seed file is used as the 10k base set. "
            "Every base query is repeated exactly rep times; OD and departure "
            "time are unchanged."
        ),
        "rep_values": rep_values,
        "seeds": seeds,
        "query_counts": {f"Rep{rep}": rep * 10000 for rep in rep_values},
        "intended_role": "GRO/TDG scalability experiment datasets",
    }
    with (output_dir / "metadata.json").open("w") as file:
        json.dump(metadata, file, indent=2)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
