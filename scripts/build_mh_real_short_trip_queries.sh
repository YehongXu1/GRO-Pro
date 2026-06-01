#!/usr/bin/env bash
#
# Regenerate MH real-world query sets with a short-trip cap, then rebuild the
# inner-progressive peak1h scalability workload from them. Overwrites:
#   data/MH_Real_query_sets/MHRealRep{1,5,10}-{0..4}.txt
#   data/MH_Real_query_sets_scalability_inner_progressive_peak1h/...
#
# Required env var:
#   TLC_FILE    Path to NYC TLC yellow-taxi CSV (.csv, .csv.gz, or .zip).
#               If you have multiple files, set TLC_DIR + TLC_PATTERN instead.
#   TLC_DIR     Alternative to TLC_FILE: directory containing TLC CSVs.
#   TLC_PATTERN Glob inside TLC_DIR (default '*.csv*').
#
# Optional env vars:
#   MAX_DIST_KM            haversine cap on OD distance, in km (default 1.5)
#   MIN_DIST_KM            minimum OD distance, in km (default 0.5)
#   QUERIES_PER_SET        rows per seed (default 10000)
#   SETS                   number of seed files (default 5)
#   REP_VALUES             rep tags to write in MHRealRepN-*.txt (default 1,5,10)
#   SOURCE_DIR             output dir for raw MH query sets
#                          (default data/MH_Real_query_sets)
#   SCAL_DIR               output dir for scalability workload
#                          (default data/MH_Real_query_sets_scalability_inner_progressive_peak1h)
#   SIZE_SPECS             per-size source_count / center radius / window
#                          (default matches the existing MH peak1h workload)
#   SEEDS                  seeds for the scalability step (default 0,1,2,3,4)
#   CENTER_LON, CENTER_LAT center for OD-radius filter (default Times Square)
#   RANDOM_SEED_TLC        random seed for TLC step (default 0)
#   RANDOM_SEED_SCAL       random seed for scalability step (default 20260601)
#   LOG                    if set, the whole script redirects stdout+stderr there
#
# Run in background with nohup:
#   TLC_FILE=/path/to/yellow_tripdata.csv \
#   LOG=logs/mh_regen_short_trips.log \
#   nohup bash scripts/build_mh_real_short_trip_queries.sh \
#     > /dev/null 2>&1 < /dev/null &
#
# Monitor:
#   tail -f logs/mh_regen_short_trips.log

set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

if [[ -z "${TLC_FILE:-}" && -z "${TLC_DIR:-}" ]]; then
  echo "ERROR: set TLC_FILE=<path> or TLC_DIR=<dir> to point at NYC TLC yellow-taxi data" >&2
  exit 2
fi

MAX_DIST_KM="${MAX_DIST_KM:-1.5}"
MIN_DIST_KM="${MIN_DIST_KM:-0.5}"
QUERIES_PER_SET="${QUERIES_PER_SET:-10000}"
SETS="${SETS:-5}"
REP_VALUES="${REP_VALUES:-1,5,10}"
SOURCE_DIR="${SOURCE_DIR:-data/MH_Real_query_sets}"
SCAL_DIR="${SCAL_DIR:-data/MH_Real_query_sets_scalability_inner_progressive_peak1h}"
SIZE_SPECS="${SIZE_SPECS:-10000:800:2.0:3600,20000:1400:2.0:3600,30000:1800:2.0:3600,50000:3000:3.0:3600,70000:4500:3.0:3600,100000:7000:4.0:3600}"
SEEDS="${SEEDS:-0,1,2,3,4}"
CENTER_LON="${CENTER_LON:--73.9855}"
CENTER_LAT="${CENTER_LAT:-40.758}"
RANDOM_SEED_TLC="${RANDOM_SEED_TLC:-0}"
RANDOM_SEED_SCAL="${RANDOM_SEED_SCAL:-20260601}"

echo "[config] MAX_DIST_KM=$MAX_DIST_KM MIN_DIST_KM=$MIN_DIST_KM QUERIES_PER_SET=$QUERIES_PER_SET SETS=$SETS"
echo "[config] SOURCE_DIR=$SOURCE_DIR  SCAL_DIR=$SCAL_DIR"
echo "[config] SIZE_SPECS=$SIZE_SPECS"
echo "[config] CENTER=($CENTER_LON, $CENTER_LAT)"
echo

# ---------------------------------------------------------------------------
# Step 1: regenerate MH source query sets from TLC raw data with the short cap
# ---------------------------------------------------------------------------
echo "[step 1] regenerating MH source query sets from TLC (max OD distance = ${MAX_DIST_KM} km)"
TLC_ARGS=()
if [[ -n "${TLC_FILE:-}" ]]; then
  TLC_ARGS+=(--tlc-file "$TLC_FILE")
fi
if [[ -n "${TLC_DIR:-}" ]]; then
  TLC_ARGS+=(--tlc-dir "$TLC_DIR")
  TLC_ARGS+=(--pattern "${TLC_PATTERN:-*.csv*}")
fi

python python/generate_mh_real_queries_from_tlc.py \
  "${TLC_ARGS[@]}" \
  --input-format header \
  --graph data/MH.txt \
  --coordinates data/MH_NodeIDLonLat.txt \
  --output-dir "$SOURCE_DIR" \
  --sets "$SETS" --queries-per-set "$QUERIES_PER_SET" \
  --rep-values "$REP_VALUES" \
  --max-snap-distance-m 250 \
  --bbox-padding-km 0.5 \
  --min-duration-min 2.0 --max-duration-min 60 \
  --min-distance-km "$MIN_DIST_KM" \
  --max-distance-km "$MAX_DIST_KM" \
  --random-seed "$RANDOM_SEED_TLC"

echo "[step 1] done"
echo

# ---------------------------------------------------------------------------
# Step 2: build the inner-progressive peak1h scalability workload from the
# newly regenerated MH source files.
# ---------------------------------------------------------------------------
echo "[step 2] building scalability workload at $SCAL_DIR"
python3 python/build_bj_real_high_congestion_scalability_queries.py \
  --source-label "mh_real_high_congestion_scalability" \
  --dataset-prefix MHReal \
  --input-dir "$SOURCE_DIR" \
  --output-dir "$SCAL_DIR" \
  --coords data/MH_NodeIDLonLat.txt \
  --center-lon "$CENTER_LON" --center-lat "$CENTER_LAT" \
  --size-specs "$SIZE_SPECS" \
  --seeds "$SEEDS" \
  --random-seed "$RANDOM_SEED_SCAL"

echo "[step 2] done"
echo
echo "[summary] new short-trip MH scalability workload is in $SCAL_DIR"
echo "         next: run shortest_path_congestion_diagnostic to verify inflation"
