#!/usr/bin/env python3
"""Generate Manhattan real-world query sets from NYC TLC taxi trip records.

The preferred input is a historical TLC yellow taxi CSV containing explicit
pickup/dropoff longitude and latitude columns. The output query files follow the
project query format:

    origin_node destination_node departure_time_seconds

This script intentionally does not depend on pandas/pyarrow so it can stream
large CSV, CSV.GZ, or ZIP-contained CSV files.
"""

from __future__ import annotations

import argparse
import csv
import gzip
import io
import json
import math
import random
import re
import zipfile
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, Iterable, Iterator, List, Optional, Sequence, TextIO, Tuple


NodeId = int
Coordinate = Tuple[float, float]

NCL_ANONDATA_COLUMNS = [
    "medallion_id",
    "license_id",
    "pickup_datetime",
    "dropoff_datetime",
    "trip_time_sec",
    "trip_distance",
    "pickup_longitude",
    "pickup_latitude",
    "dropoff_longitude",
    "dropoff_latitude",
    "payment_type",
    "fare_amount",
    "surcharge",
    "mta_tax",
    "tip_amount",
    "tolls_amount",
    "total_amount",
]

NCL_ANONDATA_COLUMN_MAP = {
    "pickup_time": "pickup_datetime",
    "dropoff_time": "dropoff_datetime",
    "pickup_lon": "pickup_longitude",
    "pickup_lat": "pickup_latitude",
    "dropoff_lon": "dropoff_longitude",
    "dropoff_lat": "dropoff_latitude",
    "trip_distance": "trip_distance",
}


@dataclass(frozen=True)
class QueryCandidate:
    origin: NodeId
    destination: NodeId
    departure_abs_seconds: int
    duration_seconds: int
    haversine_km: float
    pickup_snap_m: float
    dropoff_snap_m: float


class SpatialIndex:
    def __init__(
        self,
        coordinates: Dict[NodeId, Coordinate],
        allowed_nodes: Optional[set[NodeId]] = None,
        cell_degrees: float = 0.002,
    ) -> None:
        self.coordinates = coordinates
        self.cell_degrees = cell_degrees
        self.cells: Dict[Tuple[int, int], List[NodeId]] = defaultdict(list)
        for node, (lon, lat) in coordinates.items():
            if allowed_nodes is not None and node not in allowed_nodes:
                continue
            self.cells[self._cell(lon, lat)].append(node)

    def _cell(self, lon: float, lat: float) -> Tuple[int, int]:
        return (
            math.floor(lon / self.cell_degrees),
            math.floor(lat / self.cell_degrees),
        )

    def nearest(
        self,
        lon: float,
        lat: float,
        max_distance_km: float,
    ) -> Optional[Tuple[NodeId, float]]:
        lat_delta = max_distance_km / 111.0
        lon_delta = max_distance_km / max(
            1e-9,
            111.0 * math.cos(math.radians(lat)),
        )
        min_cell = self._cell(lon - lon_delta, lat - lat_delta)
        max_cell = self._cell(lon + lon_delta, lat + lat_delta)

        best_node: Optional[NodeId] = None
        best_distance = float("inf")
        for x in range(min_cell[0], max_cell[0] + 1):
            for y in range(min_cell[1], max_cell[1] + 1):
                for node in self.cells.get((x, y), []):
                    distance = haversine_km((lon, lat), self.coordinates[node])
                    if distance < best_distance:
                        best_node = node
                        best_distance = distance

        if best_node is None or best_distance > max_distance_km:
            return None
        return best_node, best_distance


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


def normalize_column(name: str) -> str:
    return re.sub(r"[^a-z0-9]", "", name.lower())


def read_coordinates(path: Path) -> Dict[NodeId, Coordinate]:
    coordinates: Dict[NodeId, Coordinate] = {}
    with path.open() as file:
        for line in file:
            parts = line.split()
            if len(parts) < 3:
                continue
            coordinates[int(parts[0])] = (float(parts[1]), float(parts[2]))
    return coordinates


def read_usable_nodes(graph_path: Path) -> set[NodeId]:
    out_degree: Dict[NodeId, int] = defaultdict(int)
    in_degree: Dict[NodeId, int] = defaultdict(int)
    with graph_path.open() as file:
        for line in file:
            line = line.strip()
            if not line or line.startswith("%") or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) >= 5:
                source = int(parts[1])
                target = int(parts[2])
            elif len(parts) >= 2:
                source = int(parts[0])
                target = int(parts[1])
            else:
                continue
            if source == target:
                continue
            out_degree[source] += 1
            in_degree[target] += 1
    return {
        node
        for node in set(out_degree).union(in_degree)
        if out_degree[node] > 0 and in_degree[node] > 0
    }


def coordinate_bbox(
    coordinates: Dict[NodeId, Coordinate],
    padding_km: float,
) -> Tuple[float, float, float, float]:
    lons = [coord[0] for coord in coordinates.values()]
    lats = [coord[1] for coord in coordinates.values()]
    mean_lat = sum(lats) / len(lats)
    lat_delta = padding_km / 111.0
    lon_delta = padding_km / max(1e-9, 111.0 * math.cos(math.radians(mean_lat)))
    return (
        min(lons) - lon_delta,
        min(lats) - lat_delta,
        max(lons) + lon_delta,
        max(lats) + lat_delta,
    )


def in_bbox(lon: float, lat: float, bbox: Tuple[float, float, float, float]) -> bool:
    min_lon, min_lat, max_lon, max_lat = bbox
    return min_lon <= lon <= max_lon and min_lat <= lat <= max_lat


def parse_datetime_value(value: str) -> Optional[datetime]:
    value = value.strip()
    if not value:
        return None
    if value.endswith("Z"):
        value = value[:-1]
    value = value.replace("T", " ")
    if "+" in value:
        value = value.split("+", 1)[0].strip()
    for fmt in (
        "%Y-%m-%d %H:%M:%S",
        "%Y-%m-%d %H:%M:%S.%f",
        "%m/%d/%Y %I:%M:%S %p",
        "%m/%d/%Y %H:%M:%S",
        "%Y/%m/%d %H:%M:%S",
    ):
        try:
            return datetime.strptime(value, fmt)
        except ValueError:
            pass
    try:
        return datetime.fromisoformat(value)
    except ValueError:
        return None


def parse_float_value(value: str) -> Optional[float]:
    try:
        result = float(value)
    except (TypeError, ValueError):
        return None
    if not math.isfinite(result):
        return None
    return result


def open_text(path: Path) -> Iterator[Tuple[str, TextIO]]:
    suffixes = [suffix.lower() for suffix in path.suffixes]
    if suffixes[-2:] == [".csv", ".gz"] or path.suffix.lower() == ".gz":
        with gzip.open(path, "rt", newline="", errors="ignore") as file:
            yield str(path), file
    elif path.suffix.lower() == ".zip":
        with zipfile.ZipFile(path) as archive:
            names = [
                name
                for name in archive.namelist()
                if name.lower().endswith(".csv")
            ]
            names.sort()
            for name in names:
                with archive.open(name) as raw:
                    text = io.TextIOWrapper(raw, encoding="utf-8", errors="ignore")
                    yield f"{path}:{name}", text
    else:
        with path.open(newline="", errors="ignore") as file:
            yield str(path), file


def resolve_column(
    fieldnames: Sequence[str],
    candidates: Sequence[str],
) -> Optional[str]:
    normalized = {normalize_column(name): name for name in fieldnames}
    for candidate in candidates:
        match = normalized.get(normalize_column(candidate))
        if match is not None:
            return match
    return None


def require_columns(
    source_name: str,
    fieldnames: Sequence[str],
) -> Dict[str, str]:
    specs = {
        "pickup_time": (
            "pickup_datetime",
            "tpep_pickup_datetime",
            "trip_pickup_datetime",
            "trip_pickup_date_time",
            "lpep_pickup_datetime",
        ),
        "dropoff_time": (
            "dropoff_datetime",
            "tpep_dropoff_datetime",
            "trip_dropoff_datetime",
            "trip_dropoff_date_time",
            "lpep_dropoff_datetime",
        ),
        "pickup_lon": (
            "pickup_longitude",
            "start_lon",
            "pickup_lon",
            "pu_longitude",
        ),
        "pickup_lat": (
            "pickup_latitude",
            "start_lat",
            "pickup_lat",
            "pu_latitude",
        ),
        "dropoff_lon": (
            "dropoff_longitude",
            "drop_off_longitude",
            "end_lon",
            "dropoff_lon",
            "do_longitude",
        ),
        "dropoff_lat": (
            "dropoff_latitude",
            "drop_off_latitude",
            "end_lat",
            "dropoff_lat",
            "do_latitude",
        ),
    }
    result: Dict[str, str] = {}
    missing: List[str] = []
    for key, candidates in specs.items():
        column = resolve_column(fieldnames, candidates)
        if column is None:
            missing.append(key)
        else:
            result[key] = column
    if missing:
        raise RuntimeError(
            f"{source_name}: missing coordinate-level TLC columns: "
            f"{', '.join(missing)}. This file may be zone-level Parquet/CSV; "
            "use a historical coordinate-level yellow taxi CSV for this generator."
        )
    distance_column = resolve_column(
        fieldnames,
        ("trip_distance", "trip_distance_miles", "distance"),
    )
    if distance_column is not None:
        result["trip_distance"] = distance_column
    return result


def iter_input_paths(args: argparse.Namespace) -> List[Path]:
    paths = [Path(path) for path in args.tlc_file]
    if args.tlc_dir:
        directory = Path(args.tlc_dir)
        paths.extend(sorted(path for path in directory.glob(args.pattern) if path.is_file()))
    if not paths:
        raise RuntimeError("Provide --tlc-file or --tlc-dir")
    return paths


def reservoir_insert(
    reservoir: List[QueryCandidate],
    item: QueryCandidate,
    seen_count: int,
    reservoir_size: int,
    rng: random.Random,
) -> None:
    if len(reservoir) < reservoir_size:
        reservoir.append(item)
        return
    replacement_index = rng.randrange(seen_count)
    if replacement_index < reservoir_size:
        reservoir[replacement_index] = item


def parse_int_list(text: str) -> List[int]:
    return [int(value) for value in text.split(",") if value.strip()]


def write_query_file(
    path: Path,
    candidates: Sequence[QueryCandidate],
    rep: int,
    time_origin_seconds: int,
    rng: random.Random,
    jitter_seconds: int,
) -> None:
    rows: List[Tuple[int, int, int]] = []
    for candidate in candidates:
        base_departure = candidate.departure_abs_seconds - time_origin_seconds
        for _ in range(rep):
            jitter = (
                rng.randint(-jitter_seconds, jitter_seconds)
                if jitter_seconds > 0
                else 0
            )
            departure = max(0, base_departure + jitter)
            rows.append((candidate.origin, candidate.destination, departure))
    rows.sort(key=lambda row: row[2])

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as file:
        for origin, destination, departure in rows:
            file.write(f"{origin} {destination} {departure}\n")


def write_metadata(
    output_dir: Path,
    metadata: dict,
    selected_sets: Sequence[Sequence[QueryCandidate]],
) -> None:
    with (output_dir / "metadata.json").open("w") as file:
        json.dump(metadata, file, indent=2)

    with (output_dir / "query_set_summary.csv").open("w", newline="") as file:
        writer = csv.DictWriter(
            file,
            fieldnames=[
                "dataset",
                "base_query_count",
                "min_departure",
                "max_departure",
                "mean_duration_sec",
                "mean_haversine_km",
                "mean_snap_m",
            ],
        )
        writer.writeheader()
        for set_id, candidates in enumerate(selected_sets):
            departures = [candidate.departure_abs_seconds for candidate in candidates]
            mean_duration = sum(candidate.duration_seconds for candidate in candidates) / len(candidates)
            mean_haversine = sum(candidate.haversine_km for candidate in candidates) / len(candidates)
            mean_snap = (
                sum(candidate.pickup_snap_m + candidate.dropoff_snap_m for candidate in candidates)
                / (2.0 * len(candidates))
            )
            writer.writerow(
                {
                    "dataset": f"MHReal-{set_id}",
                    "base_query_count": len(candidates),
                    "min_departure": min(departures),
                    "max_departure": max(departures),
                    "mean_duration_sec": round(mean_duration, 4),
                    "mean_haversine_km": round(mean_haversine, 6),
                    "mean_snap_m": round(mean_snap, 3),
                }
            )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tlc-file", action="append", default=[])
    parser.add_argument("--tlc-dir")
    parser.add_argument("--pattern", default="*.csv*")
    parser.add_argument(
        "--input-format",
        choices=("header", "ncl-anondata"),
        default="header",
        help=(
            "header expects named coordinate-level TLC columns; ncl-anondata "
            "expects the 17-column headerless 2013 anonymous excerpt format"
        ),
    )
    parser.add_argument("--graph", default="data/MH.txt")
    parser.add_argument("--coordinates", default="data/MH_NodeIDLonLat.txt")
    parser.add_argument("--output-dir", default="data/MH_Real_query_sets")
    parser.add_argument("--sets", type=int, default=5)
    parser.add_argument("--queries-per-set", type=int, default=10000)
    parser.add_argument("--rep-values", default="1,5,10")
    parser.add_argument("--max-snap-distance-m", type=float, default=250.0)
    parser.add_argument("--bbox-padding-km", type=float, default=0.5)
    parser.add_argument("--min-duration-min", type=float, default=2.0)
    parser.add_argument("--max-duration-min", type=float, default=60.0)
    parser.add_argument("--min-distance-km", type=float, default=0.5)
    parser.add_argument("--max-distance-km", type=float, default=30.0)
    parser.add_argument("--min-trip-distance-mile", type=float, default=0.2)
    parser.add_argument("--max-trip-distance-mile", type=float, default=40.0)
    parser.add_argument("--amplify-time-jitter-sec", type=int, default=0)
    parser.add_argument("--max-rows", type=int, default=0)
    parser.add_argument("--stop-after-required", action="store_true")
    parser.add_argument("--progress-interval", type=int, default=1000000)
    parser.add_argument("--random-seed", type=int, default=0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rng = random.Random(args.random_seed)
    rep_values = parse_int_list(args.rep_values)
    required_candidates = args.sets * args.queries_per_set
    output_dir = Path(args.output_dir)

    coordinates = read_coordinates(Path(args.coordinates))
    usable_nodes = read_usable_nodes(Path(args.graph))
    allowed_nodes = set(coordinates).intersection(usable_nodes)
    spatial_index = SpatialIndex(coordinates, allowed_nodes)
    bbox = coordinate_bbox(
        {node: coordinates[node] for node in allowed_nodes},
        args.bbox_padding_km,
    )
    max_snap_distance_km = args.max_snap_distance_m / 1000.0
    min_duration_seconds = int(round(args.min_duration_min * 60.0))
    max_duration_seconds = int(round(args.max_duration_min * 60.0))

    counters: Dict[str, int] = defaultdict(int)
    reservoir: List[QueryCandidate] = []
    input_paths = iter_input_paths(args)
    print(
        f"mh_real_start inputs={len(input_paths)} required_candidates={required_candidates} "
        f"max_snap_m={args.max_snap_distance_m} input_format={args.input_format}",
        flush=True,
    )

    for path in input_paths:
        for source_name, file in open_text(path):
            print(f"source_start {source_name}", flush=True)
            if args.input_format == "ncl-anondata":
                reader = csv.DictReader(file, fieldnames=NCL_ANONDATA_COLUMNS)
                columns = NCL_ANONDATA_COLUMN_MAP
            else:
                reader = csv.DictReader(file)
                if not reader.fieldnames:
                    continue
                columns = require_columns(source_name, reader.fieldnames)
            for row in reader:
                if None in row:
                    counters["bad_column_count"] += 1
                    continue
                counters["rows_seen"] += 1
                if args.max_rows > 0 and counters["rows_seen"] > args.max_rows:
                    break
                if (
                    args.progress_interval > 0
                    and counters["rows_seen"] % args.progress_interval == 0
                ):
                    print(
                        f"rows={counters['rows_seen']} "
                        f"candidates={counters['candidates_seen']} "
                        f"reservoir={len(reservoir)} "
                        f"bad_duration={counters['bad_duration']} "
                        f"outside_bbox={counters['outside_bbox']} "
                        f"snap_failed={counters['snap_failed']}",
                        flush=True,
                    )

                pickup_time = parse_datetime_value(row.get(columns["pickup_time"], ""))
                dropoff_time = parse_datetime_value(row.get(columns["dropoff_time"], ""))
                if pickup_time is None or dropoff_time is None:
                    counters["bad_time"] += 1
                    continue
                duration_seconds = int((dropoff_time - pickup_time).total_seconds())
                if duration_seconds < min_duration_seconds or duration_seconds > max_duration_seconds:
                    counters["bad_duration"] += 1
                    continue

                pickup_lon = parse_float_value(row.get(columns["pickup_lon"], ""))
                pickup_lat = parse_float_value(row.get(columns["pickup_lat"], ""))
                dropoff_lon = parse_float_value(row.get(columns["dropoff_lon"], ""))
                dropoff_lat = parse_float_value(row.get(columns["dropoff_lat"], ""))
                if (
                    pickup_lon is None
                    or pickup_lat is None
                    or dropoff_lon is None
                    or dropoff_lat is None
                    or pickup_lon == 0.0
                    or pickup_lat == 0.0
                    or dropoff_lon == 0.0
                    or dropoff_lat == 0.0
                ):
                    counters["bad_coordinate"] += 1
                    continue

                if not in_bbox(pickup_lon, pickup_lat, bbox) or not in_bbox(dropoff_lon, dropoff_lat, bbox):
                    counters["outside_bbox"] += 1
                    continue

                haversine = haversine_km((pickup_lon, pickup_lat), (dropoff_lon, dropoff_lat))
                if haversine < args.min_distance_km or haversine > args.max_distance_km:
                    counters["bad_haversine"] += 1
                    continue

                if "trip_distance" in columns:
                    trip_distance = parse_float_value(row.get(columns["trip_distance"], ""))
                    if trip_distance is None:
                        counters["bad_trip_distance"] += 1
                        continue
                    if (
                        trip_distance < args.min_trip_distance_mile
                        or trip_distance > args.max_trip_distance_mile
                    ):
                        counters["bad_trip_distance"] += 1
                        continue

                pickup_snap = spatial_index.nearest(
                    pickup_lon,
                    pickup_lat,
                    max_snap_distance_km,
                )
                dropoff_snap = spatial_index.nearest(
                    dropoff_lon,
                    dropoff_lat,
                    max_snap_distance_km,
                )
                if pickup_snap is None or dropoff_snap is None:
                    counters["snap_failed"] += 1
                    continue

                origin, pickup_snap_km = pickup_snap
                destination, dropoff_snap_km = dropoff_snap
                if origin == destination:
                    counters["same_node"] += 1
                    continue

                counters["candidates_seen"] += 1
                candidate = QueryCandidate(
                    origin=origin,
                    destination=destination,
                    departure_abs_seconds=int(pickup_time.timestamp()),
                    duration_seconds=duration_seconds,
                    haversine_km=haversine,
                    pickup_snap_m=pickup_snap_km * 1000.0,
                    dropoff_snap_m=dropoff_snap_km * 1000.0,
                )
                reservoir_insert(
                    reservoir,
                    candidate,
                    counters["candidates_seen"],
                    required_candidates,
                    rng,
                )

                if args.stop_after_required and len(reservoir) >= required_candidates:
                    break

            if args.max_rows > 0 and counters["rows_seen"] > args.max_rows:
                break
            if args.stop_after_required and len(reservoir) >= required_candidates:
                break

    if len(reservoir) < required_candidates:
        raise RuntimeError(
            f"Only found {len(reservoir)} candidates, but {required_candidates} "
            "are required. Try adding more input months, relaxing filters, or "
            "lowering --sets/--queries-per-set."
        )

    rng.shuffle(reservoir)
    selected_sets = [
        reservoir[index * args.queries_per_set : (index + 1) * args.queries_per_set]
        for index in range(args.sets)
    ]
    time_origin_seconds = min(
        candidate.departure_abs_seconds
        for candidates in selected_sets
        for candidate in candidates
    )

    for set_id, candidates in enumerate(selected_sets):
        for rep in rep_values:
            filename = f"MHRealRep{rep}-{set_id}.txt"
            write_query_file(
                output_dir / filename,
                candidates,
                rep,
                time_origin_seconds,
                rng,
                args.amplify_time_jitter_sec,
            )
            print(
                f"wrote {filename}: base={len(candidates)} total={len(candidates) * rep}",
                flush=True,
            )

    metadata = {
        "source": "NYC TLC yellow taxi trip records",
        "source_files": [str(path) for path in input_paths],
        "graph": args.graph,
        "coordinates": args.coordinates,
        "output_dir": args.output_dir,
        "naming": "MHRealRep{amplification_factor}-{set_id}.txt",
        "sets": args.sets,
        "queries_per_base_set": args.queries_per_set,
        "rep_values": rep_values,
        "preferred_schema": "coordinate-level pickup/dropoff longitude/latitude",
        "input_format": args.input_format,
        "bbox": {
            "min_lon": bbox[0],
            "min_lat": bbox[1],
            "max_lon": bbox[2],
            "max_lat": bbox[3],
            "padding_km": args.bbox_padding_km,
        },
        "max_snap_distance_m": args.max_snap_distance_m,
        "min_duration_min": args.min_duration_min,
        "max_duration_min": args.max_duration_min,
        "min_distance_km": args.min_distance_km,
        "max_distance_km": args.max_distance_km,
        "amplify_time_jitter_sec": args.amplify_time_jitter_sec,
        "random_seed": args.random_seed,
        "time_origin_seconds": time_origin_seconds,
        "counters": dict(counters),
    }
    write_metadata(output_dir, metadata, selected_sets)
    print(f"wrote {output_dir / 'metadata.json'}", flush=True)
    print(f"wrote {output_dir / 'query_set_summary.csv'}", flush=True)
    print(json.dumps({"counters": dict(counters)}, indent=2), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
