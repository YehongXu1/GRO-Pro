#!/usr/bin/env bash
set -euo pipefail

METHOD_LIST="${METHOD_LIST:-svp,gor,sor,fahl}"
REP="${REP:-all}"
REP_TAG="${REP_TAG:-${REP}}"
OMP_NUM_THREADS="${OMP_NUM_THREADS:-16}"
SOR_MAX_LABELS="${SOR_MAX_LABELS:-20000}"

IFS=',' read -r -a methods <<< "$METHOD_LIST"

mkdir -p logs

for method in "${methods[@]}"; do
  method="${method//[[:space:]]/}"
  if [[ -z "$method" ]]; then
    continue
  fi

  log="logs/paper_baseline_${method}_mh_real_window6h_${REP_TAG}.log"
  out="python/results/experiments/exp5_overall_effectiveness/paper_baseline_${method}_mh_real_window6h_${REP_TAG}.csv"

  echo "Launching $method -> $out"
  METHODS="$method" \
  METHOD_TAG="$method" \
  REP="$REP" \
  REP_TAG="$REP_TAG" \
  OMP_NUM_THREADS="$OMP_NUM_THREADS" \
  SOR_MAX_LABELS="$SOR_MAX_LABELS" \
  LOG="$log" \
  OUT="$out" \
  nohup bash scripts/run_mh_real_window6h_paper_baselines.sh \
    > /dev/null 2>&1 < /dev/null &
done
