#!/usr/bin/env python3
"""Generate Beijing synthetic OD query sets near central Beijing.

The generator follows the experiment pattern used in the paper:

1. choose seed OD pairs around target shortest-path distances;
2. sample local origin/destination perturbations around each seed endpoint;
3. write base and repeated query sets with departure time 0.

Seed OD distances are checked with directed Dijkstra using physical edge length
estimated from node coordinates. Query OD pairs are sampled from local spatial
neighborhoods around the seed endpoints, which keeps their distances close to
the seed distance without requiring hundreds of full Dijkstra runs per set.
"""

from __future__ import annotations

import argparse
import csv
import heapq
import json
import math
import random
from collections import defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple


NodeId = int
Coordinate = Tuple[float, float]
Adjacency = Dict[NodeId, List[Tuple[NodeId, float]]]


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


def read_graph_adjacency(path: Path, coordinates: Dict[NodeId, Coordinate]) -> Adjacency:
    adjacency: Adjacency = defaultdict(list)
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
            length_km = haversine_km(coordinates[source], coordinates[target])
            if length_km <= 0.0:
                continue
            adjacency[source].append((target, length_km))
    return dict(adjacency)


class SpatialIndex:
    def __init__(self, coordinates: Dict[NodeId, Coordinate], cell_degrees: float = 0.01):
        self.coordinates = coordinates
        self.cell_degrees = cell_degrees
        self.cells: Dict[Tuple[int, int], List[NodeId]] = defaultdict(list)
        for node, (lon, lat) in coordinates.items():
            self.cells[self._cell(lon, lat)].append(node)

    def _cell(self, lon: float, lat: float) -> Tuple[int, int]:
        return (math.floor(lon / self.cell_degrees), math.floor(lat / self.cell_degrees))

    def within_radius(
        self,
        center: Coordinate,
        radius_km: float,
        allowed: set[NodeId] | None = None,
    ) -> List[NodeId]:
        lon, lat = center
        lat_delta = radius_km / 111.0
        lon_delta = radius_km / max(1e-9, 111.0 * math.cos(math.radians(lat)))
        min_cell = self._cell(lon - lon_delta, lat - lat_delta)
        max_cell = self._cell(lon + lon_delta, lat + lat_delta)
        result: List[NodeId] = []
        for x in range(min_cell[0], max_cell[0] + 1):
            for y in range(min_cell[1], max_cell[1] + 1):
                for node in self.cells.get((x, y), []):
                    if allowed is not None and node not in allowed:
                        continue
                    if haversine_km(center, self.coordinates[node]) <= radius_km:
                        result.append(node)
        return result


def dijkstra_nodes_in_range(
    adjacency: Adjacency,
    source: NodeId,
    min_distance_km: float,
    max_distance_km: float,
    allowed_targets: set[NodeId],
) -> List[Tuple[NodeId, float]]:
    distances = {source: 0.0}
    heap = [(0.0, source)]
    candidates: List[Tuple[NodeId, float]] = []

    while heap:
        distance, node = heapq.heappop(heap)
        if distance != distances[node]:
            continue
        if distance > max_distance_km:
            break
        if distance >= min_distance_km and node in allowed_targets and node != source:
            candidates.append((node, distance))
        for next_node, weight in adjacency.get(node, []):
            next_distance = distance + weight
            if next_distance > max_distance_km:
                continue
            if next_distance < distances.get(next_node, float("inf")):
                distances[next_node] = next_distance
                heapq.heappush(heap, (next_distance, next_node))
    return candidates


def choose_seed_pairs(
    rng: random.Random,
    adjacency: Adjacency,
    coordinates: Dict[NodeId, Coordinate],
    central_nodes: Sequence[NodeId],
    target_km: int,
    count: int,
    relative_tolerance: float,
    max_attempts: int,
) -> List[Tuple[NodeId, NodeId, float]]:
    central_set = set(central_nodes)
    min_distance = target_km * (1.0 - relative_tolerance)
    max_distance = target_km * (1.0 + relative_tolerance)
    shuffled = list(central_nodes)
    rng.shuffle(shuffled)
    seed_pairs: List[Tuple[NodeId, NodeId, float]] = []
    used = set()

    attempts = 0
    for source in shuffled:
        if len(seed_pairs) >= count or attempts >= max_attempts:
            break
        attempts += 1
        if source not in adjacency:
            continue
        candidates = dijkstra_nodes_in_range(
            adjacency,
            source,
            min_distance,
            max_distance,
            central_set,
        )
        if not candidates:
            continue
        rng.shuffle(candidates)
        for target, distance in candidates:
            key = (source, target)
            if key in used:
                continue
            used.add(key)
            seed_pairs.append((source, target, distance))
            break
    if len(seed_pairs) < count:
        raise RuntimeError(
            f"Only found {len(seed_pairs)} seed pairs for {target_km} km. "
            "Try increasing --central-radius-km or --distance-relative-tolerance."
        )
    return seed_pairs


def sample_local_queries(
    rng: random.Random,
    spatial_index: SpatialIndex,
    coordinates: Dict[NodeId, Coordinate],
    seed_source: NodeId,
    seed_target: NodeId,
    target_km: int,
    query_count: int,
    neighbor_radius_km: float,
    relative_tolerance: float,
    max_pair_attempts: int,
) -> List[Tuple[NodeId, NodeId]]:
    source_center = coordinates[seed_source]
    target_center = coordinates[seed_target]
    seed_haversine = haversine_km(source_center, target_center)
    min_haversine = seed_haversine - target_km * relative_tolerance
    max_haversine = seed_haversine + target_km * relative_tolerance

    radius = neighbor_radius_km
    best_pairs: List[Tuple[float, NodeId, NodeId]] = []
    for _ in range(5):
        origins = spatial_index.within_radius(source_center, radius)
        destinations = spatial_index.within_radius(target_center, radius)
        origins = [node for node in origins if node != seed_target]
        destinations = [node for node in destinations if node != seed_source]
        if origins and destinations:
            seen: set[Tuple[NodeId, NodeId]] = set()
            pairs: List[Tuple[NodeId, NodeId]] = []
            attempts = 0
            while attempts < max_pair_attempts and len(pairs) < query_count:
                attempts += 1
                origin = rng.choice(origins)
                destination = rng.choice(destinations)
                if origin == destination:
                    continue
                pair = (origin, destination)
                if pair in seen:
                    continue
                distance = haversine_km(coordinates[origin], coordinates[destination])
                error = abs(distance - seed_haversine)
                best_pairs.append((error, origin, destination))
                if min_haversine <= distance <= max_haversine:
                    seen.add(pair)
                    pairs.append(pair)
            if len(pairs) >= query_count:
                return pairs[:query_count]
        radius *= 1.5

    best_pairs.sort(key=lambda item: item[0])
    unique: List[Tuple[NodeId, NodeId]] = []
    seen_pairs: set[Tuple[NodeId, NodeId]] = set()
    for _, origin, destination in best_pairs:
        pair = (origin, destination)
        if pair in seen_pairs:
            continue
        seen_pairs.add(pair)
        unique.append(pair)
        if len(unique) >= query_count:
            return unique
    raise RuntimeError(
        f"Could not sample {query_count} local OD pairs around "
        f"{seed_source}->{seed_target}."
    )


def write_queries(path: Path, pairs: Sequence[Tuple[NodeId, NodeId]], repeats: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as file:
        for _ in range(repeats):
            for origin, destination in pairs:
                file.write(f"{origin} {destination} 0\n")


def parse_csv_ints(text: str) -> List[int]:
    return [int(value) for value in text.split(",") if value.strip()]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--graph", default="data/BJ.txt")
    parser.add_argument("--coords", default="data/BJ_NodeIDLonLat.txt")
    parser.add_argument("--output-dir", default="data/BJ_Synthetic_query_sets")
    parser.add_argument("--distances-km", default="5,10,40")
    parser.add_argument("--sets-per-distance", type=int, default=15)
    parser.add_argument("--queries-per-set", type=int, default=100)
    parser.add_argument("--rep-values", default="1,2,4")
    parser.add_argument("--center-lon", type=float, default=116.3975)
    parser.add_argument("--center-lat", type=float, default=39.9087)
    parser.add_argument("--central-radius-km", type=float, default=16.0)
    parser.add_argument("--neighbor-radius-km", type=float, default=0.8)
    parser.add_argument("--distance-relative-tolerance", type=float, default=0.15)
    parser.add_argument("--random-seed", type=int, default=0)
    parser.add_argument("--max-seed-attempts", type=int, default=2000)
    parser.add_argument("--max-pair-attempts", type=int, default=50000)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rng = random.Random(args.random_seed)
    graph_path = Path(args.graph)
    coords_path = Path(args.coords)
    output_dir = Path(args.output_dir)
    distances = parse_csv_ints(args.distances_km)
    rep_values = parse_csv_ints(args.rep_values)

    coordinates = read_coordinates(coords_path)
    adjacency = read_graph_adjacency(graph_path, coordinates)
    spatial_index = SpatialIndex(coordinates)
    center = (args.center_lon, args.center_lat)
    central_nodes = spatial_index.within_radius(center, args.central_radius_km)
    central_nodes = [node for node in central_nodes if node in adjacency]
    if not central_nodes:
        raise RuntimeError("No central nodes found. Check center/radius inputs.")

    metadata = {
        "graph": str(graph_path),
        "coordinates": str(coords_path),
        "output_dir": str(output_dir),
        "center": {"lon": args.center_lon, "lat": args.center_lat},
        "central_radius_km": args.central_radius_km,
        "central_node_count": len(central_nodes),
        "neighbor_radius_km": args.neighbor_radius_km,
        "distance_relative_tolerance": args.distance_relative_tolerance,
        "sets_per_distance": args.sets_per_distance,
        "queries_per_base_set": args.queries_per_set,
        "rep_values": rep_values,
        "random_seed": args.random_seed,
        "sets": [],
    }

    for target_km in distances:
        seed_pairs = choose_seed_pairs(
            rng,
            adjacency,
            coordinates,
            central_nodes,
            target_km,
            args.sets_per_distance,
            args.distance_relative_tolerance,
            args.max_seed_attempts,
        )
        for set_id, (seed_source, seed_target, seed_distance) in enumerate(seed_pairs):
            pairs = sample_local_queries(
                rng,
                spatial_index,
                coordinates,
                seed_source,
                seed_target,
                target_km,
                args.queries_per_set,
                args.neighbor_radius_km,
                args.distance_relative_tolerance,
                args.max_pair_attempts,
            )
            for rep in rep_values:
                filename = f"Hop{target_km}Rep{rep}-{set_id}.txt"
                write_queries(output_dir / filename, pairs, rep)
            metadata["sets"].append(
                {
                    "distance_km": target_km,
                    "set_id": set_id,
                    "seed_origin": seed_source,
                    "seed_destination": seed_target,
                    "seed_shortest_distance_km": seed_distance,
                    "seed_haversine_distance_km": haversine_km(
                        coordinates[seed_source],
                        coordinates[seed_target],
                    ),
                    "base_query_count": len(pairs),
                }
            )
            print(
                f"generated Hop{target_km} set {set_id}: "
                f"seed={seed_source}->{seed_target} "
                f"shortest={seed_distance:.2f}km queries={len(pairs)}"
            )

    with (output_dir / "metadata.json").open("w") as file:
        json.dump(metadata, file, indent=2)
    with (output_dir / "seed_summary.csv").open("w", newline="") as file:
        writer = csv.DictWriter(
            file,
            fieldnames=[
                "distance_km",
                "set_id",
                "seed_origin",
                "seed_destination",
                "seed_shortest_distance_km",
                "seed_haversine_distance_km",
                "base_query_count",
            ],
        )
        writer.writeheader()
        writer.writerows(metadata["sets"])
    print(f"wrote {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
