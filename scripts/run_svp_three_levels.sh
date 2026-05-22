#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${LOG:-}" ]]; then
  mkdir -p "$(dirname "$LOG")"
  exec > "$LOG" 2>&1
fi

export OMP_NUM_THREADS="${OMP_NUM_THREADS:-32}"

OUT=python/results/bj_real/overall_effectiveness/paper_baseline_svp_100k_three_levels_capacity2_cap10e8.csv
TMPDIR=python/results/bj_real/overall_effectiveness/tmp_svp_100k_three_levels_capacity2_cap10e8

mkdir -p "$TMPDIR"

./paper_baseline_test config/config_bj_capacity2_cap10e8.yaml \
  --query-file data/BJ_Real_query_sets/BJRealRep10-2.txt \
  --output "$TMPDIR/lower_original_BJRealRep10-2.csv" \
  --methods svp \
  --svp-k 3 \
  --svp-theta 80

./paper_baseline_test config/config_bj_capacity2_cap10e8.yaml \
  --query-file data/BJ_Real_query_sets_window6h/BJRealRep10-2.txt \
  --output "$TMPDIR/middle_window6h_BJRealRep10-2.csv" \
  --methods svp \
  --svp-k 3 \
  --svp-theta 80

./paper_baseline_test config/config_bj_capacity2_cap10e8.yaml \
  --query-file data/BJ_Real_query_sets_window6h/BJRealRep10-4.txt \
  --output "$TMPDIR/extreme_window6h_BJRealRep10-4.csv" \
  --methods svp \
  --svp-k 3 \
  --svp-theta 80

head -n 1 "$TMPDIR/lower_original_BJRealRep10-2.csv" > "$OUT"
tail -n +2 "$TMPDIR/lower_original_BJRealRep10-2.csv" >> "$OUT"
tail -n +2 "$TMPDIR/middle_window6h_BJRealRep10-2.csv" >> "$OUT"
tail -n +2 "$TMPDIR/extreme_window6h_BJRealRep10-4.csv" >> "$OUT"
wc -l "$OUT"
