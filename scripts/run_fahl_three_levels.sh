#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-32}"

OUT=python/results/bj_real/overall_effectiveness/paper_baseline_fahl_100k_three_levels_capacity2_cap10e8.csv
TMPDIR=python/results/bj_real/overall_effectiveness/tmp_fahl_100k_three_levels_capacity2_cap10e8

mkdir -p "$TMPDIR"

./paper_baseline_test config/config_bj_capacity2_cap10e8.yaml \
  --query-file data/BJ_Real_query_sets/BJRealRep10-2.txt \
  --output "$TMPDIR/lower_original_BJRealRep10-2.csv" \
  --methods fahl \
  --fahl-alpha-percent 50 \
  --fahl-time-step 60 \
  --fahl-order-beta-percent 70

./paper_baseline_test config/config_bj_capacity2_cap10e8.yaml \
  --query-file data/BJ_Real_query_sets_window6h/BJRealRep10-2.txt \
  --output "$TMPDIR/middle_window6h_BJRealRep10-2.csv" \
  --methods fahl \
  --fahl-alpha-percent 50 \
  --fahl-time-step 60 \
  --fahl-order-beta-percent 70

./paper_baseline_test config/config_bj_capacity2_cap10e8.yaml \
  --query-file data/BJ_Real_query_sets_window6h/BJRealRep10-4.txt \
  --output "$TMPDIR/extreme_window6h_BJRealRep10-4.csv" \
  --methods fahl \
  --fahl-alpha-percent 50 \
  --fahl-time-step 60 \
  --fahl-order-beta-percent 70

head -n 1 "$TMPDIR/lower_original_BJRealRep10-2.csv" > "$OUT"
tail -n +2 "$TMPDIR/lower_original_BJRealRep10-2.csv" >> "$OUT"
tail -n +2 "$TMPDIR/middle_window6h_BJRealRep10-2.csv" >> "$OUT"
tail -n +2 "$TMPDIR/extreme_window6h_BJRealRep10-4.csv" >> "$OUT"
wc -l "$OUT"
