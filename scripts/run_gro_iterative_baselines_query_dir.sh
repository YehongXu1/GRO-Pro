#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-32}"

CONFIG="${CONFIG:-config/config.yaml}"
QUERY_DIR="${QUERY_DIR:?QUERY_DIR is required}"
RESULTS_DIR="${RESULTS_DIR:-python/results/experiments/exp5_overall_effectiveness}"
REP="${REP:-all}"
MAX_FILES="${MAX_FILES:-0}"
FIXED_FRACTIONS="${FIXED_FRACTIONS:-10}"
SELECTION_METHODS="${SELECTION_METHODS:-random,most_delayed}"
REROUTE_METHODS="${REROUTE_METHODS:-normal}"
TDG_GAMMAS_PLACEHOLDER="${TDG_GAMMAS_PLACEHOLDER:-50}"
IMPACT_WEIGHTS_PLACEHOLDER="${IMPACT_WEIGHTS_PLACEHOLDER:-30}"
RANDOM_SEED="${RANDOM_SEED:-0}"
DRY_RUN="${DRY_RUN:-0}"

OUT="${OUT:-$RESULTS_DIR/gro_iterative_baselines.csv}"

mkdir -p "$(dirname "$OUT")"

cmd=(
  ./gro_ablation_test "$CONFIG"
  --query-dir "$QUERY_DIR"
  --output "$OUT"
  --selection-methods "$SELECTION_METHODS"
  --reroute-methods "$REROUTE_METHODS"
  --fixed-fractions "$FIXED_FRACTIONS"
  --tdg-gammas "$TDG_GAMMAS_PLACEHOLDER"
  --impact-weights "$IMPACT_WEIGHTS_PLACEHOLDER"
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
