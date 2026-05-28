#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-32}"

CONFIG="${CONFIG:-config/config.yaml}"
WINDOW_LABELS="${WINDOW_LABELS:-window6h,window3h,window2h,window1h,window30min}"
RUNS="${RUNS:-score_top}"
RESULTS_DIR="${RESULTS_DIR:-python/results/experiments/exp5_overall_effectiveness/}"
SCORE_TOP_DIR="${SCORE_TOP_DIR:-$RESULTS_DIR/gro_score_top_compressed_mh_real}"
GRO_BASELINE_DIR="${GRO_BASELINE_DIR:-$RESULTS_DIR/gro_baseline_random_delayed_normal_mh_real}"
LOG_DIR="${LOG_DIR:-logs}"
DRY_RUN="${DRY_RUN:-0}"
SKIP_BUILD="${SKIP_BUILD:-0}"

TDG_GAMMAS="${TDG_GAMMAS:-50}"
IMPACT_WEIGHTS="${IMPACT_WEIGHTS:-20}"
FIXED_FRACTIONS="${FIXED_FRACTIONS:-10}"
RANDOM_SEED="${RANDOM_SEED:-0}"
CONFLICT_THRESHOLD="${CONFLICT_THRESHOLD:-5000}"

BASELINE_SELECTION_METHODS="${BASELINE_SELECTION_METHODS:-random,most_delayed}"
BASELINE_REROUTE_METHODS="${BASELINE_REROUTE_METHODS:-normal}"
BASELINE_IMPACT_WEIGHTS_PLACEHOLDER="${BASELINE_IMPACT_WEIGHTS_PLACEHOLDER:-30}"

mkdir -p "$RESULTS_DIR" "$SCORE_TOP_DIR" "$GRO_BASELINE_DIR" "$LOG_DIR"

IFS=',' read -r -a run_values <<< "$RUNS"
IFS=',' read -r -a window_values <<< "$WINDOW_LABELS"

run_enabled() {
  local needle="$1"
  local value
  for value in "${run_values[@]}"; do
    value="${value//[[:space:]]/}"
    if [[ "$value" == "$needle" ]]; then
      return 0
    fi
  done
  return 1
}

print_cmd() {
  printf ' %q' "$@"
  printf '\n'
}

if [[ "$SKIP_BUILD" != "1" ]]; then
  echo "[build] make gro_ablation_test"
  if [[ "$DRY_RUN" == "0" ]]; then
    make gro_ablation_test
  fi
fi

for label in "${window_values[@]}"; do
  label="${label//[[:space:]]/}"
  [[ -z "$label" ]] && continue

  query_dir="data/MH_Real_query_sets_100k_${label}"
  if [[ "$DRY_RUN" == "0" && ! -d "$query_dir" ]]; then
    echo "[error] missing query dir: $query_dir" >&2
    echo "Run scripts/build_mh_real_100k_congestion_candidates.sh first." >&2
    exit 1
  fi

  if run_enabled score_top; then
    out="$SCORE_TOP_DIR/${label}.csv"
    child_log="$LOG_DIR/gro_score_top_compressed_mh_real_100k_${label}.log"
    cmd=(
      env
      CONFIG="$CONFIG"
      QUERY_DIR="$query_dir"
      REP=10
      REP_TAG="100k_${label}"
      OUT="$out"
      LOG="$child_log"
      TDG_GAMMAS="$TDG_GAMMAS"
      IMPACT_WEIGHTS="$IMPACT_WEIGHTS"
      FIXED_FRACTIONS="$FIXED_FRACTIONS"
      RANDOM_SEED="$RANDOM_SEED"
      CONFLICT_THRESHOLD="$CONFLICT_THRESHOLD"
      DRY_RUN="$DRY_RUN"
      bash scripts/run_gro_score_top_compressed_query_dir.sh
    )
    printf '[run] score_top %s:' "$label"
    print_cmd "${cmd[@]}"
    if [[ "$DRY_RUN" == "0" ]]; then
      "${cmd[@]}"
      echo "[done] score_top $label output=$out log=$child_log"
    fi
  fi

  if run_enabled baseline; then
    out="$GRO_BASELINE_DIR/${label}.csv"
    child_log="$LOG_DIR/gro_baseline_random_delayed_normal_mh_real_100k_${label}.log"
    cmd=(
      env
      CONFIG="$CONFIG"
      QUERY_DIR="$query_dir"
      REP=10
      REP_TAG="100k_${label}"
      OUT="$out"
      LOG="$child_log"
      FIXED_FRACTIONS="$FIXED_FRACTIONS"
      SELECTION_METHODS="$BASELINE_SELECTION_METHODS"
      REROUTE_METHODS="$BASELINE_REROUTE_METHODS"
      TDG_GAMMAS_PLACEHOLDER="$TDG_GAMMAS"
      IMPACT_WEIGHTS_PLACEHOLDER="$BASELINE_IMPACT_WEIGHTS_PLACEHOLDER"
      RANDOM_SEED="$RANDOM_SEED"
      DRY_RUN="$DRY_RUN"
      bash scripts/run_gro_iterative_baselines_query_dir.sh
    )
    printf '[run] baseline %s:' "$label"
    print_cmd "${cmd[@]}"
    if [[ "$DRY_RUN" == "0" ]]; then
      "${cmd[@]}"
      echo "[done] baseline $label output=$out log=$child_log"
    fi
  fi
done

echo "[all_done] RUNS=$RUNS WINDOW_LABELS=$WINDOW_LABELS"
