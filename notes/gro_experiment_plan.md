# GRO Experiment Plan

This note records the current paper-level experiment design. The order is
intentional: first isolate the major components, then compare the complete
method against baselines.

## Main Claim

GRO builds a traffic dependency graph over massive route trajectories and uses
the dependency signal to guide iterative global rerouting. The experiments
should show three things:

1. TDG-based selection chooses a better set of queries to reroute.
2. TDG-aware rerouting can improve replacement routes once queries are selected.
3. TDG compression makes the method scalable without destroying route quality.

## Dataset Roles

| Dataset | Role |
| --- | --- |
| BJ Synthetic 3 x 3 x 30 | Main ablation dataset. It controls query length and density, so it is the cleanest setting for explaining why each component works. |
| Real-world taxi workloads | Main scalability and practical-effectiveness dataset. Use varying query counts, e.g. 10k, 30k, 50k, 100k. |
| MH Synthetic | Robustness or appendix only unless it becomes necessary for a specific comparison. |

## Method Names

| Name | Selection | Reroute | Role |
| --- | --- | --- | --- |
| `baseline_random_normal` | random fixed fraction | normal TD-Dijkstra | simple iterative baseline |
| `baseline_delayed_normal` | most-delayed fixed fraction | normal TD-Dijkstra | strongest simple iterative baseline |
| `baseline_delayed_tdg_reroute` | most-delayed fixed fraction | TDG impact reroute | reroute ablation only |
| `tdg_anchor_normal` | TDG anchor selection | normal TD-Dijkstra | selection ablation |
| `tdg_excess_normal` | TDG excess-relief selection | normal TD-Dijkstra | main selection variant |
| `tdg_excess_full` | TDG excess-relief selection | TDG impact reroute | complete GRO variant |
| `SVP` | one-shot diversified routing | one-shot | paper baseline |
| `GOR` | one-shot greedy congestion-aware routing | one-shot | paper baseline |
| `SOR` | one-shot spatiotemporal congestion mitigation | one-shot | paper baseline |
| `FAHL` | indexed flow-aware individual routing | one-shot query processing | paper baseline |

Only `baseline_random_normal`, `baseline_delayed_normal`, and GRO variants are
iterative methods. SVP, GOR, SOR, and FAHL should not be plotted as
over-iteration curves.

## Subsection 1: Selection Ablation

Purpose:

Show that TDG-based query selection is better than simple fixed-fraction
selection when rerouting is held fixed.

Dataset:

```text
BJ Synthetic 3 x 3 x 30
```

Methods:

```text
baseline_random_normal
baseline_delayed_normal
tdg_anchor_normal
tdg_excess_normal
```

Default plotting setting:

```text
baseline fraction = 10%
TDG gamma = 25
reroute method = normal_td_dijkstra
```

Additional settings such as baseline fraction 30% and gamma 50 can be reported
in a compact table or appendix if needed.

Metrics:

```text
TTT over iterations
final TTT
average query travel time
selected_count
selected_fraction
select_sec
tdg_prepare_sec
```

Expected interpretation:

If `tdg_excess_normal` beats `baseline_delayed_normal` under the same iteration
budget, the gain is attributable to selection because rerouting is fixed to
normal TD-Dijkstra.

## Subsection 2: Rerouting Ablation

Purpose:

Show whether TDG impact-aware rerouting improves route replacement quality once
the selected query set is fixed.

Dataset:

```text
BJ Synthetic 3 x 3 x 30
```

Methods:

```text
baseline_delayed_normal
baseline_delayed_tdg_reroute
tdg_excess_normal
tdg_excess_full
```

Controlled comparisons:

```text
baseline_delayed_normal vs baseline_delayed_tdg_reroute
tdg_excess_normal vs tdg_excess_full
```

Default setting:

```text
baseline fraction = 10%
TDG gamma = 25
impact_weight = 30
```

Metrics:

```text
TTT over iterations
final TTT
average query travel time
reroute_sec
batch_sec
batch_count
selected_count
method_total_sec
```

Expected interpretation:

`baseline_delayed_tdg_reroute` is not an overall-effectiveness baseline. It is
a diagnostic method showing what happens when only the reroute component is
changed while selection stays simple.

## Subsection 3: Compression And Scalability

Purpose:

Show that compression is the scalability component of GRO: it reduces TDG size
and runtime enough for large query workloads while preserving route quality.

Dataset:

```text
Real-world taxi workloads with varying query count:
10k, 30k, 50k, 100k
```

Methods:

```text
tdg_excess_normal without compression
tdg_excess_normal with compression
tdg_excess_full without compression
tdg_excess_full with compression
```

If the uncompressed method cannot finish at larger sizes, report timeout or
memory failure as part of the scalability result.

Metrics:

```text
TDG node count
TDG edge/timeline count
compression ratio
build/compress time
impact computation time
selection time
reroute time
method_total_sec
memory usage
final TTT
TTT difference from uncompressed, when uncompressed is available
```

Compression parameter experiment:

Use one representative real-world workload, e.g. 30k or 50k queries, and vary
`delta_compress`. Report TDG size, runtime, and final TTT. Do not make
compression a large synthetic-data experiment.

## Subsection 4: Parameter Sensitivity

Purpose:

Show that the default parameters are reasonable and that the method is not
fragile. This subsection is a robustness check, not a tuning contest. It should
use a small number of pre-declared values and then fix one default for the
overall-effectiveness section.

Default candidates:

```text
gamma = 50
impact_weight = 20
delta_compress = 1200 seconds
compressed real-workload conflict_threshold = 5000
```

These match the current proposed GRO runner settings. Older component-ablation
figures may still use `gamma = 25` or `impact_weight = 30`; treat those as
ablation settings unless the final paper default is explicitly changed.

Parameter groups:

```text
gamma: 25, 50, 90
impact_weight: 0, 10, 20, 30, 50
delta_compress: 600, 1200, 2400 seconds
```

Datasets and scope:

```text
gamma:
  BJ Synthetic 3 x 3 x 30
  tdg_excess_normal and tdg_excess_full

impact_weight:
  BJ Synthetic 3 x 3 x 30
  gamma fixed to the selected default
  tdg_excess_full only

delta_compress:
  one representative peak real-world workload
  compressed TDG only
  candidate_filter = score_top
  gamma and impact_weight fixed to the selected defaults
```

Primary metrics:

```text
final TTT
average query travel time
improvement over shortest-path initialization
selected_fraction
method_total_sec
tdg_prepare_sec
select_sec
reroute_sec
tdg_node_count and tdg_edge_timeline_count for delta_compress
```

Decision rule:

- Use all 270 BJ synthetic datasets for `gamma` and `impact_weight`; do not
  choose a default from selected-presentation seeds.
- Compare fixed parameter values, not oracle or best-param-by-iteration rows.
- Prefer a parameter if it is within the stable quality band of the best tested
  value, avoids catastrophic dense-workload failures, and does not noticeably
  inflate selected fraction or runtime.
- Report medians and per-configuration summaries because means are sensitive to
  a few extreme dense synthetic workloads.

Current result status:

- Gamma has usable BJ synthetic results under
  `python/results/experiments/exp4_parameter_sensitivity/bj_synthetic_capacity2_cap10e8/gamma/`.
  In the selection-only normal-reroute comparison, `gamma = 25` selects about
  `3.70%` of queries on average and wins `61.9%` of datasets; `gamma = 50`
  selects about `7.91%` and has similar median final TTT; `gamma = 90` selects
  about `20.63%`, is slower, and is not a good default candidate. In the
  TDG-impact reroute comparison, `gamma = 50` is safer for dense outlier cases,
  while `gamma = 25` wins more individual datasets. This supports a paper
  statement that the method is stable for `gamma` in the `25-50` range and that
  `90` is an over-selection stress setting.
- The current exp4 impact-weight CSV is incomplete:
  `python/results/experiments/exp4_parameter_sensitivity/bj_synthetic_capacity2_cap10e8/impact_weight/csv/gro_ablation_tdg_excess_full_impact_sweep_capacity2_cap10e8.csv`
  has `11351` data rows, not the expected full grid. Do not cite it as a
  completed paper-facing result. A complete diagnostic subset exists for
  weights `5,10,20` in the exp1 raw/analysis files, and it reinforces the
  current caveat that TDG-impact rerouting is less stable than selection.
- Delta-compression sensitivity is still pending as a clean exp4 result. There
  is only a single `delta=2400` probe in the exp3 peak1h directory, so it should
  not be cited until matched `600/1200/2400` runs are produced on the same
  workload and machine setting.

Suggested execution commands:

```bash
make gro_ablation_test

mkdir -p \
  python/results/experiments/exp4_parameter_sensitivity/bj_synthetic_capacity2_cap10e8/gamma/csv \
  python/results/experiments/exp4_parameter_sensitivity/bj_synthetic_capacity2_cap10e8/impact_weight/csv \
  python/results/experiments/exp4_parameter_sensitivity/real_peak1h_delta/csv \
  logs

# Gamma sensitivity on BJ synthetic.
nohup ./gro_ablation_test config/config_bj_capacity2_cap10e8.yaml \
  --query-dir data/BJ_Synthetic_query_sets \
  --output python/results/experiments/exp4_parameter_sensitivity/bj_synthetic_capacity2_cap10e8/gamma/csv/gro_ablation_tdg_excess_gamma_sweep_capacity2_cap10e8.csv \
  --selection-methods tdg_excess \
  --reroute-methods normal,tdg \
  --fixed-fractions 10 \
  --tdg-gammas 25,50,90 \
  --impact-weights 20 \
  --random-seed 0 \
  > logs/exp4_bj_tdg_excess_gamma_sweep_capacity2_cap10e8.log 2>&1 &

# Impact-weight sensitivity after fixing gamma.
nohup ./gro_ablation_test config/config_bj_capacity2_cap10e8.yaml \
  --query-dir data/BJ_Synthetic_query_sets \
  --output python/results/experiments/exp4_parameter_sensitivity/bj_synthetic_capacity2_cap10e8/impact_weight/csv/gro_ablation_tdg_excess_impact_weight_sweep_gamma50_capacity2_cap10e8.csv \
  --selection-methods tdg_excess \
  --reroute-methods tdg \
  --fixed-fractions 10 \
  --tdg-gammas 50 \
  --impact-weights 0,10,20,30,50 \
  --random-seed 0 \
  > logs/exp4_bj_tdg_excess_impact_weight_sweep_gamma50_capacity2_cap10e8.log 2>&1 &

# Delta-compression sensitivity on one representative 100k peak workload.
for delta in 600 1200 2400; do
  ./gro_ablation_test config/config_bj_capacity2_cap10e8_iter5.yaml \
    --query-file data/BJ_Real_query_sets_scalability_inner_progressive_peak1h/BJRealRep10-4.txt \
    --output python/results/experiments/exp4_parameter_sensitivity/real_peak1h_delta/csv/gro_delta${delta}_BJRealRep10-4.csv \
    --selection-methods tdg_excess \
    --reroute-methods tdg \
    --fixed-fractions 10 \
    --tdg-gammas 50 \
    --impact-weights 20 \
    --candidate-filter score_top \
    --tdg-mode compressed \
    --conflict-threshold 5000 \
    --delta-compress "$delta" \
    --random-seed 0 \
    > "logs/exp4_delta${delta}_BJRealRep10-4.log" 2>&1
done
```

Paper presentation:

Use one compact table, not a large figure. Recommended rows are:

```text
gamma = 25 / 50 / 90
impact_weight = 0 / 10 / 20 / 30 / 50
delta_compress = 600 / 1200 / 2400
```

For `gamma` and `impact_weight`, report final average travel time, selected
fraction, and runtime. For `delta_compress`, report TDG size, runtime, and
final TTT relative to `delta_compress = 1200`.

## Subsection 5: Overall Effectiveness

Purpose:

Compare the complete GRO method against simple iterative baselines and
literature baselines under the same traffic evaluation pipeline.

Datasets:

```text
BJ Synthetic 3 x 3 x 30
Real-world taxi workload, large setting such as 100k queries
```

Iterative convergence figure:

Only include iterative methods:

```text
baseline_random_normal
baseline_delayed_normal
tdg_excess_normal
tdg_excess_full
```

Metrics for the convergence figure:

```text
TTT over iterations
average query travel time over iterations
runtime per iteration, if space allows
```

Final comparison table:

Include both iterative and one-shot methods:

```text
shortest-path initialization
baseline_delayed_normal, final iteration
tdg_excess_normal, final iteration
tdg_excess_full, final iteration
SVP
GOR
SOR
FAHL
```

Metrics for the final table:

```text
final TTT
average query travel time
improvement over shortest-path initialization
total runtime
preprocessing/index construction time for FAHL
query/routing time for FAHL
memory/index size, if available
```

Fairness notes:

- All methods output routes and are evaluated by the same traffic simulator.
- Iterative methods are allowed multiple rerouting iterations; report their
  final-iteration result and total runtime.
- SVP, GOR, SOR, and FAHL are one-shot or independent-routing baselines; compare
  their final route set, not convergence.
- FAHL uses a fixed flow distribution derived from a specified reference route
  set. Report index construction separately from query time.
- `baseline_delayed_tdg_reroute` belongs in rerouting ablation, not the overall
  baseline table.

## Current Result Files

BJ capacity2/cap10e8 iterative files:

```text
python/results/bj/gro_ablation_baseline_delayed_normal_capacity2_cap10e8.csv
python/results/bj/gro_ablation_baseline_delayed_tdg_reroute_capacity2_cap10e8.csv
python/results/bj/gro_ablation_tdg_anchor_normal_capacity2_cap10e8.csv
python/results/bj/gro_ablation_tdg_excess_normal_capacity2_cap10e8.csv
```

Current comparison plots:

```text
python/results/bj/bj_delayed_vs_tdg_anchor_normal_capacity2_cap10e8_ttt.png
python/results/bj/bj_delayed_vs_tdg_anchor_normal_capacity2_cap10e8_log_ttt.png
```

Command records are kept in:

```text
notes/executables.md
```

Algorithm-change notes are kept in:

```text
notes/gro_algorithm_changes.md
notes/tdg_compression.md
notes/tdg_impact_normalization.md
```

## Paper-Level Story

The experimental narrative should be:

1. Selection ablation: TDG selection gives a better set of queries to reroute.
2. Rerouting ablation: TDG impact helps replacement routes avoid harmful
   congestion feedback.
3. Compression/scalability: compressed TDG keeps the dependency signal usable at
   large scale.
4. Parameter sensitivity: the chosen defaults are stable enough.
5. Overall effectiveness: complete GRO beats simple iterative baselines and is
   competitive with or better than literature baselines under the same
   simulation-based evaluation.
