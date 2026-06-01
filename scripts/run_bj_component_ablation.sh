#!/usr/bin/env bash
set -euo pipefail

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-16}"

CONFIG="${CONFIG:-config/config_bj_capacity2_cap10e8.yaml}"
QUERY_DIR="${QUERY_DIR:-data/BJ_Synthetic_query_sets}"
RESULTS_DIR="${RESULTS_DIR:-python/results/experiments/exp1_component_ablation/bj_synthetic_capacity2_cap10e8/raw}"
SUFFIX="${SUFFIX:-capacity2_cap10e8}"

SELECTION_METHODS="${SELECTION_METHODS:-random,most_delayed,tdg_excess}"
REROUTE_METHODS="${REROUTE_METHODS:-normal,tdg}"
FIXED_FRACTIONS="${FIXED_FRACTIONS:-1,10,30}"
TDG_GAMMAS="${TDG_GAMMAS:-25,50,90}"
IMPACT_WEIGHTS="${IMPACT_WEIGHTS:-10,30,60}"
RANDOM_SEED="${RANDOM_SEED:-0}"

HOP="${HOP:-all}"
REP="${REP:-all}"
DATASETS="${DATASETS:-}"
DATASET_LIST="${DATASET_LIST:-}"
MAX_FILES="${MAX_FILES:-0}"

CANDIDATE_FILTER="${CANDIDATE_FILTER:-all}"
CANDIDATE_THETA="${CANDIDATE_THETA:-}"
REROUTE_CONGESTION_GATE="${REROUTE_CONGESTION_GATE:-50}"
TDG_MODE="${TDG_MODE:-fine}"
CONFLICT_THRESHOLD="${CONFLICT_THRESHOLD:-}"
DELTA_COMPRESS="${DELTA_COMPRESS:-}"
ANCHOR_WINDOW="${ANCHOR_WINDOW:-}"
ANCHOR_THRESHOLD="${ANCHOR_THRESHOLD:-}"
NO_PROGRESS_LOG="${NO_PROGRESS_LOG:-0}"
DRY_RUN="${DRY_RUN:-0}"

sanitize_tag() {
  local value="$1"
  value="${value//,/__}"
  value="${value// /}"
  value="${value//\//_}"
  value="${value//[^A-Za-z0-9._-]/_}"
  printf '%s' "$value"
}

selection_tag="$(sanitize_tag "$SELECTION_METHODS")"
reroute_tag="$(sanitize_tag "$REROUTE_METHODS")"
default_label="selection_${selection_tag}_reroute_${reroute_tag}"
if [[ -n "$REROUTE_CONGESTION_GATE" ]]; then
  gate_tag="$(sanitize_tag "$REROUTE_CONGESTION_GATE")"
  default_label="${default_label}_gate${gate_tag}"
fi
if [[ "$REP" != "all" ]]; then
  default_label="${default_label}_rep${REP}"
fi
if [[ "$HOP" != "all" ]]; then
  default_label="${default_label}_hop${HOP}"
fi

LABEL="${LABEL:-$default_label}"
OUT="${OUT:-$RESULTS_DIR/gro_ablation_${LABEL}.csv}"
LOG="${LOG:-logs/bj_component_ablation_${LABEL}.log}"

if [[ "$DRY_RUN" == "0" && -n "$LOG" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

if [[ ! -x ./gro_ablation_test ]]; then
  echo "Missing ./gro_ablation_test. Build it first with: make gro_ablation_test" >&2
  exit 1
fi

mkdir -p "$(dirname "$OUT")"

cmd=(
  ./gro_ablation_test "$CONFIG"
  --query-dir "$QUERY_DIR"
  --output "$OUT"
  --selection-methods "$SELECTION_METHODS"
  --reroute-methods "$REROUTE_METHODS"
  --fixed-fractions "$FIXED_FRACTIONS"
  --tdg-gammas "$TDG_GAMMAS"
  --impact-weights "$IMPACT_WEIGHTS"
  --candidate-filter "$CANDIDATE_FILTER"
  --tdg-mode "$TDG_MODE"
  --random-seed "$RANDOM_SEED"
)

if [[ -n "$CANDIDATE_THETA" ]]; then
  cmd+=(--candidate-theta "$CANDIDATE_THETA")
fi

if [[ -n "$REROUTE_CONGESTION_GATE" ]]; then
  cmd+=(--reroute-congestion-gate "$REROUTE_CONGESTION_GATE")
fi

if [[ -n "$CONFLICT_THRESHOLD" ]]; then
  cmd+=(--conflict-threshold "$CONFLICT_THRESHOLD")
fi

if [[ -n "$DELTA_COMPRESS" ]]; then
  cmd+=(--delta-compress "$DELTA_COMPRESS")
fi

if [[ -n "$ANCHOR_WINDOW" ]]; then
  cmd+=(--anchor-window "$ANCHOR_WINDOW")
fi

if [[ -n "$ANCHOR_THRESHOLD" ]]; then
  cmd+=(--anchor-threshold "$ANCHOR_THRESHOLD")
fi

if [[ "$HOP" != "all" ]]; then
  cmd+=(--hop "$HOP")
fi

if [[ "$REP" != "all" ]]; then
  cmd+=(--rep "$REP")
fi

if [[ -n "$DATASETS" ]]; then
  cmd+=(--datasets "$DATASETS")
fi

if [[ -n "$DATASET_LIST" ]]; then
  cmd+=(--dataset-list "$DATASET_LIST")
fi

if [[ "$MAX_FILES" != "0" ]]; then
  cmd+=(--max-files "$MAX_FILES")
fi

if [[ "$NO_PROGRESS_LOG" != "0" ]]; then
  cmd+=(--no-progress-log)
fi

printf 'Output: %s\n' "$OUT"
printf 'Log: %s\n' "$LOG"
printf 'Running:'
printf ' %q' "${cmd[@]}"
printf '\n'

if [[ "$DRY_RUN" != "0" ]]; then
  exit 0
fi

"${cmd[@]}"
wc -l "$OUT"
