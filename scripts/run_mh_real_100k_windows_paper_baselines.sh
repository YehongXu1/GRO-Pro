#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-32}"

CONFIG="${CONFIG:-config/config.yaml}"
WINDOW_LABELS="${WINDOW_LABELS:-window6h,window3h,window2h,window1h,window30min}"
METHODS="${METHODS:-svp,gor,sor,fahl}"
RESULTS_DIR="${RESULTS_DIR:-python/results/experiments/exp5_overall_effectiveness}"
LOG_DIR="${LOG_DIR:-logs}"
DRY_RUN="${DRY_RUN:-0}"
SKIP_BUILD="${SKIP_BUILD:-0}"
MAX_FILES="${MAX_FILES:-0}"
MAX_QUERIES="${MAX_QUERIES:-0}"
SOR_MAX_LABELS="${SOR_MAX_LABELS:-20000}"

mkdir -p "$RESULTS_DIR" "$LOG_DIR"

IFS=',' read -r -a method_values <<< "$METHODS"
IFS=',' read -r -a window_values <<< "$WINDOW_LABELS"

print_cmd() {
  printf ' %q' "$@"
  printf '\n'
}

if [[ "$SKIP_BUILD" != "1" ]]; then
  echo "[build] make paper_baseline_test"
  if [[ "$DRY_RUN" == "0" ]]; then
    make paper_baseline_test
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

  for method in "${method_values[@]}"; do
    method="${method//[[:space:]]/}"
    [[ -z "$method" ]] && continue

    out="$RESULTS_DIR/paper_baseline_${method}_mh_real_100k_${label}.csv"
    child_log="$LOG_DIR/paper_baseline_${method}_mh_real_100k_${label}.log"
    cmd=(
      env
      CONFIG="$CONFIG"
      QUERY_DIR="$query_dir"
      METHODS="$method"
      METHOD_TAG="$method"
      REP=10
      REP_TAG="100k_${label}"
      OUT="$out"
      LOG="$child_log"
      MAX_FILES="$MAX_FILES"
      MAX_QUERIES="$MAX_QUERIES"
      SOR_MAX_LABELS="$SOR_MAX_LABELS"
      OMP_NUM_THREADS="$OMP_NUM_THREADS"
      DRY_RUN="$DRY_RUN"
      bash scripts/run_paper_baselines_query_dir.sh
    )

    printf '[run] %s %s:' "$method" "$label"
    print_cmd "${cmd[@]}"

    if [[ "$DRY_RUN" == "0" ]]; then
      "${cmd[@]}"
      echo "[done] $method $label output=$out log=$child_log"
    fi
  done
done

echo "[all_done] METHODS=$METHODS WINDOW_LABELS=$WINDOW_LABELS"
