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
  --random-seed 0
```

This also writes `python/results/gro_selection_debug_iterations_runs.csv`.

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
