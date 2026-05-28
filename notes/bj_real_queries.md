# Beijing Real Query Sets

## Source

The real Beijing query sets are generated from the public T-Drive Beijing taxi
GPS trajectories. The raw data contains GPS points rather than explicit
passenger pickup/dropoff records, so each query is derived from a taxi
trajectory segment.

## Region

We focus on central Beijing inside a third-ring proxy:

```text
center: 116.3975, 39.9087
radius: 8 km
```

This circular filter is centered near Tiananmen. It is not an exact third-ring
polygon, but it is simple, reproducible, and matches the current road-network
coordinate system.

Under this proxy, the BJ road network contains:

```text
vertices by coordinates: 49,782
directed edges with both endpoints inside: 103,395
usable positive-length vertices with both incoming and outgoing edges: 44,565
usable positive-length directed internal edges: 78,624
```

## Query Derivation

For each taxi trajectory file:

1. Parse GPS points in the T-Drive format:

   ```text
   taxi_id,timestamp,longitude,latitude
   ```

2. Keep points inside the 8 km central region.
3. Snap each GPS point to the nearest usable BJ road-network vertex.
4. Extract OD segments from the same taxi trajectory.
5. Keep segments satisfying the configured duration and straight-line distance
   filters.
6. Sample query sets from all derived OD segments.

The generated query format is the project-standard format:

```text
origin_node destination_node departure_time_seconds
```

Departure times are stored as seconds relative to the earliest selected query
departure time.

## Naming

Real query files use:

```text
BJRealRep{amplification_factor}-{set_id}.txt
```

Examples:

```text
BJRealRep1-0.txt
BJRealRep5-0.txt
BJRealRep10-4.txt
```

`Rep` is the amplification factor. Unlike the synthetic files, there is no
`Hop` field because real queries are not grouped by target OD length.

## Generation Command

After downloading and extracting T-Drive, run for example:

```bash
/Users/xyh/opt/anaconda3/envs/plot/bin/python \
  python/generate_bj_real_queries_from_tdrive.py \
  --tdrive-dir "data/BJ_Real_query_sets/T-drive Taxi Trajectories.zip" \
  --output-dir data/BJ_Real_query_sets \
  --sets 5 \
  --queries-per-set 10000 \
  --rep-values 1,5,10 \
  --region-radius-km 8 \
  --min-duration-min 5 \
  --max-duration-min 60 \
  --min-distance-km 0.8 \
  --max-distance-km 16 \
  --max-snap-distance-m 500 \
  --max-files 500 \
  --shuffle-files \
  --progress-interval 100 \
  --random-seed 0
```

The obsolete generated set used a reproducible random subset of 500 taxi files.
This subset produced 232,093 candidate trajectory segments inside the 8 km
region, enough to sample five disjoint base query sets of 10,000 queries each.
Those short-trip query files and their metadata were removed after the
long-trip paper-facing workloads were generated.

Removed files:

```text
data/BJ_Real_query_sets/BJRealRep*.txt
data/BJ_Real_query_sets/metadata.json
data/BJ_Real_query_sets/query_set_summary.csv
```

## Removed Time-Window Diagnostics

The original T-Drive-derived query sets span about six days, which makes
congestion too weak for low amplification factors. A one-hour modulo workload
was too aggressive because it folded all trips into one artificial peak. The
old derived directories `data/BJ_Real_query_sets_window6h` and
`data/BJ_Real_query_sets_window1h` were removed during the
overall-effectiveness cleanup because the underlying BJ real OD sets are too
short for the final paper-facing real-world table.

## Revised Paper-Facing Overall Effectiveness Plan

The current BJ real query sets should be treated as diagnostic, not final
paper-facing overall-effectiveness workloads. They pass the congestion-ratio
gate after departure-window compression, but their OD lengths are too short:
`BJRealRep10-2` has `free_flow_ttt = 21,306,090` for 100k queries, which is
only about `3.55` minutes per query. A reviewer could reasonably object that
the workload is mostly short local trips and that high congestion is created by
compressing many short trips into the same time window.

For final real-world overall effectiveness, use the following gates:

```text
query_count: fixed at 100k for every dataset
OD source: T-Drive-derived real trajectory segments
query length gate: network free-flow shortest-path avg >= 8-10 min
query length upper guard: network free-flow shortest-path avg <= 45-60 min
congestion gate: choose lower / middle / high by evaluated_ttt / free_flow_ttt
```

The final three traffic levels should vary congestion intensity while keeping
the 100k query count and OD-length distribution comparable. Do not use
`Rep1/Rep5/Rep10` as traffic-intensity levels, because that changes both query
count and congestion. Prefer generating a pool of 100k longer-trip BJ real
candidate files and then creating departure-window variants such as:

```text
data/BJ_Real_query_sets_long100k
data/BJ_Real_query_sets_long100k_window6h
data/BJ_Real_query_sets_long100k_window3h
data/BJ_Real_query_sets_long100k_window2h
data/BJ_Real_query_sets_long100k_window1h
```

Candidate selection should be based on shortest-path congestion diagnostics:

```text
free_flow_ttt
evaluated_ttt
avg_free_flow_tt
avg_evaluated_tt
inflation_ratio = evaluated_ttt / free_flow_ttt
```

Suggested final target bands:

```text
lower   avg_free_flow >= 8-10 min, inflation roughly 3x-10x
middle  avg_free_flow >= 8-10 min, inflation roughly 10x-40x
high    avg_free_flow >= 8-10 min, inflation roughly 40x-100x
```

The exact ratio bands can be adjusted after seeing candidate diagnostics, but
the query-length gate should not be relaxed back to the old short-trip setting.
The old `data/BJ_Real_query_sets` query files were removed; that directory now
keeps only the T-Drive raw source archive/extracted trajectories for
regeneration.

## Ablation Test

The C++ ablation parser recognizes `BJRealRep{rep}-{id}.txt` files. For real
query sets, the output CSV uses:

```text
hop = -1
rep = amplification factor
seed = set id
```

Example:

```bash
./gro_ablation_test config/config_bj_capacity2_cap10e8.yaml \
  --query-dir data/BJ_Real_query_sets \
  --output python/results/bj_real/gro_ablation_bj_real.csv \
  --selection-methods random,most_delayed,tdg_bpr_relief \
  --reroute-methods normal,tdg \
  --fixed-fractions 10,30 \
  --tdg-gammas 50 \
  --impact-weights 30 \
  --random-seed 0
```
