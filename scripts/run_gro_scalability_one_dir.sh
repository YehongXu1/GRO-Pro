#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-24}"

CONFIG="${CONFIG:-config/config_bj_capacity2_cap10e8.yaml}"
QUERY_DIR="${QUERY_DIR:?Set QUERY_DIR to a scalability query directory}"
LABEL="${LABEL:?Set LABEL for output filenames, e.g. original or window6h}"
RESULTS_DIR="${RESULTS_DIR:-python/results/experiments/exp3_compression_scalability}"
REPS="${REPS:-1,2,3,5,7,10}"
TDG_GAMMAS="${TDG_GAMMAS:-90}"
IMPACT_WEIGHTS="${IMPACT_WEIGHTS:-20}"
RANDOM_SEED="${RANDOM_SEED:-0}"
SELECTION_METHODS="${SELECTION_METHODS:-tdg_excess}"
REROUTE_METHODS="${REROUTE_METHODS:-tdg}"
FIXED_FRACTIONS="${FIXED_FRACTIONS:-10}"
SUFFIX="${SUFFIX:-capacity2_cap10e8}"

TMPDIR="$RESULTS_DIR/tmp_gro_scalability_${LABEL}_tdg_excess_full_${SUFFIX}"
OUT="$RESULTS_DIR/gro_scalability_${LABEL}_tdg_excess_full_${SUFFIX}.csv"

mkdir -p "$TMPDIR" "$RESULTS_DIR"

IFS=',' read -r -a REP_VALUES <<< "$REPS"

first=1
for rep in "${REP_VALUES[@]}"; do
  rep="$(echo "$rep" | xargs)"
  if [[ -z "$rep" ]]; then
    continue
  fi

  REP_OUT="$TMPDIR/rep${rep}.csv"
  echo "[run] label=$LABEL rep=$rep query_dir=$QUERY_DIR output=$REP_OUT"
  ./gro_ablation_test "$CONFIG" \
    --query-dir "$QUERY_DIR" \
    --rep "$rep" \
    --output "$REP_OUT" \
    --selection-methods "$SELECTION_METHODS" \
    --reroute-methods "$REROUTE_METHODS" \
    --fixed-fractions "$FIXED_FRACTIONS" \
    --tdg-gammas "$TDG_GAMMAS" \
    --impact-weights "$IMPACT_WEIGHTS" \
    --random-seed "$RANDOM_SEED"

  if [[ "$first" -eq 1 ]]; then
    head -n 1 "$REP_OUT" > "$OUT"
    first=0
  fi
  tail -n +2 "$REP_OUT" >> "$OUT"
  echo "[done] label=$LABEL rep=$rep rows=$(wc -l < "$REP_OUT")"
done

echo "[merged] $OUT"
wc -l "$OUT"
