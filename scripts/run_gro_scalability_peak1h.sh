#!/usr/bin/env bash
set -euo pipefail

QUERY_DIR="${QUERY_DIR:-data/BJ_Real_query_sets_scalability_inner_progressive_peak1h}" \
LABEL="${LABEL:-peak1h}" \
"$(dirname "$0")/run_gro_scalability_one_dir.sh"
