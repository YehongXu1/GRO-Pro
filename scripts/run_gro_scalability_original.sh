#!/usr/bin/env bash
set -euo pipefail

QUERY_DIR="${QUERY_DIR:-data/BJ_Real_query_sets_scalability_original}" \
LABEL="${LABEL:-original}" \
"$(dirname "$0")/run_gro_scalability_one_dir.sh"
