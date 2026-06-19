#!/usr/bin/env bash
# One-at-a-time (OAT) parameter sensitivity sweep for the GRO paper.
#
# Anchor point (paper-aligned after the batching fix, calibrated for 100k):
#   gamma = 50, lambda = 10, impact_weight = 20,
#   conflict_threshold = 5000 (= paper kappa 50; lenient at 100k scale so
#                              batching is not a moving variable in other
#                              stages), delta_compress = 1200 s,
#   anchor_threshold = 20
#
# Sweep ranges (5 values each, anchor included; OAT around anchor):
#   gamma            : 10, 25, 50, 75, 90        (--tdg-gammas)
#   lambda           : 0, 10, 25, 50, 100        (--lambda)
#   impact_weight    : 0, 5, 20, 50, 100         (--impact-weights)
#   conflict_threshold: 500, 2000, 5000, 20000, 100000  (--conflict-threshold)
#                       paper-equivalent kappa : 5, 20, 50, 200, 1000
#                       Log-spaced for 100k workloads where loads L_B(slot)
#                       on popular slots scale with batch size.
#   delta_compress   : 300, 600, 1200, 1800, 3600 s (--delta-compress)
#                       Note: Delta is shared between batching slot width and
#                       TDG compression window (paper Sec.7 & Sec.8).
#   anchor_threshold : 5, 10, 20, 40, 60         (--anchor-threshold)
#
# Workload: data/BJ_Real_query_sets_long100k_window3h (5 seeds, 100k queries,
# congestion inflation 82x-225x per the existing diagnostic CSV).
# Method  : tdg_excess_full (selection=tdg_excess, reroute=tdg_impact_reroute).
# TDG mode: compressed (paper-facing setting).
#
# Per-(parameter, value) CSV written under RESULTS_DIR/raw/. Existing CSVs are
# skipped, so the script is resumable.
#
# Override via env: PARAMS, SEEDS, QUERY_DIR, CONFIG, RESULTS_DIR, RUN_TAG,
# OMP_NUM_THREADS, DRY_RUN.

set -euo pipefail

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-16}"

CONFIG="${CONFIG:-config/config_bj_capacity2_cap10e8.yaml}"
QUERY_DIR="${QUERY_DIR:-data/BJ_Real_query_sets_scalability_inner_progressive_peak1h}"
WORKLOAD_TAG="${WORKLOAD_TAG:-bj_real_scalability_peak1h_rep1_10k}"
DATASET_REP="${DATASET_REP:-1}"
RUN_TAG="${RUN_TAG:-capacity2_cap10e8}"
RESULTS_DIR="${RESULTS_DIR:-python/results/experiments/exp4_parameter_sensitivity/${WORKLOAD_TAG}}"
LOG_DIR="${LOG_DIR:-logs}"
RAW_DIR="${RAW_DIR:-$RESULTS_DIR/raw}"

# OAT anchor.
ANCHOR_GAMMA="${ANCHOR_GAMMA:-50}"
ANCHOR_LAMBDA="${ANCHOR_LAMBDA:-10}"
ANCHOR_IMPACT="${ANCHOR_IMPACT:-20}"
ANCHOR_CONFLICT="${ANCHOR_CONFLICT:-5000}"
ANCHOR_DELTA="${ANCHOR_DELTA:-1200}"
ANCHOR_EPSILON="${ANCHOR_EPSILON:-20}"

# Sweep grids.
GAMMA_VALUES="${GAMMA_VALUES:-10 25 50 75 90}"
LAMBDA_VALUES="${LAMBDA_VALUES:-0 10 25 50 100}"
IMPACT_VALUES="${IMPACT_VALUES:-0 5 20 50 100}"
CONFLICT_VALUES="${CONFLICT_VALUES:-500 2000 5000 20000 100000}"
DELTA_VALUES="${DELTA_VALUES:-300 600 1200 1800 3600}"
EPSILON_VALUES="${EPSILON_VALUES:-5 10 20 40 60}"

# Datasets (5 seeds). Default is 10k Rep1 from the BJ scalability peak1h
# directory (used in the scalability test); override DATASET_REP for other
# query counts (e.g. DATASET_REP=10 picks the 100k Rep10 files).
SEEDS="${SEEDS:-0 1 2 3 4}"
DATASETS=""
for seed in $SEEDS; do
  if [[ -z "$DATASETS" ]]; then
    DATASETS="BJRealRep${DATASET_REP}-${seed}"
  else
    DATASETS="${DATASETS},BJRealRep${DATASET_REP}-${seed}"
  fi
done

PARAMS="${PARAMS:-gamma lambda impact conflict delta epsilon}"
DRY_RUN="${DRY_RUN:-0}"

mkdir -p "$RAW_DIR" "$LOG_DIR"

if [[ ! -x ./gro_ablation_test ]]; then
  echo "Missing ./gro_ablation_test. Build it first with: make gro_ablation_test" >&2
  exit 1
fi

base_cmd=(
  ./gro_ablation_test "$CONFIG"
  --query-dir "$QUERY_DIR"
  --datasets "$DATASETS"
  --selection-methods tdg_excess
  --reroute-methods tdg
  --candidate-filter all
  --tdg-mode compressed
  --random-seed 0
)

run_one() {
  local param="$1" value="$2"
  local out="$RAW_DIR/gro_sensitivity_${param}_${value}_${RUN_TAG}.csv"
  local log="$LOG_DIR/parameter_sensitivity_${WORKLOAD_TAG}_${param}_${value}_${RUN_TAG}.log"

  if [[ -s "$out" ]]; then
    printf '[skip] %s exists\n' "$out"
    return 0
  fi

  local cmd=("${base_cmd[@]}" --output "$out")

  case "$param" in
    gamma)
      cmd+=(--tdg-gammas "$value" --impact-weights "$ANCHOR_IMPACT" --conflict-threshold "$ANCHOR_CONFLICT" --delta-compress "$ANCHOR_DELTA" --anchor-threshold "$ANCHOR_EPSILON" --lambda "$ANCHOR_LAMBDA")
      ;;
    lambda)
      cmd+=(--tdg-gammas "$ANCHOR_GAMMA" --impact-weights "$ANCHOR_IMPACT" --conflict-threshold "$ANCHOR_CONFLICT" --delta-compress "$ANCHOR_DELTA" --anchor-threshold "$ANCHOR_EPSILON" --lambda "$value")
      ;;
    impact)
      cmd+=(--tdg-gammas "$ANCHOR_GAMMA" --impact-weights "$value" --conflict-threshold "$ANCHOR_CONFLICT" --delta-compress "$ANCHOR_DELTA" --anchor-threshold "$ANCHOR_EPSILON" --lambda "$ANCHOR_LAMBDA")
      ;;
    conflict)
      cmd+=(--tdg-gammas "$ANCHOR_GAMMA" --impact-weights "$ANCHOR_IMPACT" --conflict-threshold "$value" --delta-compress "$ANCHOR_DELTA" --anchor-threshold "$ANCHOR_EPSILON" --lambda "$ANCHOR_LAMBDA")
      ;;
    delta)
      cmd+=(--tdg-gammas "$ANCHOR_GAMMA" --impact-weights "$ANCHOR_IMPACT" --conflict-threshold "$ANCHOR_CONFLICT" --delta-compress "$value" --anchor-threshold "$ANCHOR_EPSILON" --lambda "$ANCHOR_LAMBDA")
      ;;
    epsilon)
      cmd+=(--tdg-gammas "$ANCHOR_GAMMA" --impact-weights "$ANCHOR_IMPACT" --conflict-threshold "$ANCHOR_CONFLICT" --delta-compress "$ANCHOR_DELTA" --anchor-threshold "$value" --lambda "$ANCHOR_LAMBDA")
      ;;
    *)
      echo "Unknown parameter: $param" >&2
      exit 1
      ;;
  esac

  printf '[run] %s=%s -> %s\n' "$param" "$value" "$out"
  if [[ "$DRY_RUN" != "0" ]]; then
    printf '       cmd:'
    printf ' %q' "${cmd[@]}"
    printf '\n'
    return 0
  fi

  if ! "${cmd[@]}" >>"$log" 2>&1; then
    echo "[fail] $param=$value (see $log)" >&2
    rm -f "$out"
    return 1
  fi
}

sweep_param() {
  local param="$1"
  local values_var
  case "$param" in
    gamma)    values_var="$GAMMA_VALUES" ;;
    lambda)   values_var="$LAMBDA_VALUES" ;;
    impact)   values_var="$IMPACT_VALUES" ;;
    conflict) values_var="$CONFLICT_VALUES" ;;
    delta)    values_var="$DELTA_VALUES" ;;
    epsilon)  values_var="$EPSILON_VALUES" ;;
    *)
      echo "Unknown parameter: $param" >&2
      exit 1
      ;;
  esac

  printf '\n=== Sweeping %s over: %s ===\n' "$param" "$values_var"
  for value in $values_var; do
    run_one "$param" "$value"
  done
}

printf 'Workload : %s\n' "$QUERY_DIR"
printf 'Datasets : %s\n' "$DATASETS"
printf 'Anchor   : gamma=%s lambda=%s impact=%s conflict=%s delta=%s epsilon=%s\n' \
  "$ANCHOR_GAMMA" "$ANCHOR_LAMBDA" "$ANCHOR_IMPACT" "$ANCHOR_CONFLICT" "$ANCHOR_DELTA" "$ANCHOR_EPSILON"
printf 'Params   : %s\n' "$PARAMS"
printf 'Raw dir  : %s\n' "$RAW_DIR"
printf 'Threads  : %s\n' "$OMP_NUM_THREADS"

for param in $PARAMS; do
  sweep_param "$param"
done

printf '\nDone. CSVs under %s\n' "$RAW_DIR"
