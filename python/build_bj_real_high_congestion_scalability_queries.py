#!/usr/bin/env python3
"""Build high-congestion BJ real-derived scalability query sets.

The source OD rows are the T-Drive-derived BJRealRep1 seed files. For each
target query size, this builder samples a seed-specific subset of source rows
and repeats each selected row a fixed number of times. This keeps OD pairs and
departure times grounded in the real query extraction while making even the
smallest scalability workloads congested enough to evaluate route optimization.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple


QueryRow = Tuple[int, int, int]
Coordinate = Tuple[float, float]


@dataclass(frozen=True)
class SizeSpec:
    query_count: int
    source_count: int
    od_center_radius_km: float
    departure_window_sec: int
    jitter_sec: int


def parse_int_list(text: str) -> List[int]:
    values: List[int] = []
    for item in text.split(","):
        item = item.strip()
        if item:
            values.append(int(item))
    return values


def parse_size_specs(text: str) -> List[SizeSpec]:
    specs: List[SizeSpec] = []
    if not text.strip():
        return specs
    for item in text.split(","):
        item = item.strip()
        if not item:
            continue
        parts = item.split(":")
        if len(parts) not in {4, 5}:
            raise ValueError(
                "--size-specs entries must be "
                "query_count:source_count:od_radius_km:window_sec[:jitter_sec]"
            )
        jitter_sec = int(parts[4]) if len(parts) == 5 else 0
        specs.append(
            SizeSpec(
                query_count=int(parts[0]),
                source_count=int(parts[1]),
                od_center_radius_km=float(parts[2]),
                departure_window_sec=int(parts[3]),
                jitter_sec=jitter_sec,
            )
        )
    return specs


def read_queries(path: Path) -> List[QueryRow]:
    rows: List[QueryRow] = []
    with path.open() as file:
        for line_number, line in enumerate(file, start=1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) != 3:
                raise ValueError(f"{path}:{line_number}: expected 3 columns")
            rows.append((int(parts[0]), int(parts[1]), int(parts[2])))
    if not rows:
        raise ValueError(f"Empty source query file: {path}")
    return rows


def read_coordinates(path: Path) -> Dict[int, Coordinate]:
    coordinates: Dict[int, Coordinate] = {}
    with path.open() as file:
        for line in file:
            parts = line.split()
            if len(parts) < 3:
                continue
            coordinates[int(parts[0])] = (float(parts[1]), float(parts[2]))
    return coordinates


def haversine_km(a: Coordinate, b: Coordinate) -> float:
    lon1, lat1 = a
    lon2, lat2 = b
    radius_km = 6371.0088
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    value = (
        math.sin(dphi / 2.0) ** 2
        + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2.0) ** 2
    )
    return 2.0 * radius_km * math.atan2(math.sqrt(value), math.sqrt(1.0 - value))


def filter_rows_by_center_radius(
    rows: Sequence[QueryRow],
    coordinates: Dict[int, Coordinate],
    center: Coordinate,
    radius_km: float,
) -> List[QueryRow]:
    if radius_km <= 0:
        return list(rows)

    filtered: List[QueryRow] = []
    for origin, destination, departure in rows:
        if origin not in coordinates or destination not in coordinates:
            continue
        if (
            haversine_km(center, coordinates[origin]) <= radius_km
            and haversine_km(center, coordinates[destination]) <= radius_km
        ):
            filtered.append((origin, destination, departure))
    return filtered


def maybe_rescale_departures(rows: Sequence[QueryRow], window_sec: int) -> List[QueryRow]:
    if window_sec <= 0:
        return list(rows)

    min_departure = min(row[2] for row in rows)
    max_departure = max(row[2] for row in rows)
    span = max_departure - min_departure
    if span <= 0:
        return [(origin, destination, 0) for origin, destination, _ in rows]

    return [
        (
            origin,
            destination,
            int(round((departure - min_departure) * window_sec / span)),
        )
        for origin, destination, departure in rows
    ]


def amplified_rows(
    rows: Sequence[QueryRow],
    query_count: int,
    jitter_sec: int,
    rng: random.Random,
) -> Tuple[List[QueryRow], int, int]:
    if not rows:
        raise ValueError("Cannot amplify an empty source row set")
    if query_count < len(rows):
        raise ValueError(
            f"query_count={query_count} is smaller than source rows={len(rows)}"
        )

    base_copies = query_count // len(rows)
    extra_copies = query_count % len(rows)
    extra_indices = list(range(len(rows)))
    rng.shuffle(extra_indices)
    extra_set = set(extra_indices[:extra_copies])

    output: List[QueryRow] = []
    min_copies = base_copies
    max_copies = base_copies + (1 if extra_copies > 0 else 0)
    for index, (origin, destination, departure) in enumerate(rows):
        copies = base_copies + (1 if index in extra_set else 0)
        for _ in range(copies):
            jitter = rng.randint(-jitter_sec, jitter_sec) if jitter_sec > 0 else 0
            output.append((origin, destination, max(0, departure + jitter)))

    if len(output) != query_count:
        raise ValueError(
            f"Internal error: wrote {len(output)} rows for target {query_count}"
        )
    output.sort(key=lambda row: row[2])
    return output, min_copies, max_copies


def write_queries(path: Path, rows: Iterable[QueryRow]) -> int:
    count = 0
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as file:
        for origin, destination, departure in rows:
            file.write(f"{origin} {destination} {departure}\n")
            count += 1
    return count


def rep_label_for_query_count(query_count: int, base_scale: int) -> int:
    if query_count % base_scale != 0:
        raise ValueError(
            f"Query count {query_count} is not divisible by base scale {base_scale}"
        )
    return query_count // base_scale


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", default="data/BJ_Real_query_sets")
    parser.add_argument("--coords", default="data/BJ_NodeIDLonLat.txt")
    parser.add_argument(
        "--dataset-prefix",
        default="BJReal",
        help=(
            "Dataset filename prefix. The builder reads "
            "{prefix}Rep1-{seed}.txt and writes {prefix}Rep{rep}-{seed}.txt."
        ),
    )
    parser.add_argument("--source-label", default="bj_real_high_congestion_scalability")
    parser.add_argument(
        "--output-dir",
        default="data/BJ_Real_query_sets_scalability_high_congestion",
    )
    parser.add_argument(
        "--query-counts",
        default="10000,20000,30000,50000,70000,100000",
    )
    parser.add_argument(
        "--size-specs",
        default="",
        help=(
            "Optional per-size specs: "
            "query_count:source_count:od_radius_km:window_sec[:jitter_sec],..."
        ),
    )
    parser.add_argument("--seeds", default="0,1,2,3,4")
    parser.add_argument(
        "--copies-per-source",
        type=int,
        default=10,
        help=(
            "Repeat count for each sampled real source row. Ignored when "
            "--source-count-per-dataset is positive."
        ),
    )
    parser.add_argument(
        "--source-count-per-dataset",
        type=int,
        default=0,
        help=(
            "If positive, sample this many source rows for every query size "
            "and set copies_per_source = query_count / source_count."
        ),
    )
    parser.add_argument(
        "--departure-window-sec",
        type=int,
        default=0,
        help="If positive, rescale selected source departures to this window.",
    )
    parser.add_argument(
        "--jitter-sec",
        type=int,
        default=0,
        help="Optional uniform copy-level departure jitter.",
    )
    parser.add_argument("--base-scale", type=int, default=10000)
    parser.add_argument("--center-lon", type=float, default=116.3975)
    parser.add_argument("--center-lat", type=float, default=39.9087)
    parser.add_argument(
        "--od-center-radius-km",
        type=float,
        default=0.0,
        help="If positive, keep rows whose origin and destination are inside this radius.",
    )
    parser.add_argument("--random-seed", type=int, default=20260523)
    args = parser.parse_args()

    if args.copies_per_source <= 0:
        raise ValueError("--copies-per-source must be positive")
    if args.source_count_per_dataset < 0:
        raise ValueError("--source-count-per-dataset must be non-negative")
    if args.jitter_sec < 0:
        raise ValueError("--jitter-sec must be non-negative")
    if args.od_center_radius_km < 0:
        raise ValueError("--od-center-radius-km must be non-negative")

    input_dir = Path(args.input_dir)
    output_dir = Path(args.output_dir)
    coordinates = read_coordinates(Path(args.coords))
    center = (args.center_lon, args.center_lat)
    size_specs = parse_size_specs(args.size_specs)
    query_counts = (
        [spec.query_count for spec in size_specs]
        if size_specs
        else parse_int_list(args.query_counts)
    )
    seeds = parse_int_list(args.seeds)
    if not query_counts:
        raise ValueError("--query-counts must not be empty")
    if not seeds:
        raise ValueError("--seeds must not be empty")

    output_dir.mkdir(parents=True, exist_ok=True)
    summary_rows = []

    for seed in seeds:
        source_path = input_dir / f"{args.dataset_prefix}Rep1-{seed}.txt"
        source_rows = read_queries(source_path)

        for query_count in query_counts:
            spec = next(
                (item for item in size_specs if item.query_count == query_count),
                None,
            )
            od_center_radius_km = (
                spec.od_center_radius_km if spec else args.od_center_radius_km
            )
            departure_window_sec = (
                spec.departure_window_sec if spec else args.departure_window_sec
            )
            jitter_sec = spec.jitter_sec if spec else args.jitter_sec

            filtered_rows = filter_rows_by_center_radius(
                source_rows,
                coordinates,
                center,
                od_center_radius_km,
            )
            if not filtered_rows:
                raise ValueError(
                    f"No rows left after OD radius filter for {source_path}"
                )

            if spec is not None:
                source_count = spec.source_count
            elif args.source_count_per_dataset > 0:
                source_count = args.source_count_per_dataset
            else:
                source_count = int(math.ceil(query_count / args.copies_per_source))

            if source_count > len(filtered_rows):
                raise ValueError(
                    f"{source_path} has {len(filtered_rows)} rows after filtering, but "
                    f"{source_count} are needed for query_count={query_count}"
                )

            rng = random.Random(args.random_seed + seed * 1000003 + query_count)
            selected_rows = list(filtered_rows)
            rng.shuffle(selected_rows)
            selected_rows = selected_rows[:source_count]
            selected_rows = maybe_rescale_departures(
                selected_rows,
                departure_window_sec,
            )

            rows, min_copies, max_copies = amplified_rows(
                selected_rows,
                query_count,
                jitter_sec,
                rng,
            )
            rep = rep_label_for_query_count(query_count, args.base_scale)
            output_path = output_dir / f"{args.dataset_prefix}Rep{rep}-{seed}.txt"
            written = write_queries(output_path, rows)

            unique_od_count = len({(row[0], row[1]) for row in selected_rows})
            min_departure = min(row[2] for row in rows)
            max_departure = max(row[2] for row in rows)
            summary_rows.append(
                {
                    "dataset": output_path.stem,
                    "source_file": str(source_path),
                    "seed": seed,
                    "rep": rep,
                    "query_count": written,
                    "filtered_source_row_count": len(filtered_rows),
                    "source_row_count": len(selected_rows),
                    "unique_od_count": unique_od_count,
                    "min_copies_per_source": min_copies,
                    "max_copies_per_source": max_copies,
                    "mean_copies_per_source": (
                        written / len(selected_rows) if selected_rows else 0
                    ),
                    "jitter_sec": jitter_sec,
                    "departure_window_sec": departure_window_sec,
                    "od_center_radius_km": od_center_radius_km,
                    "min_departure": min_departure,
                    "max_departure": max_departure,
                    "departure_span_sec": max_departure - min_departure,
                }
            )
            print(
                f"wrote {output_path}: queries={written} "
                f"source_rows={len(selected_rows)} "
                f"copies={min_copies}-{max_copies} "
                f"radius={od_center_radius_km}km window={departure_window_sec}s"
            )

    with (output_dir / "scalability_summary.csv").open("w", newline="") as file:
        fieldnames = [
            "dataset",
            "source_file",
            "seed",
            "rep",
            "query_count",
            "filtered_source_row_count",
            "source_row_count",
            "unique_od_count",
            "min_copies_per_source",
            "max_copies_per_source",
            "mean_copies_per_source",
            "jitter_sec",
            "departure_window_sec",
            "od_center_radius_km",
            "min_departure",
            "max_departure",
            "departure_span_sec",
        ]
        writer = csv.DictWriter(file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(summary_rows)

    metadata = {
        "source_label": args.source_label,
        "dataset_prefix": args.dataset_prefix,
        "input_dir": str(input_dir),
        "output_dir": str(output_dir),
        "derivation": (
            "For each seed and query count, sample source rows from the "
            f"real-derived {args.dataset_prefix}Rep1 file, repeat each selected source row "
            "copies_per_source times, optionally filter source rows by central "
            "OD radius, optionally rescale departures, and sort by departure "
            "time. This is a controlled congested peak workload for "
            "scalability experiments, not an independent raw taxi sample."
        ),
        "query_counts": query_counts,
        "size_specs": [spec.__dict__ for spec in size_specs],
        "seeds": seeds,
        "copies_per_source": args.copies_per_source,
        "source_count_per_dataset": args.source_count_per_dataset,
        "jitter_sec": args.jitter_sec,
        "departure_window_sec": args.departure_window_sec,
        "coordinates": args.coords,
        "center": {"lon": args.center_lon, "lat": args.center_lat},
        "od_center_radius_km": args.od_center_radius_km,
        "base_scale": args.base_scale,
        "random_seed": args.random_seed,
        "intended_congestion_gate": (
            "Run shortest_path_congestion_diagnostic and keep only datasets "
            "with shortest-path inflation in the paper-facing high-congestion "
            "range, currently 10x to 100x."
        ),
    }
    with (output_dir / "metadata.json").open("w") as file:
        json.dump(metadata, file, indent=2)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
