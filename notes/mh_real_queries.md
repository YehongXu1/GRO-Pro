# Manhattan Real Taxi Query Sets

## Goal

Build real Manhattan taxi-derived query sets for GRO experiments from public NYC
Taxi and Limousine Commission (TLC) trip records.

This is separate from the existing synthetic Manhattan workload in
`data/MH_Synthetic_query_sets`. The target output format remains the project
standard:

```text
origin_node destination_node departure_time_seconds
```

## Source Choice

The preferred source is historical NYC yellow taxi trip records with explicit
pickup and dropoff longitude/latitude. These records are better for route-level
experiments because each trip can be snapped directly to `data/MH.txt` through
`data/MH_NodeIDLonLat.txt`.

Modern TLC Parquet files are official and convenient, but recent schemas use
TLC taxi zones:

```text
PULocationID
DOLocationID
```

Zone-level data can be mapped through the TLC taxi zone shapefile or zone
centroids, but this produces coarser OD points and may concentrate many trips at
the same snapped graph nodes. Use it as a fallback or fast prototype, not the
preferred paper-facing route-level workload.

Official source references:

```text
https://www.nyc.gov/site/tlc/about/tlc-trip-record-data.page
https://www.nyc.gov/assets/tlc/downloads/pdf/data_dictionary_trip_records_yellow.pdf
https://catalog.data.gov/dataset/2013-yellow-taxi-trip-data
```

## Proposed Derivation

1. Load `data/MH.txt` and `data/MH_NodeIDLonLat.txt`.
2. Read public NYC yellow taxi trip records.
3. Prefer records with explicit pickup/dropoff coordinates.
4. Filter invalid trips:
   - missing or zero coordinates;
   - pickup/dropoff outside the MH graph bounding box or Manhattan study area;
   - nonpositive duration;
   - unreasonable duration or trip distance;
   - origin and destination snapping to the same graph node.
5. Snap pickup and dropoff coordinates to the nearest MH graph node.
6. Store departure time as seconds relative to the earliest selected pickup
   time.
7. Sample disjoint base query sets, then create controlled repetitions:

```text
MHRealRep1-{seed}.txt    10,000 queries
MHRealRep5-{seed}.txt    50,000 queries
MHRealRep10-{seed}.txt   100,000 queries
```

The exact base query count, number of seeds, and repetition factors should match
the BJ real query convention unless diagnostics show that MH needs a different
range.

## Candidate Time Windows

Keep at least two versions:

- original relative departure span from the selected taxi records;
- a controlled peak window, initially `[0, 21600]` seconds, mirroring the BJ
  `window6h` setting.

Avoid one-hour modulo folding as a default paper setting. If a one-hour peak is
needed, generate it by controlled linear rescaling and validate it with
shortest-path congestion diagnostics before use.

## Selection for Overall Effectiveness

After generation, run the same shortest-path congestion diagnostic used for BJ:

```text
free-flow shortest-path route set
evaluated through the project BPR traffic evaluator
inflation_ratio = evaluated_ttt / free_flow_ttt
```

Select three 100k representatives:

```text
lower    weak-to-moderate congestion
middle   moderate/high congestion
extreme  high but defensible congestion
```

For paper-facing settings, prefer representatives with enough improvement room
but avoid pathological workloads. The BJ scalability gate uses `10x-100x`; for
overall effectiveness, a lower-congestion representative can be below `10x` if
it is explicitly labeled as the lower level.

## Paper Baselines

The MH real six-hour window paper baselines can be launched with:

```bash
make paper_baseline_test

LOG=logs/paper_baselines_mh_real_window6h_rep10.log \
nohup bash scripts/run_mh_real_window6h_paper_baselines.sh \
  > /dev/null 2>&1 < /dev/null &
```

By default this runs `svp,gor,sor,fahl` on all five
`data/MH_Real_query_sets_window6h/MHRealRep10-*.txt` datasets with
`config/config.yaml`, and writes:

```text
python/results/experiments/exp5_overall_effectiveness/paper_baselines_mh_real_window6h_rep10.csv
```

Useful overrides:

```bash
METHODS=sor
REP=5
MAX_FILES=1
MAX_QUERIES=10000
OUT=path/to/output.csv
LOG=path/to/log
```

## Generator

A coordinate-level CSV generator has been added:

```text
python/generate_mh_real_queries_from_tlc.py
```

It streams CSV, CSV.GZ, or ZIP-contained CSV files using only the Python
standard library. It expects explicit pickup/dropoff longitude and latitude
columns and intentionally rejects zone-only inputs. Example command:

```bash
python python/generate_mh_real_queries_from_tlc.py \
  --tlc-file path/to/yellow_tripdata_coordinate_level.csv \
  --input-format header \
  --graph data/MH.txt \
  --coordinates data/MH_NodeIDLonLat.txt \
  --output-dir data/MH_Real_query_sets \
  --sets 5 \
  --queries-per-set 10000 \
  --rep-values 1,5,10 \
  --max-snap-distance-m 250 \
  --min-duration-min 2 \
  --max-duration-min 60 \
  --min-distance-km 0.5 \
  --max-distance-km 30 \
  --random-seed 0
```

For the local NCL/DEBS-style anonymous excerpt:

```bash
python python/generate_mh_real_queries_from_tlc.py \
  --tlc-file data/MH_Real_query_sets/nyc_tlc/anondata.csv \
  --input-format ncl-anondata \
  --graph data/MH.txt \
  --coordinates data/MH_NodeIDLonLat.txt \
  --output-dir data/MH_Real_query_sets \
  --sets 5 \
  --queries-per-set 10000 \
  --rep-values 1,5,10 \
  --max-snap-distance-m 250 \
  --min-duration-min 2 \
  --max-duration-min 60 \
  --min-distance-km 0.5 \
  --max-distance-km 30 \
  --random-seed 0
```

For a first smoke generation, add:

```bash
--stop-after-required
```

For paper-facing data, prefer processing the full selected source file(s)
without `--stop-after-required`, so reservoir sampling covers the entire input.

## Status

This workload has not been generated yet. A raw coordinate-level NYC TLC yellow
taxi source file still needs to be downloaded or pointed to locally. After
generation, the script writes metadata similar to the BJ real generator,
including raw source file names, filter thresholds, snap radius, query counts,
and per-file departure spans.
