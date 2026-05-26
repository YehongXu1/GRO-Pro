#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-32}"

CONFIG="${CONFIG:-config/config.yaml}"
QUERY_DIR="${QUERY_DIR:-data/MH_Real_query_sets_window6h}"
RESULTS_DIR="${RESULTS_DIR:-python/results/experiments/exp5_overall_effectiveness}"
REP="${REP:-all}"
REP_TAG="${REP_TAG:-${REP}}"
MAX_FILES="${MAX_FILES:-0}"
TDG_GAMMAS="${TDG_GAMMAS:-50}"
IMPACT_WEIGHTS="${IMPACT_WEIGHTS:-20}"
FIXED_FRACTIONS="${FIXED_FRACTIONS:-10}"
RANDOM_SEED="${RANDOM_SEED:-0}"
CONFLICT_THRESHOLD="${CONFLICT_THRESHOLD:-5000}"
DRY_RUN="${DRY_RUN:-0}"

OUT="${OUT:-$RESULTS_DIR/gro_score_top_compressed_mh_real_window6h_${REP_TAG}.csv}"

mkdir -p "$RESULTS_DIR"

cmd=(
  ./gro_ablation_test "$CONFIG"
  --query-dir "$QUERY_DIR"
  --output "$OUT"
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

if [[ "$REP" != "all" ]]; then
  cmd+=(--rep "$REP")
fi

if [[ "$MAX_FILES" != "0" ]]; then
  cmd+=(--max-files "$MAX_FILES")
fi

printf 'Running:'
printf ' %q' "${cmd[@]}"
printf '\n'

if [[ "$DRY_RUN" != "0" ]]; then
  exit 0
fi

"${cmd[@]}"
wc -l "$OUT"
