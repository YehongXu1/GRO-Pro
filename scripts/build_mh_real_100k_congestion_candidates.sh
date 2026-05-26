#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

SOURCE_DIR="${SOURCE_DIR:-data/MH_Real_query_sets}"
PATTERN="${PATTERN:-MHRealRep10-*.txt}"
WINDOWS="${WINDOWS:-21600,10800,7200,3600,1800}"
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

IFS=',' read -r -a window_values <<< "$WINDOWS"
for window_sec in "${window_values[@]}"; do
  label="$(window_label "$window_sec")"
  output_dir="data/MH_Real_query_sets_100k_${label}"
  cmd=(
    python3 python/rescale_query_departures.py
    --input-dir "$SOURCE_DIR"
    --output-dir "$output_dir"
    --pattern "$PATTERN"
    --window-sec "$window_sec"
  )

  printf '[build] %s:' "$output_dir"
  printf ' %q' "${cmd[@]}"
  printf '\n'

  if [[ "$DRY_RUN" == "0" ]]; then
    "${cmd[@]}"
  fi
done
