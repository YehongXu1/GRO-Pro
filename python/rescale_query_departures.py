#!/usr/bin/env python3
"""Rescale query departure times into a fixed time window.

The project query format is:

    origin_node destination_node departure_time_seconds

This script preserves OD pairs and relative departure ordering within each
query file, but maps the file's departure span linearly to [0, window_sec].
It is useful for building controlled peak-hour real workloads without folding
many days of trips by modulo.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Iterable


def iter_query_files(input_dir: Path, pattern: str) -> list[Path]:
    files = [path for path in input_dir.glob(pattern) if path.is_file()]
    files.sort()
    if not files:
        raise RuntimeError(f"No files matched {pattern} in {input_dir}")
    return files


def read_queries(path: Path) -> list[tuple[int, int, int]]:
    rows: list[tuple[int, int, int]] = []
    with path.open() as file:
        for line_number, line in enumerate(file, start=1):
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) < 3:
                raise RuntimeError(f"{path}:{line_number}: expected at least 3 columns")
            rows.append((int(parts[0]), int(parts[1]), int(parts[2])))
    if not rows:
        raise RuntimeError(f"{path}: empty query file")
    return rows


def rescale_rows(
    rows: Iterable[tuple[int, int, int]],
    window_sec: int,
) -> tuple[list[tuple[int, int, int]], int, int]:
    rows = list(rows)
    min_departure = min(row[2] for row in rows)
    max_departure = max(row[2] for row in rows)
    span = max_departure - min_departure
    if span <= 0:
        scaled = [(origin, destination, 0) for origin, destination, _ in rows]
    else:
        scaled = [
            (
                origin,
                destination,
                int(round((departure - min_departure) * window_sec / span)),
            )
            for origin, destination, departure in rows
        ]
    scaled.sort(key=lambda row: row[2])
    return scaled, min_departure, max_departure


def write_queries(path: Path, rows: Iterable[tuple[int, int, int]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as file:
        for origin, destination, departure in rows:
            file.write(f"{origin} {destination} {departure}\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--window-sec", type=int, required=True)
    parser.add_argument("--pattern", default="BJRealRep*.txt")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.window_sec <= 0:
        raise RuntimeError("--window-sec must be positive")

    input_dir = Path(args.input_dir)
    output_dir = Path(args.output_dir)
    files = iter_query_files(input_dir, args.pattern)
    summary = []
    for path in files:
        rows = read_queries(path)
        scaled, min_departure, max_departure = rescale_rows(rows, args.window_sec)
        output_path = output_dir / path.name
        write_queries(output_path, scaled)
        summary.append(
            {
                "dataset": path.stem,
                "query_count": len(rows),
                "input_min_departure": min_departure,
                "input_max_departure": max_departure,
                "input_departure_span_sec": max_departure - min_departure,
                "output_min_departure": min(row[2] for row in scaled),
                "output_max_departure": max(row[2] for row in scaled),
                "output_departure_span_sec": max(row[2] for row in scaled)
                - min(row[2] for row in scaled),
            }
        )
        print(
            f"wrote {output_path}: queries={len(rows)} "
            f"span={max_departure - min_departure}->{args.window_sec}"
        )

    metadata = {
        "source_query_dir": str(input_dir),
        "output_query_dir": str(output_dir),
        "pattern": args.pattern,
        "window_sec": args.window_sec,
        "method": "linear_rescale_per_file",
        "description": (
            "Departure times are mapped per file from the original min/max "
            "departure span to [0, window_sec], preserving relative order."
        ),
        "files": summary,
    }
    with (output_dir / "departure_rescale_metadata.json").open("w") as file:
        json.dump(metadata, file, indent=2)
    print(f"wrote {output_dir / 'departure_rescale_metadata.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
