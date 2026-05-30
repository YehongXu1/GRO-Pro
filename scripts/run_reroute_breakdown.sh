#!/usr/bin/env bash
# Reroute timing-breakdown experiment: 10k / 50k / 100k x {fine, compressed}.
# One dataset per run, stderr captured per (method, size, dataset).
# Requires config/config_bj_capacity2_cap10e8_iter5_timing.yaml (timing on).
# Parse the logs with: python python/parse_reroute_breakdown.py
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

PEAK1H="${PEAK1H:-data/BJ_Real_query_sets_scalability_inner_progressive_peak1h}"
CFG="${CFG:-config/config_bj_capacity2_cap10e8_iter5_timing.yaml}"
OUT_DIR="${OUT_DIR:-python/results/experiments/exp3_compression_scalability/reroute_breakdown}"
LOG_DIR="${LOG_DIR:-logs/reroute_breakdown}"
OMP="${OMP_NUM_THREADS:-24}"
BIN="${BIN:-./gro_ablation_test}"

# Default tasks: size:dataset_basename (extension .txt assumed)
TASKS_DEFAULT=(
  "10k:BJRealRep1-0"
  "50k:BJRealRep5-0"
  "100k:BJRealRep10-0"
)
if [[ -n "${TASKS:-}" ]]; then
  IFS=' ' read -r -a TASK_LIST <<< "$TASKS"
else
  TASK_LIST=("${TASKS_DEFAULT[@]}")
fi

# Default modes: both fine and compressed; override via MODES="fine" etc.
MODES_DEFAULT=(fine compressed)
if [[ -n "${MODES:-}" ]]; then
  IFS=' ' read -r -a MODE_LIST <<< "$MODES"
else
  MODE_LIST=("${MODES_DEFAULT[@]}")
fi

[[ -f "$CFG" ]] || { echo "Timing config $CFG not found" >&2; exit 1; }
[[ -x "$BIN" ]] || { echo "Binary $BIN not built (run: make gro_ablation_test)" >&2; exit 1; }
mkdir -p "$OUT_DIR" "$LOG_DIR"

COMMON=(
  --selection-methods tdg_excess
  --reroute-methods   tdg
  --fixed-fractions   10
  --tdg-gammas        50
  --candidate-theta   80
  --impact-weights    15
  --conflict-threshold 5000
  --candidate-filter  all
  --random-seed       0
)

for entry in "${TASK_LIST[@]}"; do
  size="${entry%%:*}"
  file="${entry##*:}"
  qfile="$PEAK1H/${file}.txt"
  if [[ ! -f "$qfile" ]]; then
    echo "[skip] missing query file: $qfile" >&2
    continue
  fi
  for mode in "${MODE_LIST[@]}"; do
    tag="${mode}_${size}_${file}"
    out_csv="$OUT_DIR/breakdown_${tag}.csv"
    log_file="$LOG_DIR/${tag}.log"
    echo "[run] $tag (OMP=$OMP) -> $log_file"
    OMP_NUM_THREADS="$OMP" "$BIN" "$CFG" \
      --query-file "$qfile" \
      --output    "$out_csv" \
      --tdg-mode  "$mode" \
      "${COMMON[@]}" \
      > "$log_file" 2>&1
    echo "[done] $tag"
  done
done

echo
echo "Done."
echo "Logs:  $LOG_DIR/"
echo "CSVs:  $OUT_DIR/"
echo "Parse: python python/parse_reroute_breakdown.py"
