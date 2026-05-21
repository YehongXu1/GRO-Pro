#!/usr/bin/env python3
"""Generate Beijing real-world query sets from T-Drive taxi GPS trajectories.

The output query files follow the project query format:

    origin_node destination_node departure_time_seconds

The generator derives OD requests from taxi trajectory segments whose endpoints
both fall inside a central Beijing region. By default this region is an 8 km
circle around Tiananmen, used as a practical third-ring proxy.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import io
import json
import math
import random
import zipfile
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, Iterable, Iterator, List, Optional, Sequence, Tuple


NodeId = int
Coordinate = Tuple[float, float]


@dataclass(frozen=True)
class GPSPoint:
    taxi_id: str
    timestamp: datetime
    timestamp_seconds: int
    lon: float
    lat: float


@dataclass(frozen=True)
class SnappedPoint:
    taxi_id: str
    timestamp_seconds: int
    lon: float
    lat: float
    node: NodeId


@dataclass(frozen=True)
class QueryCandidate:
    origin: NodeId
    destination: NodeId
    departure_abs_seconds: int
    taxi_id: str
    duration_seconds: int
    haversine_km: float


class SpatialIndex:
    def __init__(
        self,
        coordinates: Dict[NodeId, Coordinate],
        allowed_nodes: Optional[set[NodeId]] = None,
        cell_degrees: float = 0.005,
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


def read_coordinates(path: Path) -> Dict[NodeId, Coordinate]:
    coordinates: Dict[NodeId, Coordinate] = {}
    with path.open() as file:
        for line in file:
            parts = line.split()
            if len(parts) < 3:
                continue
            node = int(parts[0])
            coordinates[node] = (float(parts[1]), float(parts[2]))
    return coordinates


def read_graph_connectivity(
    path: Path,
    coordinates: Dict[NodeId, Coordinate],
) -> Tuple[set[NodeId], set[NodeId], List[Tuple[NodeId, NodeId]]]:
    outgoing: set[NodeId] = set()
    incoming: set[NodeId] = set()
    edges: List[Tuple[NodeId, NodeId]] = []
    with path.open() as file:
        for line in file:
            parts = line.split()
            if len(parts) < 5:
                continue
            edge_id = int(parts[0])
            if edge_id < 0:
                continue
            source = int(parts[1])
            target = int(parts[2])
            if source not in coordinates or target not in coordinates:
                continue
            if haversine_km(coordinates[source], coordinates[target]) <= 0.0:
                continue
            outgoing.add(source)
            incoming.add(target)
            edges.append((source, target))
    return outgoing, incoming, edges


def datetime_to_seconds(value: datetime) -> int:
    epoch = datetime(1970, 1, 1)
    return int((value - epoch).total_seconds())


def parse_timestamp(text: str) -> datetime:
    text = text.strip()
    formats = [
        "%Y-%m-%d %H:%M:%S",
        "%Y/%m/%d %H:%M:%S",
        "%Y-%m-%dT%H:%M:%S",
    ]
    for fmt in formats:
        try:
            return datetime.strptime(text, fmt)
        except ValueError:
            pass
    raise ValueError(f"Unsupported timestamp format: {text}")


def parse_tdrive_line(line: str, fallback_taxi_id: str) -> Optional[GPSPoint]:
    line = line.strip()
    if not line:
        return None

    if "," in line:
        parts = [part.strip() for part in line.split(",")]
        if len(parts) < 4:
            return None
        taxi_id = parts[0] or fallback_taxi_id
        timestamp_text = parts[1]
        lon_text = parts[2]
        lat_text = parts[3]
    else:
        parts = line.split()
        if len(parts) < 4:
            return None
        taxi_id = parts[0]
        if len(parts) >= 5:
            timestamp_text = f"{parts[1]} {parts[2]}"
            lon_text = parts[3]
            lat_text = parts[4]
        else:
            taxi_id = fallback_taxi_id
            timestamp_text = f"{parts[0]} {parts[1]}"
            lon_text = parts[2]
            lat_text = parts[3]

    timestamp = parse_timestamp(timestamp_text)
    return GPSPoint(
        taxi_id=taxi_id,
        timestamp=timestamp,
        timestamp_seconds=datetime_to_seconds(timestamp),
        lon=float(lon_text),
        lat=float(lat_text),
    )


def iter_trajectory_files(
    input_dir: Path,
    max_files: int = 0,
    shuffle_files: bool = False,
    rng: Optional[random.Random] = None,
) -> Iterator[Path]:
    suffixes = {".txt", ".csv"}
    files = [
        path
        for path in input_dir.rglob("*")
        if path.is_file() and path.suffix.lower() in suffixes
    ]
    files.sort()
    if shuffle_files:
        if rng is None:
            raise RuntimeError("shuffle_files requires a random generator")
        rng.shuffle(files)
    if max_files > 0:
        files = files[:max_files]
    yield from files


def load_snapped_points_from_lines(
    lines: Iterable[str],
    fallback_taxi_id: str,
    spatial_index: SpatialIndex,
    center: Coordinate,
    region_radius_km: float,
    max_snap_distance_km: float,
    start_seconds: Optional[int],
    end_seconds: Optional[int],
) -> Tuple[List[SnappedPoint], int, int, int]:
    points: List[SnappedPoint] = []
    parsed_count = 0
    region_count = 0
    snapped_count = 0

    for line in lines:
        try:
            point = parse_tdrive_line(line, fallback_taxi_id)
        except (ValueError, IndexError):
            continue
        if point is None:
            continue
        parsed_count += 1
        if start_seconds is not None and point.timestamp_seconds < start_seconds:
            continue
        if end_seconds is not None and point.timestamp_seconds > end_seconds:
            continue
        if haversine_km(center, (point.lon, point.lat)) > region_radius_km:
            continue
        region_count += 1
        nearest = spatial_index.nearest(
            point.lon,
            point.lat,
            max_snap_distance_km,
        )
        if nearest is None:
            continue
        node, _ = nearest
        snapped_count += 1
        points.append(
            SnappedPoint(
                taxi_id=point.taxi_id,
                timestamp_seconds=point.timestamp_seconds,
                lon=point.lon,
                lat=point.lat,
                node=node,
            )
        )

    points.sort(key=lambda point: point.timestamp_seconds)
    deduped: List[SnappedPoint] = []
    previous: Optional[SnappedPoint] = None
    for point in points:
        if (
            previous is not None
            and previous.node == point.node
            and previous.timestamp_seconds == point.timestamp_seconds
        ):
            continue
        deduped.append(point)
        previous = point
    return deduped, parsed_count, region_count, snapped_count


def load_snapped_points(
    path: Path,
    spatial_index: SpatialIndex,
    center: Coordinate,
    region_radius_km: float,
    max_snap_distance_km: float,
    start_seconds: Optional[int],
    end_seconds: Optional[int],
) -> Tuple[List[SnappedPoint], int, int, int]:
    with path.open(errors="ignore") as file:
        return load_snapped_points_from_lines(
            file,
            path.stem,
            spatial_index,
            center,
            region_radius_km,
            max_snap_distance_km,
            start_seconds,
            end_seconds,
        )


def generate_candidates_from_track(
    points: Sequence[SnappedPoint],
    min_duration_seconds: int,
    max_duration_seconds: int,
    min_distance_km: float,
    max_distance_km: float,
    start_stride: int,
    max_end_points_per_start: int,
) -> Iterator[QueryCandidate]:
    if len(points) < 2:
        return

    timestamps = [point.timestamp_seconds for point in points]
    for start_index in range(0, len(points) - 1, max(1, start_stride)):
        start = points[start_index]
        min_end_time = start.timestamp_seconds + min_duration_seconds
        max_end_time = start.timestamp_seconds + max_duration_seconds
        first_end_index = bisect.bisect_left(timestamps, min_end_time, start_index + 1)
        last_end_index = bisect.bisect_right(timestamps, max_end_time, first_end_index)
        if max_end_points_per_start > 0:
            last_end_index = min(
                last_end_index,
                first_end_index + max_end_points_per_start,
            )
        best: Optional[QueryCandidate] = None
        for end_index in range(first_end_index, last_end_index):
            end = points[end_index]
            duration = end.timestamp_seconds - start.timestamp_seconds
            if start.node == end.node:
                continue
            distance = haversine_km((start.lon, start.lat), (end.lon, end.lat))
            if distance < min_distance_km or distance > max_distance_km:
                continue
            best = QueryCandidate(
                origin=start.node,
                destination=end.node,
                departure_abs_seconds=start.timestamp_seconds,
                taxi_id=start.taxi_id,
                duration_seconds=duration,
                haversine_km=distance,
            )
            break
        if best is not None:
            yield best


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
            ],
        )
        writer.writeheader()
        for set_id, candidates in enumerate(selected_sets):
            durations = [candidate.duration_seconds for candidate in candidates]
            distances = [candidate.haversine_km for candidate in candidates]
            departures = [candidate.departure_abs_seconds for candidate in candidates]
            writer.writerow(
                {
                    "dataset": f"BJReal-{set_id}",
                    "base_query_count": len(candidates),
                    "min_departure": min(departures) if departures else "",
                    "max_departure": max(departures) if departures else "",
                    "mean_duration_sec": (
                        sum(durations) / len(durations) if durations else ""
                    ),
                    "mean_haversine_km": (
                        sum(distances) / len(distances) if distances else ""
                    ),
                }
            )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tdrive-dir", required=True)
    parser.add_argument("--graph", default="data/BJ.txt")
    parser.add_argument("--coords", default="data/BJ_NodeIDLonLat.txt")
    parser.add_argument("--output-dir", default="data/BJ_Real_query_sets")
    parser.add_argument("--sets", type=int, default=5)
    parser.add_argument("--queries-per-set", type=int, default=10000)
    parser.add_argument("--rep-values", default="1,5,10")
    parser.add_argument("--center-lon", type=float, default=116.3975)
    parser.add_argument("--center-lat", type=float, default=39.9087)
    parser.add_argument("--region-radius-km", type=float, default=8.0)
    parser.add_argument("--max-snap-distance-m", type=float, default=500.0)
    parser.add_argument("--min-duration-min", type=float, default=5.0)
    parser.add_argument("--max-duration-min", type=float, default=60.0)
    parser.add_argument("--min-distance-km", type=float, default=0.8)
    parser.add_argument("--max-distance-km", type=float, default=16.0)
    parser.add_argument("--start-time")
    parser.add_argument("--end-time")
    parser.add_argument("--start-stride", type=int, default=1)
    parser.add_argument("--max-end-points-per-start", type=int, default=40)
    parser.add_argument("--amplify-time-jitter-sec", type=int, default=0)
    parser.add_argument("--max-files", type=int, default=0)
    parser.add_argument("--shuffle-files", action="store_true")
    parser.add_argument("--progress-interval", type=int, default=100)
    parser.add_argument("--random-seed", type=int, default=0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rng = random.Random(args.random_seed)
    tdrive_dir = Path(args.tdrive_dir)
    graph_path = Path(args.graph)
    coords_path = Path(args.coords)
    output_dir = Path(args.output_dir)
    center = (args.center_lon, args.center_lat)
    rep_values = parse_int_list(args.rep_values)
    required_candidates = args.sets * args.queries_per_set

    if not tdrive_dir.exists():
        raise RuntimeError(f"T-Drive directory does not exist: {tdrive_dir}")
    if required_candidates <= 0:
        raise RuntimeError("sets * queries-per-set must be positive")

    start_seconds = (
        datetime_to_seconds(parse_timestamp(args.start_time))
        if args.start_time
        else None
    )
    end_seconds = (
        datetime_to_seconds(parse_timestamp(args.end_time))
        if args.end_time
        else None
    )

    coordinates = read_coordinates(coords_path)
    outgoing, incoming, edges = read_graph_connectivity(graph_path, coordinates)
    inside_nodes = {
        node
        for node, coord in coordinates.items()
        if haversine_km(center, coord) <= args.region_radius_km
    }
    usable_nodes = inside_nodes & outgoing & incoming
    if not usable_nodes:
        raise RuntimeError("No usable road nodes found inside the requested region.")

    spatial_index = SpatialIndex(coordinates, usable_nodes)
    max_snap_distance_km = args.max_snap_distance_m / 1000.0
    min_duration_seconds = int(round(args.min_duration_min * 60.0))
    max_duration_seconds = int(round(args.max_duration_min * 60.0))

    reservoir: List[QueryCandidate] = []
    seen_candidates = 0
    file_count = 0
    parsed_points = 0
    region_points = 0
    snapped_points = 0
    internal_edges = [
        (source, target)
        for source, target in edges
        if source in usable_nodes and target in usable_nodes
    ]

    def process_points(points: Sequence[SnappedPoint]) -> None:
        nonlocal seen_candidates
        for candidate in generate_candidates_from_track(
            points,
            min_duration_seconds,
            max_duration_seconds,
            args.min_distance_km,
            args.max_distance_km,
            args.start_stride,
            args.max_end_points_per_start,
        ):
            seen_candidates += 1
            reservoir_insert(
                reservoir,
                candidate,
                seen_candidates,
                required_candidates,
                rng,
            )

    if tdrive_dir.is_file() and tdrive_dir.suffix.lower() == ".zip":
        with zipfile.ZipFile(tdrive_dir) as archive:
            names = [
                name
                for name in archive.namelist()
                if name.lower().endswith((".txt", ".csv"))
            ]
            names.sort()
            if args.shuffle_files:
                rng.shuffle(names)
            if args.max_files > 0:
                names = names[: args.max_files]
            for name in names:
                file_count += 1
                fallback_taxi_id = Path(name).stem
                with archive.open(name) as raw:
                    text = io.TextIOWrapper(raw, encoding="utf-8", errors="ignore")
                    points, parsed, in_region, snapped = load_snapped_points_from_lines(
                        text,
                        fallback_taxi_id,
                        spatial_index,
                        center,
                        args.region_radius_km,
                        max_snap_distance_km,
                        start_seconds,
                        end_seconds,
                    )
                parsed_points += parsed
                region_points += in_region
                snapped_points += snapped
                process_points(points)
                if args.progress_interval > 0 and file_count % args.progress_interval == 0:
                    print(
                        "processed "
                        f"{file_count} files, seen_candidates={seen_candidates}, "
                        f"reservoir={len(reservoir)}"
                    )
    else:
        for path in iter_trajectory_files(
            tdrive_dir,
            args.max_files,
            args.shuffle_files,
            rng,
        ):
            file_count += 1
            points, parsed, in_region, snapped = load_snapped_points(
                path,
                spatial_index,
                center,
                args.region_radius_km,
                max_snap_distance_km,
                start_seconds,
                end_seconds,
            )
            parsed_points += parsed
            region_points += in_region
            snapped_points += snapped
            process_points(points)
            if args.progress_interval > 0 and file_count % args.progress_interval == 0:
                print(
                    "processed "
                    f"{file_count} files, seen_candidates={seen_candidates}, "
                    f"reservoir={len(reservoir)}"
                )

    if len(reservoir) < required_candidates:
        raise RuntimeError(
            f"Only found {len(reservoir)} candidates, but "
            f"{required_candidates} are required. Try lowering "
            "--queries-per-set, --sets, --min-duration-min, or --min-distance-km."
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
            filename = f"BJRealRep{rep}-{set_id}.txt"
            write_query_file(
                output_dir / filename,
                candidates,
                rep,
                time_origin_seconds,
                rng,
                args.amplify_time_jitter_sec,
            )
            print(
                f"wrote {filename}: base={len(candidates)} total={len(candidates) * rep}"
            )

    metadata = {
        "source": "T-Drive Beijing taxi GPS trajectories",
        "graph": str(graph_path),
        "coordinates": str(coords_path),
        "tdrive_dir": str(tdrive_dir),
        "output_dir": str(output_dir),
        "naming": "BJRealRep{amplification_factor}-{set_id}.txt",
        "center": {"lon": args.center_lon, "lat": args.center_lat},
        "region_radius_km": args.region_radius_km,
        "region_description": "8 km Tiananmen-centered third-ring proxy",
        "road_nodes_inside_region": len(inside_nodes),
        "usable_road_nodes_inside_region": len(usable_nodes),
        "usable_internal_directed_edges": len(internal_edges),
        "sets": args.sets,
        "queries_per_base_set": args.queries_per_set,
        "rep_values": rep_values,
        "max_snap_distance_m": args.max_snap_distance_m,
        "min_duration_min": args.min_duration_min,
        "max_duration_min": args.max_duration_min,
        "min_distance_km": args.min_distance_km,
        "max_distance_km": args.max_distance_km,
        "start_time": args.start_time,
        "end_time": args.end_time,
        "start_stride": args.start_stride,
        "max_end_points_per_start": args.max_end_points_per_start,
        "amplify_time_jitter_sec": args.amplify_time_jitter_sec,
        "max_files": args.max_files,
        "shuffle_files": args.shuffle_files,
        "random_seed": args.random_seed,
        "files_processed": file_count,
        "gps_points_parsed": parsed_points,
        "gps_points_inside_region": region_points,
        "gps_points_snapped": snapped_points,
        "candidate_segments_seen": seen_candidates,
        "time_origin_seconds": time_origin_seconds,
    }
    output_dir.mkdir(parents=True, exist_ok=True)
    write_metadata(output_dir, metadata, selected_sets)
    print(f"wrote metadata to {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
