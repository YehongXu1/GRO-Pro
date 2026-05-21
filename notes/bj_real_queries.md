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

The current generated set used a reproducible random subset of 500 taxi files.
This subset produced 232,093 candidate trajectory segments inside the 8 km
region, enough to sample five disjoint base query sets of 10,000 queries each.

The script also writes:

```text
data/BJ_Real_query_sets/metadata.json
data/BJ_Real_query_sets/query_set_summary.csv
```

## Six-Hour Peak Workload

The original T-Drive-derived query sets span about six days, which makes
congestion too weak for low amplification factors. A one-hour modulo workload
is too aggressive because it folds all trips into one artificial peak. The
current controlled real workload therefore uses a six-hour departure window:

```bash
/Users/xyh/opt/anaconda3/envs/plot/bin/python \
  python/rescale_query_departures.py \
  --input-dir data/BJ_Real_query_sets \
  --output-dir data/BJ_Real_query_sets_window6h \
  --window-sec 21600
```

This linearly rescales departures within each query file to `[0, 21600]`
seconds. It preserves the OD pairs and relative temporal order, unlike modulo
folding.

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
