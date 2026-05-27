#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

export PYTHONUNBUFFERED="${PYTHONUNBUFFERED:-1}"

TDRIVE_INPUT="${TDRIVE_INPUT:-data/BJ_Real_query_sets/T-drive Taxi Trajectories.zip}"
BASE_OUTPUT="${BASE_OUTPUT:-data/BJ_Real_query_sets_long100k}"
WINDOWS="${WINDOWS:-21600,10800,7200,3600}"
MAX_FILES="${MAX_FILES:-1000}"
SETS="${SETS:-5}"
QUERIES_PER_SET="${QUERIES_PER_SET:-10000}"
REP_VALUES="${REP_VALUES:-10}"
REGION_RADIUS_KM="${REGION_RADIUS_KM:-20}"
MIN_DURATION_MIN="${MIN_DURATION_MIN:-10}"
MAX_DURATION_MIN="${MAX_DURATION_MIN:-90}"
MIN_DISTANCE_KM="${MIN_DISTANCE_KM:-4}"
MAX_DISTANCE_KM="${MAX_DISTANCE_KM:-40}"
MIN_FREE_FLOW_MIN="${MIN_FREE_FLOW_MIN:-8}"
MAX_FREE_FLOW_MIN="${MAX_FREE_FLOW_MIN:-60}"
MAX_END_POINTS_PER_START="${MAX_END_POINTS_PER_START:-80}"
PROGRESS_INTERVAL="${PROGRESS_INTERVAL:-25}"
DRY_RUN="${DRY_RUN:-0}"

window_label() {
  local seconds="$1"
  if (( seconds % 3600 == 0 )); then
    printf 'window%dh' "$((seconds / 3600))"
  elif (( seconds % 60 == 0 )); then
    printf 'window%dmin' "$((seconds / 60))"
  else
    printf 'window%ds' "$seconds"
  fi
}

generate_cmd=(
  python3 python/generate_bj_real_queries_from_tdrive.py
  --tdrive-dir "$TDRIVE_INPUT"
  --output-dir "$BASE_OUTPUT"
  --sets "$SETS"
  --queries-per-set "$QUERIES_PER_SET"
  --rep-values "$REP_VALUES"
  --region-radius-km "$REGION_RADIUS_KM"
  --min-duration-min "$MIN_DURATION_MIN"
  --max-duration-min "$MAX_DURATION_MIN"
  --min-distance-km "$MIN_DISTANCE_KM"
  --max-distance-km "$MAX_DISTANCE_KM"
  --min-free-flow-min "$MIN_FREE_FLOW_MIN"
  --max-free-flow-min "$MAX_FREE_FLOW_MIN"
  --max-end-points-per-start "$MAX_END_POINTS_PER_START"
  --max-files "$MAX_FILES"
  --shuffle-files
  --progress-interval "$PROGRESS_INTERVAL"
  --random-seed 0
)

printf '[build] base long100k:'
printf ' %q' "${generate_cmd[@]}"
printf '\n'
if [[ "$DRY_RUN" == "0" ]]; then
  "${generate_cmd[@]}"
fi

IFS=',' read -r -a window_values <<< "$WINDOWS"
for window_sec in "${window_values[@]}"; do
  label="$(window_label "$window_sec")"
  output_dir="${BASE_OUTPUT}_${label}"
  rescale_cmd=(
    python3 python/rescale_query_departures.py
    --input-dir "$BASE_OUTPUT"
    --output-dir "$output_dir"
    --pattern 'BJRealRep10-*.txt'
    --window-sec "$window_sec"
  )

  printf '[build] %s:' "$output_dir"
  printf ' %q' "${rescale_cmd[@]}"
  printf '\n'
  if [[ "$DRY_RUN" == "0" ]]; then
    "${rescale_cmd[@]}"
  fi
done
