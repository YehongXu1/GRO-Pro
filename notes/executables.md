# Executables

Build all executables:

```bash
make
```

Run all MH synthetic experiments:

```bash
./mh_synthetic_experiment config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/mh_synthetic_all_cpp.csv \
  --algorithms tdg,baseline,tdg_selection_baseline,tdg_reroute_baseline
```

The C++ runner writes all plotted series into the main CSV. Prefer this path
for new experiments so figures and data files have a one-to-one relationship.

Plot the existing MH synthetic C++ result CSV with total travel time on the
y-axis:

```bash
python3 python/display_mh_synthetic_total.py \
  --input python/results/mh_synthetic_all_cpp.csv \
  --output python/results/mh_synthetic_all_cpp_mean_total.png \
  --summary-output python/results/mh_synthetic_all_cpp_mean_total.csv
```

Run a small MH synthetic smoke test:

```bash
./mh_synthetic_experiment config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/mh_synthetic_smoke_cpp.csv \
  --algorithms tdg,baseline,tdg_selection_baseline,tdg_reroute_baseline \
  --max-files 1
```

Run one MH synthetic seed across all hop/rep settings:

```bash
./mh_synthetic_experiment config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/mh_synthetic_seed1_cpp.csv \
  --algorithms tdg,baseline,tdg_selection_baseline,tdg_reroute_baseline \
  --seed 1 \
  --max-iterations 10
```

Run one MH synthetic panel:

```bash
./mh_synthetic_experiment config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/mh_synthetic_hop20_rep2_seed1_cpp.csv \
  --algorithms tdg,baseline,tdg_selection_baseline,tdg_reroute_baseline \
  --hop 20 \
  --rep 2 \
  --seed 1
```

Run baseline unit test:

```bash
./gro_baseline_test config/test_config.yaml
```

Run the no-candidate-filter selection diagnostic:

```bash
./gro_selection_debug_test config/config.yaml \
  --query-file data/MH_Synthetic_query_sets/Hop10Rep1-0.txt \
  --output python/results/gro_selection_debug_iterations.csv \
  --gamma-values 0,25,50,75,100 \
  --removal-modes all_nodes,congestion_important,anchor_important \
  --random-seed 0
```

Run the same diagnostic on the full MH synthetic query directory:

```bash
nohup ./gro_selection_debug_test config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/gro_selection_debug_removal_modes.csv \
  --gamma-values 25,50,75 \
  --removal-modes all_nodes,congestion_important,anchor_important \
  --random-seed 0 \
  > gro_selection_debug_removal_modes.log 2>&1 &
```

Run fixed-size simple selection baselines, such as random and most-delayed
queries at 10% and 30%:

```bash
nohup ./gro_fixed_random_selection_test config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/gro_simple_selection_baselines_10_30.csv \
  --random-fractions 10,30 \
  --methods random,most_delayed \
  --random-seed 0 \
  > gro_simple_selection_baselines_10_30.log 2>&1 &
```

Run the reroute diagnostic with random selected queries:

```bash
./gro_reroute_debug_test config/config.yaml \
  --query-file data/MH_Synthetic_query_sets/Hop10Rep1-0.txt \
  --output python/results/gro_reroute_debug_smoke.csv \
  --impact-weights 0,5,15,30,50,100 \
  --random-seed 0
```

Run the reroute diagnostic on the full MH synthetic query directory:

```bash
nohup ./gro_reroute_debug_test config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/gro_reroute_debug.csv \
  --impact-weights 0,5,15,30,50,100 \
  --random-seed 0 \
  > gro_reroute_debug.log 2>&1 &
```

Run the iterative end-to-end GRO component ablation. Each method combination
runs the full `max_iterations` loop from `config/config.yaml`.

```bash
./gro_ablation_test config/config.yaml \
  --query-file data/MH_Synthetic_query_sets/Hop10Rep1-0.txt \
  --output python/results/gro_ablation_smoke.csv \
  --selection-methods random,most_delayed,tdg_anchor,tdg_excess \
  --reroute-methods normal,tdg \
  --fixed-fractions 10,30 \
  --tdg-gammas 50 \
  --impact-weights 30 \
  --hop 10 \
  --rep 1 \
  --random-seed 0
```

Run the iterative end-to-end GRO component ablation split by MH synthetic
configuration. Each job writes
`20 datasets x 12 method combinations x 5 iterations = 1200` rows with the
default `max_iterations=5`.

```bash
for hop in 10 20 40; do
  for rep in 1 2 4; do
    nohup ./gro_ablation_test config/config.yaml \
      --query-dir data/MH_Synthetic_query_sets \
      --hop "$hop" \
      --rep "$rep" \
      --output "python/results/gro_ablation_Hop${hop}Rep${rep}.csv" \
      --selection-methods random,most_delayed,tdg_anchor,tdg_excess \
      --reroute-methods normal,tdg \
      --fixed-fractions 10,30 \
      --tdg-gammas 50 \
      --impact-weights 30 \
      --random-seed 0 \
      > "gro_ablation_Hop${hop}Rep${rep}.log" 2>&1 &
  done
done
```

Merge the nine ablation output files:

```bash
head -1 python/results/gro_ablation_Hop10Rep1.csv > python/results/gro_ablation.csv
for hop in 10 20 40; do
  for rep in 1 2 4; do
    tail -n +2 "python/results/gro_ablation_Hop${hop}Rep${rep}.csv" \
      >> python/results/gro_ablation.csv
  done
done
```

Run GRO unit test:

```bash
./gro_test config/test_config.yaml
```

Run other baseline unit tests:

```bash
./svp_test config/test_config.yaml
./gor_test config/test_config.yaml
./sor_test config/test_config.yaml
./fahl_test config/test_config.yaml
```
