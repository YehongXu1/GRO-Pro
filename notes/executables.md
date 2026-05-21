# Executables

This file records the commands we currently use. Prefer the `make` targets for
repeatable experiments; use direct executable commands only for diagnostics or
one-off runs.

## Build

Build everything:

```bash
make
```

Build only the iterative ablation runner:

```bash
make gro_ablation_test
```

Run the small test suite:

```bash
make test
```

Useful overrides:

```bash
make CXX=g++-14
make ABLATION_CONFIG=config/config.yaml QUERY_DIR=data/MH_Synthetic_query_sets
make ABLATION_CONFIG=config/config_bj.yaml QUERY_DIR=data/BJ_Synthetic_query_sets RESULTS_DIR=python/results/bj
make RESULT_SUFFIX=capacity2_cap10e8
```

## Main Iterative Ablation

Preferred output layout: one CSV per method.

```text
MH results: python/results/mh/
BJ results: python/results/bj/
logs:       logs/
```

The fixed-fraction baselines use:

```text
FIXED_FRACTIONS=10,30
```

This means each fixed-selection method writes both 10% and 30% selection
fraction rows into the same method CSV.

Run all method-split ablations sequentially:

```bash
make run-ablation-methods
```

On a server:

```bash
nohup make run-ablation-methods > gro_ablation_methods.log 2>&1 &
```

Check result file row counts:

```bash
make check-ablation-methods
```

Merge the method files into one CSV:

```bash
make merge-ablation-methods
```

The method-split targets are:

```bash
make run-ablation-baseline-random-normal
make run-ablation-baseline-delayed-normal
make run-ablation-baseline-random-tdg-reroute
make run-ablation-baseline-delayed-tdg-reroute
make run-ablation-tdg-anchor-normal
make run-ablation-tdg-excess-normal
make run-ablation-tdg-bpr-relief-normal
make run-ablation-tdg-anchor-full
make run-ablation-tdg-excess-full
make run-ablation-tdg-bpr-relief-full
```

To run method jobs in parallel on a server:

```bash
nohup make run-ablation-baseline-random-normal > gro_ablation_baseline_random_normal.log 2>&1 &
nohup make run-ablation-baseline-delayed-normal > gro_ablation_baseline_delayed_normal.log 2>&1 &
nohup make run-ablation-baseline-random-tdg-reroute > gro_ablation_baseline_random_tdg_reroute.log 2>&1 &
nohup make run-ablation-baseline-delayed-tdg-reroute > gro_ablation_baseline_delayed_tdg_reroute.log 2>&1 &
nohup make run-ablation-tdg-anchor-normal > gro_ablation_tdg_anchor_normal.log 2>&1 &
nohup make run-ablation-tdg-excess-normal > gro_ablation_tdg_excess_normal.log 2>&1 &
nohup make run-ablation-tdg-bpr-relief-normal > gro_ablation_tdg_bpr_relief_normal.log 2>&1 &
nohup make run-ablation-tdg-anchor-full > gro_ablation_tdg_anchor_full.log 2>&1 &
nohup make run-ablation-tdg-excess-full > gro_ablation_tdg_excess_full.log 2>&1 &
nohup make run-ablation-tdg-bpr-relief-full > gro_ablation_tdg_bpr_relief_full.log 2>&1 &
```

Expected output files:

```text
python/results/mh/gro_ablation_baseline_random_normal.csv
python/results/mh/gro_ablation_baseline_delayed_normal.csv
python/results/mh/gro_ablation_baseline_random_tdg_reroute.csv
python/results/mh/gro_ablation_baseline_delayed_tdg_reroute.csv
python/results/mh/gro_ablation_tdg_anchor_normal.csv
python/results/mh/gro_ablation_tdg_excess_normal.csv
python/results/mh/gro_ablation_tdg_bpr_relief_normal.csv
python/results/mh/gro_ablation_tdg_anchor_full.csv
python/results/mh/gro_ablation_tdg_excess_full.csv
python/results/mh/gro_ablation_tdg_bpr_relief_full.csv
python/results/mh/gro_ablation.csv
```

## Beijing Full Iterative Ablation

Build once:

```bash
make
```

BJ uses:

```text
default config: config/config_bj.yaml
query dir:      data/BJ_Synthetic_query_sets
results dir:    python/results/bj
iterations:     10
fractions:      10,30
```

For the current BJ ablation runs, prefer explicit capacity/cap configs and add
the same value as `RESULT_SUFFIX` so the CSV filename records the setting:

```text
config/config_bj_capacity2_cap10e8.yaml -> RESULT_SUFFIX=capacity2_cap10e8
config/config_bj_capacity5_cap10e8.yaml -> RESULT_SUFFIX=capacity5_cap10e8
```

Run all BJ method-split jobs sequentially:

```bash
nohup make run-bj-ablation-methods \
  BJ_CONFIG=config/config_bj_capacity2_cap10e8.yaml \
  FIXED_FRACTIONS=10,30 \
  TDG_GAMMAS=50 \
  IMPACT_WEIGHTS=30 \
  RANDOM_SEED=0 \
  RESULT_SUFFIX=capacity2_cap10e8 \
  > logs/bj_ablation_methods_capacity2_cap10e8.log 2>&1 &
```

If one full run is too slow, run one method at a time. Example:

```bash
nohup make run-ablation-tdg-excess-normal \
  ABLATION_CONFIG=config/config_bj_capacity2_cap10e8.yaml \
  QUERY_DIR=data/BJ_Synthetic_query_sets \
  RESULTS_DIR=python/results/bj \
  FIXED_FRACTIONS=10,30 \
  TDG_GAMMAS=25,50 \
  IMPACT_WEIGHTS=30 \
  RANDOM_SEED=0 \
  RESULT_SUFFIX=capacity2_cap10e8 \
  > logs/bj_ablation_tdg_excess_normal_capacity2_cap10e8.log 2>&1 &
```

Check BJ outputs:

```bash
make check-bj-ablation-methods RESULT_SUFFIX=capacity2_cap10e8
```

Merge BJ outputs:

```bash
make merge-bj-ablation-methods RESULT_SUFFIX=capacity2_cap10e8
```

Expected BJ output files:

```text
python/results/bj/gro_ablation_baseline_random_normal_capacity2_cap10e8.csv
python/results/bj/gro_ablation_baseline_delayed_normal_capacity2_cap10e8.csv
python/results/bj/gro_ablation_baseline_random_tdg_reroute_capacity2_cap10e8.csv
python/results/bj/gro_ablation_baseline_delayed_tdg_reroute_capacity2_cap10e8.csv
python/results/bj/gro_ablation_tdg_anchor_normal_capacity2_cap10e8.csv
python/results/bj/gro_ablation_tdg_excess_normal_capacity2_cap10e8.csv
python/results/bj/gro_ablation_tdg_bpr_relief_normal_capacity2_cap10e8.csv
python/results/bj/gro_ablation_tdg_anchor_full_capacity2_cap10e8.csv
python/results/bj/gro_ablation_tdg_excess_full_capacity2_cap10e8.csv
python/results/bj/gro_ablation_tdg_bpr_relief_full_capacity2_cap10e8.csv
python/results/bj/gro_ablation_capacity2_cap10e8.csv
```

To run BJ method jobs in parallel on the server:

```bash
nohup make run-ablation-baseline-random-normal ABLATION_CONFIG=config/config_bj_capacity2_cap10e8.yaml QUERY_DIR=data/BJ_Synthetic_query_sets RESULTS_DIR=python/results/bj FIXED_FRACTIONS=10,30 TDG_GAMMAS=50 IMPACT_WEIGHTS=30 RANDOM_SEED=0 RESULT_SUFFIX=capacity2_cap10e8 > logs/bj_ablation_baseline_random_normal_capacity2_cap10e8.log 2>&1 &

nohup make run-ablation-baseline-delayed-normal ABLATION_CONFIG=config/config_bj_capacity2_cap10e8.yaml QUERY_DIR=data/BJ_Synthetic_query_sets RESULTS_DIR=python/results/bj FIXED_FRACTIONS=10,30 TDG_GAMMAS=50 IMPACT_WEIGHTS=30 RANDOM_SEED=0 RESULT_SUFFIX=capacity2_cap10e8 > logs/bj_ablation_baseline_delayed_normal_capacity2_cap10e8.log 2>&1 &

nohup make run-ablation-baseline-random-tdg-reroute ABLATION_CONFIG=config/config_bj_capacity2_cap10e8.yaml QUERY_DIR=data/BJ_Synthetic_query_sets RESULTS_DIR=python/results/bj FIXED_FRACTIONS=10,30 TDG_GAMMAS=50 IMPACT_WEIGHTS=30 RANDOM_SEED=0 RESULT_SUFFIX=capacity2_cap10e8 > logs/bj_ablation_baseline_random_tdg_reroute_capacity2_cap10e8.log 2>&1 &

nohup make run-ablation-baseline-delayed-tdg-reroute ABLATION_CONFIG=config/config_bj_capacity2_cap10e8.yaml QUERY_DIR=data/BJ_Synthetic_query_sets RESULTS_DIR=python/results/bj FIXED_FRACTIONS=10,30 TDG_GAMMAS=50 IMPACT_WEIGHTS=30 RANDOM_SEED=0 RESULT_SUFFIX=capacity2_cap10e8 > logs/bj_ablation_baseline_delayed_tdg_reroute_capacity2_cap10e8.log 2>&1 &

nohup make run-ablation-tdg-anchor-normal ABLATION_CONFIG=config/config_bj_capacity2_cap10e8.yaml QUERY_DIR=data/BJ_Synthetic_query_sets RESULTS_DIR=python/results/bj FIXED_FRACTIONS=10,30 TDG_GAMMAS=50 IMPACT_WEIGHTS=30 RANDOM_SEED=0 RESULT_SUFFIX=capacity2_cap10e8 > logs/bj_ablation_tdg_anchor_normal_capacity2_cap10e8.log 2>&1 &

nohup make run-ablation-tdg-excess-normal ABLATION_CONFIG=config/config_bj_capacity2_cap10e8.yaml QUERY_DIR=data/BJ_Synthetic_query_sets RESULTS_DIR=python/results/bj FIXED_FRACTIONS=10,30 TDG_GAMMAS=50 IMPACT_WEIGHTS=30 RANDOM_SEED=0 RESULT_SUFFIX=capacity2_cap10e8 > logs/bj_ablation_tdg_excess_normal_capacity2_cap10e8.log 2>&1 &

nohup make run-ablation-tdg-bpr-relief-normal ABLATION_CONFIG=config/config_bj_capacity2_cap10e8.yaml QUERY_DIR=data/BJ_Synthetic_query_sets RESULTS_DIR=python/results/bj FIXED_FRACTIONS=10,30 TDG_GAMMAS=50 IMPACT_WEIGHTS=30 RANDOM_SEED=0 RESULT_SUFFIX=capacity2_cap10e8 > logs/bj_ablation_tdg_bpr_relief_normal_capacity2_cap10e8.log 2>&1 &

nohup make run-ablation-tdg-anchor-full ABLATION_CONFIG=config/config_bj_capacity2_cap10e8.yaml QUERY_DIR=data/BJ_Synthetic_query_sets RESULTS_DIR=python/results/bj FIXED_FRACTIONS=10,30 TDG_GAMMAS=50 IMPACT_WEIGHTS=30 RANDOM_SEED=0 RESULT_SUFFIX=capacity2_cap10e8 > logs/bj_ablation_tdg_anchor_full_capacity2_cap10e8.log 2>&1 &

nohup make run-ablation-tdg-excess-full ABLATION_CONFIG=config/config_bj_capacity2_cap10e8.yaml QUERY_DIR=data/BJ_Synthetic_query_sets RESULTS_DIR=python/results/bj FIXED_FRACTIONS=10,30 TDG_GAMMAS=50 IMPACT_WEIGHTS=30 RANDOM_SEED=0 RESULT_SUFFIX=capacity2_cap10e8 > logs/bj_ablation_tdg_excess_full_capacity2_cap10e8.log 2>&1 &

nohup make run-ablation-tdg-bpr-relief-full ABLATION_CONFIG=config/config_bj_capacity2_cap10e8.yaml QUERY_DIR=data/BJ_Synthetic_query_sets RESULTS_DIR=python/results/bj FIXED_FRACTIONS=10,30 TDG_GAMMAS=50 IMPACT_WEIGHTS=30 RANDOM_SEED=0 RESULT_SUFFIX=capacity2_cap10e8 > logs/bj_ablation_tdg_bpr_relief_full_capacity2_cap10e8.log 2>&1 &
```

BJ TDG-reroute impact-weight sweep. This keeps our `tdg_excess` selection and
TDG-impact reroute, then varies the reroute impact penalty. `impact_weight=0`
is a useful control because it keeps the TDG reroute path and batching machinery
but removes the TDG-impact penalty from the path score.

```bash
make gro_ablation_test

mkdir -p python/results/bj/sweeps/tdg_reroute_impact logs

nohup ./gro_ablation_test config/config_bj_capacity2_cap10e8.yaml \
  --query-dir data/BJ_Synthetic_query_sets \
  --output python/results/bj/sweeps/tdg_reroute_impact/gro_ablation_tdg_excess_full_impact_sweep_capacity2_cap10e8.csv \
  --selection-methods tdg_excess \
  --reroute-methods tdg \
  --tdg-gammas 25,50,90 \
  --impact-weights 0,5,10,15,20,30,50,75,100 \
  --random-seed 0 \
  > logs/bj_ablation_tdg_excess_full_impact_sweep_capacity2_cap10e8.log 2>&1 &

tail -f logs/bj_ablation_tdg_excess_full_impact_sweep_capacity2_cap10e8.log
wc -l python/results/bj/sweeps/tdg_reroute_impact/gro_ablation_tdg_excess_full_impact_sweep_capacity2_cap10e8.csv
```

Expected complete output size:

```text
72901 lines = header + 270 query sets * 10 iterations * 3 gammas * 9 impact weights
```

Shortest-path congestion diagnostic. This computes free-flow shortest paths,
then compares their no-flow total travel time against the same routes evaluated
with the project congestion evaluator.

BJ real query sets:

```bash
make shortest_path_congestion_diagnostic

mkdir -p python/results/bj_real logs

nohup ./shortest_path_congestion_diagnostic config/config_bj_capacity2_cap10e8.yaml \
  --query-dir data/BJ_Real_query_sets \
  --output python/results/bj_real/shortest_path_congestion_bj_real_capacity2_cap10e8.csv \
  > logs/shortest_path_congestion_bj_real_capacity2_cap10e8.log 2>&1 &

tail -f logs/shortest_path_congestion_bj_real_capacity2_cap10e8.log
```

BJ synthetic query sets, for a same-graph density reference:

```bash
make shortest_path_congestion_diagnostic

mkdir -p python/results/bj/analysis/congestion logs

nohup ./shortest_path_congestion_diagnostic config/config_bj_capacity2_cap10e8.yaml \
  --query-dir data/BJ_Synthetic_query_sets \
  --output python/results/bj/analysis/congestion/shortest_path_congestion_bj_synthetic_capacity2_cap10e8.csv \
  > logs/shortest_path_congestion_bj_synthetic_capacity2_cap10e8.log 2>&1 &

tail -f logs/shortest_path_congestion_bj_synthetic_capacity2_cap10e8.log
```

List-only summary for the synthetic diagnostic:

```bash
/Users/xyh/opt/anaconda3/envs/plot/bin/python \
  python/summarize_shortest_path_congestion.py \
  --input python/results/bj/analysis/congestion/shortest_path_congestion_bj_synthetic_capacity2_cap10e8.csv \
  --summary-output python/results/bj/analysis/congestion/shortest_path_congestion_bj_synthetic_capacity2_cap10e8_summary.csv
```

If you only want to vary random-selection fraction 10% vs 30% with normal
TD-Dijkstra first, run just:

```bash
nohup make run-ablation-baseline-random-normal ABLATION_CONFIG=config/config_bj.yaml QUERY_DIR=data/BJ_Synthetic_query_sets RESULTS_DIR=python/results/bj FIXED_FRACTIONS=10,30 RANDOM_SEED=0 > logs/bj_ablation_baseline_random_normal.log 2>&1 &
```

## Diagnostics

Selection-only diagnostic. This uses the older TDG selection debug runner, not
the new `tdg_excess` selection.

```bash
nohup ./gro_selection_debug_test config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/mh/gro_selection_debug_removal_modes.csv \
  --gamma-values 25,50,75 \
  --removal-modes all_nodes,congestion_important,anchor_important \
  --random-seed 0 \
  > gro_selection_debug_removal_modes.log 2>&1 &
```

Simple selection-only baselines:

```bash
nohup ./gro_fixed_random_selection_test config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/mh/gro_simple_selection_baselines_10_30.csv \
  --random-fractions 10,30 \
  --methods random,most_delayed \
  --random-seed 0 \
  > gro_simple_selection_baselines_10_30.log 2>&1 &
```

Reroute-only diagnostic:

```bash
nohup ./gro_reroute_debug_test config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/mh/gro_reroute_debug.csv \
  --impact-weights 0,5,15,30,50,100 \
  --random-seed 0 \
  > gro_reroute_debug.log 2>&1 &
```

ContGRO-style slot TDG diagnostic:

```bash
make gro_slot_legacy_ablation_test

nohup ./gro_slot_legacy_ablation_test config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/mh/gro_slot_legacy_all.csv \
  --methods legacy_slot_normal,legacy_slot_tdg,random_normal \
  --slot-width 1 \
  --tau 90 \
  --gamma 50 \
  --lambda 80 \
  --random-fraction 10 \
  --random-seed 0 \
  > logs/gro_slot_legacy_all.log 2>&1 &
```

BJ version:

```bash
nohup ./gro_slot_legacy_ablation_test config/config_bj.yaml \
  --query-dir data/BJ_Synthetic_query_sets \
  --output python/results/bj/gro_slot_legacy_all.csv \
  --methods legacy_slot_normal,legacy_slot_tdg,random_normal \
  --slot-width 1 \
  --tau 90 \
  --gamma 50 \
  --lambda 80 \
  --random-fraction 10 \
  --random-seed 0 \
  > logs/bj_slot_legacy_all.log 2>&1 &
```

No-cap stability checks:

```bash
./mh_synthetic_experiment config/config_no_cap.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/mh/random30_normal_no_cap_all_time64.csv \
  --algorithms baseline

./mh_synthetic_experiment config/config_no_cap_beta2.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/mh/random30_normal_no_cap_beta2_all_time64.csv \
  --algorithms baseline
```

## Python Analysis

Use the existing plot environment directly:

```bash
/Users/xyh/opt/anaconda3/envs/plot/bin/python \
  python/compare_selection_with_simple_baselines.py \
  --tdg-selection python/results/mh/gro_selection_debug_removal_modes.csv \
  --simple-baselines python/results/mh/gro_simple_selection_baselines_10_30.csv \
  --output-dir python/results/mh
```

BJ best-param component ablation plot. This compares the baseline curves against
the oracle/best-parameter `tdg_excess` CSVs and uses the second y-axis for the
selected-query fraction of `TDG-guided + TDG-impact reroute`.

```bash
cd /Users/xyh/Desktop/GRO-Pro

/Users/xyh/opt/anaconda3/envs/plot/bin/python python/plot_gro_component_ablation.py \
  --random-normal python/results/bj/gro_ablation_baseline_random_normal_capacity2_cap10e8.csv \
  --delayed-normal python/results/bj/gro_ablation_baseline_delayed_normal_capacity2_cap10e8.csv \
  --delayed-tdg-reroute python/results/bj/gro_ablation_baseline_delayed_tdg_reroute_capacity2_cap10e8.csv \
  --tdg-excess-normal python/results/bj/oracle/gro_ablation_tdg_excess_normal_best_param_by_iter_capacity2_cap10e8.csv \
  --tdg-excess-full python/results/bj/oracle/gro_ablation_tdg_excess_full_best_param_by_iter_capacity2_cap10e8.csv \
  --show-selection-fraction \
  --selection-fraction-label TDG-guided \
  --selection-fraction-reroute "TDG-impact reroute" \
  --baseline-fraction 10 \
  --output python/results/bj/plots/bj_component_ablation_best_param_capacity2_cap10e8_with_selection_fraction.png
```

## Unit Tests

Run individual tests:

```bash
./gro_test config/test_config.yaml
./gro_baseline_test config/test_config.yaml
./svp_test config/test_config.yaml
./gor_test config/test_config.yaml
./sor_test config/test_config.yaml
./fahl_test config/test_config.yaml
```
