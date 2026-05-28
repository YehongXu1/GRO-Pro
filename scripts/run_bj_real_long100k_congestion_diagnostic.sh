#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

CONFIG="${CONFIG:-config/config_bj_capacity2_cap10e8.yaml}"
BASE_QUERY_DIR="${BASE_QUERY_DIR:-data/BJ_Real_query_sets_long100k}"
WINDOW_LABELS="${WINDOW_LABELS:-original,window6h,window3h,window2h,window1h}"
RESULTS_DIR="${RESULTS_DIR:-python/results/experiments/exp5_overall_effectiveness}"
TMPDIR_RESULTS="${TMPDIR_RESULTS:-$RESULTS_DIR/tmp_bj_real_long100k_congestion_diagnostic}"
OUT="${OUT:-$RESULTS_DIR/bj_real_long100k_congestion_candidates.csv}"
SUMMARY_OUT="${SUMMARY_OUT:-$RESULTS_DIR/bj_real_long100k_congestion_candidates_summary.csv}"
THREADS="${THREADS:-0}"
DRY_RUN="${DRY_RUN:-0}"

mkdir -p "$TMPDIR_RESULTS" "$RESULTS_DIR"

cmd=(make shortest_path_congestion_diagnostic)
printf '[build]:'
printf ' %q' "${cmd[@]}"
printf '\n'
if [[ "$DRY_RUN" == "0" ]]; then
  "${cmd[@]}"
fi

first=1
IFS=',' read -r -a labels <<< "$WINDOW_LABELS"
for label in "${labels[@]}"; do
  if [[ "$label" == "original" ]]; then
    query_dir="$BASE_QUERY_DIR"
  else
    query_dir="${BASE_QUERY_DIR}_${label}"
  fi
  run_out="$TMPDIR_RESULTS/${label}.csv"
  cmd=(
    ./shortest_path_congestion_diagnostic "$CONFIG"
    --query-dir "$query_dir"
    --output "$run_out"
    --rep 10
  )

  printf '[diagnose] %s:' "$label"
  printf ' %q' "${cmd[@]}"
  printf '\n'

  if [[ "$DRY_RUN" != "0" ]]; then
    continue
  fi

  if [[ "$THREADS" != "0" ]]; then
    OMP_NUM_THREADS="$THREADS" "${cmd[@]}"
  else
    "${cmd[@]}"
  fi
  if [[ "$first" -eq 1 ]]; then
    awk -v label="$label" 'NR == 1 { print "window_label," $0; next } { print label "," $0 }' "$run_out" > "$OUT"
    first=0
  else
    awk -v label="$label" 'NR > 1 { print label "," $0 }' "$run_out" >> "$OUT"
  fi
done

if [[ "$DRY_RUN" == "0" ]]; then
  python3 python/summarize_shortest_path_congestion.py \
    --input "$OUT" \
    --summary-output "$SUMMARY_OUT"
  echo "[wrote] $OUT"
  echo "[wrote] $SUMMARY_OUT"
fi
