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
```

## Main Iterative Ablation

Preferred output layout: one CSV per method, all MH synthetic query sets inside
each file.

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
make run-ablation-tdg-anchor-full
make run-ablation-tdg-excess-full
```

To run method jobs in parallel on a server:

```bash
nohup make run-ablation-baseline-random-normal > gro_ablation_baseline_random_normal.log 2>&1 &
nohup make run-ablation-baseline-delayed-normal > gro_ablation_baseline_delayed_normal.log 2>&1 &
nohup make run-ablation-baseline-random-tdg-reroute > gro_ablation_baseline_random_tdg_reroute.log 2>&1 &
nohup make run-ablation-baseline-delayed-tdg-reroute > gro_ablation_baseline_delayed_tdg_reroute.log 2>&1 &
nohup make run-ablation-tdg-anchor-normal > gro_ablation_tdg_anchor_normal.log 2>&1 &
nohup make run-ablation-tdg-excess-normal > gro_ablation_tdg_excess_normal.log 2>&1 &
nohup make run-ablation-tdg-anchor-full > gro_ablation_tdg_anchor_full.log 2>&1 &
nohup make run-ablation-tdg-excess-full > gro_ablation_tdg_excess_full.log 2>&1 &
```

Expected output files:

```text
python/results/gro_ablation_baseline_random_normal.csv
python/results/gro_ablation_baseline_delayed_normal.csv
python/results/gro_ablation_baseline_random_tdg_reroute.csv
python/results/gro_ablation_baseline_delayed_tdg_reroute.csv
python/results/gro_ablation_tdg_anchor_normal.csv
python/results/gro_ablation_tdg_excess_normal.csv
python/results/gro_ablation_tdg_anchor_full.csv
python/results/gro_ablation_tdg_excess_full.csv
python/results/gro_ablation.csv
```

## Diagnostics

Selection-only diagnostic. This uses the older TDG selection debug runner, not
the new `tdg_excess` selection.

```bash
nohup ./gro_selection_debug_test config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/gro_selection_debug_removal_modes.csv \
  --gamma-values 25,50,75 \
  --removal-modes all_nodes,congestion_important,anchor_important \
  --random-seed 0 \
  > gro_selection_debug_removal_modes.log 2>&1 &
```

Simple selection-only baselines:

```bash
nohup ./gro_fixed_random_selection_test config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/gro_simple_selection_baselines_10_30.csv \
  --random-fractions 10,30 \
  --methods random,most_delayed \
  --random-seed 0 \
  > gro_simple_selection_baselines_10_30.log 2>&1 &
```

Reroute-only diagnostic:

```bash
nohup ./gro_reroute_debug_test config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/gro_reroute_debug.csv \
  --impact-weights 0,5,15,30,50,100 \
  --random-seed 0 \
  > gro_reroute_debug.log 2>&1 &
```

No-cap stability checks:

```bash
./mh_synthetic_experiment config/config_no_cap.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/random30_normal_no_cap_all_time64.csv \
  --algorithms baseline

./mh_synthetic_experiment config/config_no_cap_beta2.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/random30_normal_no_cap_beta2_all_time64.csv \
  --algorithms baseline
```

## Python Analysis

Use the existing plot environment directly:

```bash
/Users/xyh/opt/anaconda3/envs/plot/bin/python \
  python/compare_selection_with_simple_baselines.py \
  --tdg-selection python/results/gro_selection_debug_removal_modes.csv \
  --simple-baselines python/results/gro_simple_selection_baselines_10_30.csv \
  --output-dir python/results
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
