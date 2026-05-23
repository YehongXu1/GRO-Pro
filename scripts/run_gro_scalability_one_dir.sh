#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

if [[ ! ${OMP_NUM_THREADS+x} || -z "$OMP_NUM_THREADS" ]]; then OMP_NUM_THREADS=24; fi
export OMP_NUM_THREADS

if [[ ! ${CONFIG+x} || -z "$CONFIG" ]]; then CONFIG=config/config_bj_capacity2_cap10e8_iter5.yaml; fi
if [[ ! ${QUERY_DIR+x} || -z "$QUERY_DIR" ]]; then
  echo "Set QUERY_DIR to a scalability query directory" >&2
  exit 2
fi
if [[ ! ${LABEL+x} || -z "$LABEL" ]]; then
  echo "Set LABEL for output filenames, e.g. original or window6h" >&2
  exit 2
fi
if [[ ! ${RESULTS_DIR+x} || -z "$RESULTS_DIR" ]]; then RESULTS_DIR=python/results/scalability; fi
if [[ ! ${REPS+x} || -z "$REPS" ]]; then REPS=1,2,3,5,7,10; fi
if [[ ! ${TDG_GAMMAS+x} || -z "$TDG_GAMMAS" ]]; then TDG_GAMMAS=50; fi
if [[ ! ${IMPACT_WEIGHTS+x} || -z "$IMPACT_WEIGHTS" ]]; then IMPACT_WEIGHTS=20; fi
if [[ ! ${RANDOM_SEED+x} || -z "$RANDOM_SEED" ]]; then RANDOM_SEED=0; fi
if [[ ! ${SELECTION_METHODS+x} || -z "$SELECTION_METHODS" ]]; then SELECTION_METHODS=tdg_excess; fi
if [[ ! ${REROUTE_METHODS+x} || -z "$REROUTE_METHODS" ]]; then REROUTE_METHODS=tdg; fi
if [[ ! ${FIXED_FRACTIONS+x} || -z "$FIXED_FRACTIONS" ]]; then FIXED_FRACTIONS=10; fi
if [[ ! ${SUFFIX+x} || -z "$SUFFIX" ]]; then SUFFIX=capacity2_cap10e8; fi
if [[ ! ${CANDIDATE_FILTER+x} || -z "$CANDIDATE_FILTER" ]]; then CANDIDATE_FILTER=all; fi

if [[ "$CANDIDATE_FILTER" == "all" ]]; then
  CANDIDATE_TAG=full
else
  CANDIDATE_TAG=candidate_${CANDIDATE_FILTER}
fi

TMPDIR="$RESULTS_DIR/tmp_gro_scalability_${LABEL}_tdg_excess_${CANDIDATE_TAG}_${SUFFIX}"
OUT="$RESULTS_DIR/gro_scalability_${LABEL}_tdg_excess_${CANDIDATE_TAG}_${SUFFIX}.csv"

mkdir -p "$TMPDIR" "$RESULTS_DIR"

IFS=',' read -r -a REP_VALUES <<< "$REPS"

first=1
for rep in "${REP_VALUES[@]}"; do
  rep="$(echo "$rep" | xargs)"
  if [[ -z "$rep" ]]; then
    continue
  fi

  REP_OUT="$TMPDIR/rep${rep}.csv"
  echo "[run] label=$LABEL rep=$rep candidate_filter=$CANDIDATE_FILTER query_dir=$QUERY_DIR output=$REP_OUT"
  ./gro_ablation_test "$CONFIG" \
    --query-dir "$QUERY_DIR" \
    --rep "$rep" \
    --output "$REP_OUT" \
    --selection-methods "$SELECTION_METHODS" \
    --reroute-methods "$REROUTE_METHODS" \
    --fixed-fractions "$FIXED_FRACTIONS" \
    --tdg-gammas "$TDG_GAMMAS" \
    --impact-weights "$IMPACT_WEIGHTS" \
    --candidate-filter "$CANDIDATE_FILTER" \
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
