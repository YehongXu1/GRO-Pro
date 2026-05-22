#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-24}"

FIXED_FRACTIONS="${FIXED_FRACTIONS:-10}"
TDG_GAMMAS_PLACEHOLDER="${TDG_GAMMAS_PLACEHOLDER:-50}"
IMPACT_WEIGHTS_PLACEHOLDER="${IMPACT_WEIGHTS_PLACEHOLDER:-30}"
RANDOM_SEED="${RANDOM_SEED:-0}"

OUT=python/results/bj_real/overall_effectiveness/gro_baseline_random_delayed_normal_100k_three_levels_capacity2_cap10e8.csv
TMPDIR=python/results/bj_real/overall_effectiveness/tmp_gro_baseline_random_delayed_normal_100k_three_levels_capacity2_cap10e8

mkdir -p "$TMPDIR"

./gro_ablation_test config/config_bj_capacity2_cap10e8.yaml \
  --query-file data/BJ_Real_query_sets/BJRealRep10-2.txt \
  --output "$TMPDIR/lower_original_BJRealRep10-2.csv" \
  --selection-methods random,most_delayed \
  --reroute-methods normal \
  --fixed-fractions "$FIXED_FRACTIONS" \
  --tdg-gammas "$TDG_GAMMAS_PLACEHOLDER" \
  --impact-weights "$IMPACT_WEIGHTS_PLACEHOLDER" \
  --random-seed "$RANDOM_SEED"

./gro_ablation_test config/config_bj_capacity2_cap10e8.yaml \
  --query-file data/BJ_Real_query_sets_window6h/BJRealRep10-2.txt \
  --output "$TMPDIR/middle_window6h_BJRealRep10-2.csv" \
  --selection-methods random,most_delayed \
  --reroute-methods normal \
  --fixed-fractions "$FIXED_FRACTIONS" \
  --tdg-gammas "$TDG_GAMMAS_PLACEHOLDER" \
  --impact-weights "$IMPACT_WEIGHTS_PLACEHOLDER" \
  --random-seed "$RANDOM_SEED"

./gro_ablation_test config/config_bj_capacity2_cap10e8.yaml \
  --query-file data/BJ_Real_query_sets_window6h/BJRealRep10-4.txt \
  --output "$TMPDIR/extreme_window6h_BJRealRep10-4.csv" \
  --selection-methods random,most_delayed \
  --reroute-methods normal \
  --fixed-fractions "$FIXED_FRACTIONS" \
  --tdg-gammas "$TDG_GAMMAS_PLACEHOLDER" \
  --impact-weights "$IMPACT_WEIGHTS_PLACEHOLDER" \
  --random-seed "$RANDOM_SEED"

head -n 1 "$TMPDIR/lower_original_BJRealRep10-2.csv" > "$OUT"
tail -n +2 "$TMPDIR/lower_original_BJRealRep10-2.csv" >> "$OUT"
tail -n +2 "$TMPDIR/middle_window6h_BJRealRep10-2.csv" >> "$OUT"
tail -n +2 "$TMPDIR/extreme_window6h_BJRealRep10-4.csv" >> "$OUT"
wc -l "$OUT"
