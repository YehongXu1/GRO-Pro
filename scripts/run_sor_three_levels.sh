#!/usr/bin/env bash
set -euo pipefail

OUT=python/results/bj_real/overall_effectiveness/paper_baseline_sor_100k_three_levels_capacity2_cap10e8.csv
TMPDIR=python/results/bj_real/overall_effectiveness/tmp_sor_100k_three_levels_capacity2_cap10e8

mkdir -p "$TMPDIR"

./paper_baseline_test config/config_bj_capacity2_cap10e8.yaml \
  --query-file data/BJ_Real_query_sets/BJRealRep10-2.txt \
  --output "$TMPDIR/lower_original_BJRealRep10-2.csv" \
  --methods sor \
  --sor-time-step 1800 \
  --sor-max-time-steps 48 \
  --sor-max-labels-per-query 3

./paper_baseline_test config/config_bj_capacity2_cap10e8.yaml \
  --query-file data/BJ_Real_query_sets_window6h/BJRealRep10-2.txt \
  --output "$TMPDIR/middle_window6h_BJRealRep10-2.csv" \
  --methods sor \
  --sor-time-step 1800 \
  --sor-max-time-steps 48 \
  --sor-max-labels-per-query 3

./paper_baseline_test config/config_bj_capacity2_cap10e8.yaml \
  --query-file data/BJ_Real_query_sets_window6h/BJRealRep10-4.txt \
  --output "$TMPDIR/extreme_window6h_BJRealRep10-4.csv" \
  --methods sor \
  --sor-time-step 1800 \
  --sor-max-time-steps 48 \
  --sor-max-labels-per-query 3

head -n 1 "$TMPDIR/lower_original_BJRealRep10-2.csv" > "$OUT"
tail -n +2 "$TMPDIR/lower_original_BJRealRep10-2.csv" >> "$OUT"
tail -n +2 "$TMPDIR/middle_window6h_BJRealRep10-2.csv" >> "$OUT"
tail -n +2 "$TMPDIR/extreme_window6h_BJRealRep10-4.csv" >> "$OUT"
wc -l "$OUT"
