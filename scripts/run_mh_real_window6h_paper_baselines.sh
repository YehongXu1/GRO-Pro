#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-32}"

CONFIG="${CONFIG:-config/config.yaml}"
QUERY_DIR="${QUERY_DIR:-data/MH_Real_query_sets_window6h}"
METHODS="${METHODS:-svp,gor,sor,fahl}"
REP="${REP:-all}"
REP_TAG="${REP_TAG:-${REP}}"
MAX_FILES="${MAX_FILES:-0}"
MAX_QUERIES="${MAX_QUERIES:-0}"
SOR_MAX_LABELS="${SOR_MAX_LABELS:-20000}"
METHOD_TAG="${METHOD_TAG:-${METHODS//,/_}}"
DRY_RUN="${DRY_RUN:-0}"

OUT="${OUT:-python/results/experiments/exp5_overall_effectiveness/paper_baseline_${METHOD_TAG}_mh_real_window6h_${REP_TAG}.csv}"

mkdir -p "$(dirname "$OUT")"

cmd=(
  ./paper_baseline_test "$CONFIG"
  --query-dir "$QUERY_DIR"
  --output "$OUT"
  --methods "$METHODS"
  --svp-k 3
  --svp-theta 80
  --sor-detour-percent 10
  --sor-time-step 60
  --sor-max-time-steps 10080
  --sor-max-labels-per-query "$SOR_MAX_LABELS"
  --sor-lower-bound-cache-size -1
  --sor-lower-bound-cache-min-frequency 1
  --fahl-alpha-percent 50
  --fahl-time-step 60
  --fahl-order-beta-percent 70
)

if [[ "$REP" != "all" ]]; then
  cmd+=(--rep "$REP")
fi

if [[ "$MAX_FILES" != "0" ]]; then
  cmd+=(--max-files "$MAX_FILES")
fi

if [[ "$MAX_QUERIES" != "0" ]]; then
  cmd+=(--max-queries "$MAX_QUERIES")
fi

printf 'Running:'
printf ' %q' "${cmd[@]}"
printf '\n'

if [[ "$DRY_RUN" != "0" ]]; then
  exit 0
fi

"${cmd[@]}"
wc -l "$OUT"
