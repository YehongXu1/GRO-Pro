#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-32}"

CONFIG="${CONFIG:-config/config_bj_capacity2_cap10e8_iter5.yaml}"
RESULTS_DIR="${RESULTS_DIR:-python/results/experiments/exp5_overall_effectiveness}"
RUN_TMPDIR="${RUN_TMPDIR:-$RESULTS_DIR/tmp_gro_score_top_compressed_bj_selected_real_capacity2_cap10e8}"
OUT="${OUT:-$RESULTS_DIR/gro_score_top_compressed_bj_selected_real_capacity2_cap10e8.csv}"

TDG_GAMMAS="${TDG_GAMMAS:-50}"
IMPACT_WEIGHTS="${IMPACT_WEIGHTS:-20}"
FIXED_FRACTIONS="${FIXED_FRACTIONS:-10}"
RANDOM_SEED="${RANDOM_SEED:-0}"
CONFLICT_THRESHOLD="${CONFLICT_THRESHOLD:-5000}"
DRY_RUN="${DRY_RUN:-0}"

mkdir -p "$RUN_TMPDIR" "$RESULTS_DIR"

declare -a labels=(
  "lower_original_BJRealRep10-2"
  "middle_window6h_BJRealRep10-2"
  "extreme_window6h_BJRealRep10-4"
)

declare -a query_files=(
  "data/BJ_Real_query_sets/BJRealRep10-2.txt"
  "data/BJ_Real_query_sets_window6h/BJRealRep10-2.txt"
  "data/BJ_Real_query_sets_window6h/BJRealRep10-4.txt"
)

first=1
for index in "${!query_files[@]}"; do
  label="${labels[$index]}"
  query_file="${query_files[$index]}"
  run_out="$RUN_TMPDIR/${label}.csv"

  cmd=(
    ./gro_ablation_test "$CONFIG"
    --query-file "$query_file"
    --output "$run_out"
    --selection-methods tdg_excess
    --reroute-methods tdg
    --fixed-fractions "$FIXED_FRACTIONS"
    --tdg-gammas "$TDG_GAMMAS"
    --impact-weights "$IMPACT_WEIGHTS"
    --candidate-filter score_top
    --tdg-mode compressed
    --conflict-threshold "$CONFLICT_THRESHOLD"
    --random-seed "$RANDOM_SEED"
  )

  printf '[run] %s:' "$label"
  printf ' %q' "${cmd[@]}"
  printf '\n'

  if [[ "$DRY_RUN" != "0" ]]; then
    continue
  fi

  "${cmd[@]}"
  if [[ "$first" -eq 1 ]]; then
    head -n 1 "$run_out" > "$OUT"
    first=0
  fi
  tail -n +2 "$run_out" >> "$OUT"
  echo "[done] $label rows=$(wc -l < "$run_out")"
done

if [[ "$DRY_RUN" == "0" ]]; then
  echo "[merged] $OUT"
  wc -l "$OUT"
fi
