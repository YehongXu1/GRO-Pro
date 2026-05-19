# GRO Experiment Plan

This note records the experiment plan for validating the TDG/TIE idea in GRO.
The purpose is not only to show that the final method works, but also to show
which component is responsible for the improvement.

## Main Question

The paper's core claim should be:

```text
TDG/TIE captures spatiotemporal dependencies among massive route trajectories,
and this dependency information helps iterative global rerouting select better
queries and construct less harmful replacement routes.
```

Therefore, the experiments must answer three questions:

1. Does TDG-based selection choose more system-impactful queries than simple
   selection heuristics?
2. Does impact-aware rerouting produce better replacement routes than normal
   time-dependent Dijkstra?
3. In the full iterative paradigm, do TDG selection and TDG rerouting improve
   total travel time faster and more consistently than the baselines?

## Dataset Roles

Use the MH synthetic datasets as the main ablation dataset.

Reason:

- `hop` controls query distance.
- `rep` controls demand density.
- The 3 x 3 grid, `Hop10/20/40 x Rep1/2/4`, lets us explain when TDG/TIE is
  useful instead of only reporting one averaged number.

Use real-world taxi request samples, including amplified query sets, as the
final realism evaluation.

Reason:

- Real taxi OD and departure-time distributions show that the method is not
  only effective on controlled synthetic data.
- Amplification gives a way to test scalability and congestion strength under
  realistic spatial-temporal demand patterns.

## Method Names

Use these names consistently in result files and figures.

| Name | Selection | Reroute |
| --- | --- | --- |
| `baseline_random_normal` | random fixed fraction | normal TD-Dijkstra |
| `baseline_delayed_normal` | most delayed fixed fraction | normal TD-Dijkstra |
| `baseline_random_tdg_reroute` | random fixed fraction | TDG impact-aware reroute |
| `baseline_delayed_tdg_reroute` | most delayed fixed fraction | TDG impact-aware reroute |
| `tdg_selection_normal` | TDG anchor selection | normal TD-Dijkstra |
| `tdg_excess_normal` | TDG excess-relief selection | normal TD-Dijkstra |
| `tdg_full` | TDG anchor selection | TDG impact-aware reroute |
| `tdg_excess_full` | TDG excess-relief selection | TDG impact-aware reroute |

In the current `gro_ablation_test` CSV, these are represented by:

```text
selection_method = random, most_delayed, tdg, tdg_excess
removal_mode     = none, anchor_important
reroute_method   = normal_td_dijkstra, tdg_impact_reroute
selection_fraction = 10, 30, or -1 for TDG selection
gamma = -1 for simple selection, 50 for TDG selection
impact_weight = 0 for normal_td_dijkstra, 30 for tdg_impact_reroute
```

## Experiment 1: Selection-Only Quality

Goal:

```text
Test whether TDG selection chooses queries whose removal gives more congestion
relief than simple selection methods.
```

Protocol:

- Use current routes before rerouting.
- Do not reroute selected queries.
- Remove selected queries from the current route set.
- Re-evaluate the remaining queries only.
- Compare remaining-query average travel time.

Primary metric:

```text
remaining_avg_query_tt =
    remaining_total_travel_time / remaining_query_count
```

A better selection method should leave a lower `remaining_avg_query_tt` after
the selected queries are removed.

Baselines:

- random 10%
- random 30%
- most_delayed 10%
- most_delayed 30%

TDG settings:

- `removal_mode = anchor_important`
- `gamma = 25, 50, 75`

Main output to report:

- value over random
- percentage over random
- value over most_delayed
- percentage over most_delayed
- selection running time
- selected query count or selected fraction
- TDG node count and important node count

Current evidence:

```text
TDG selection is clearly better than random on average.
TDG selection is only moderately better than most_delayed.
The selection evidence is positive but not yet the strongest part of the paper.
```

Selection methods to include in the next ablation:

- `tdg_anchor`: the original TDG selection method.
- `tdg_excess`: the new excess-relief TDG selection method.

Algorithm details for `tdg_excess` are recorded separately in
`notes/gro_algorithm_changes.md`.

Decision rule:

```text
If TDG selection only beats random but not most_delayed, do not overclaim the
selection contribution. Frame it as a useful dependency-aware component whose
main value is strongest in the full iterative setting.
```

## Experiment 2: Reroute-Only Quality

Goal:

```text
Test whether TDG impact-aware rerouting creates better replacement routes than
normal TD-Dijkstra when the selected queries are fixed.
```

Protocol:

- Use random selection to avoid mixing selection quality into this experiment.
- Select the same fraction of queries for every reroute method.
- Compare normal TD-Dijkstra against TDG impact-aware reroute.
- Vary `impact_weight`.

Settings:

- selected fraction from `config/config.yaml`
- `impact_weight = 0, 5, 15, 30, 50, 100`
- `impact_weight = 0` means normal TD-Dijkstra

Primary metric:

```text
total_after = total travel time after rerouting and re-evaluation
gain_vs_normal = normal_total_after - tdg_total_after
```

Main output to report:

- average gain over normal TD-Dijkstra
- win rate over normal TD-Dijkstra
- reroute running time
- extra running time over normal TD-Dijkstra
- best or default impact weight

Current evidence:

```text
impact_weight = 30 is currently the best default.
TDG reroute has stronger evidence than TDG selection.
The improvement is stable and the extra time is moderate.
```

Decision rule:

```text
If impact-aware reroute consistently wins for weights near 15/30/50, then the
reroute component can be presented as strong evidence that TDG impact scores
are meaningful.
```

## Experiment 3: Iterative Component Ablation

Goal:

```text
Test whether TDG selection and TDG reroute help inside the actual iterative GRO
paradigm.
```

This is the most important ablation for the paper.

Dataset:

```text
MH synthetic, all Hop10/20/40 x Rep1/2/4 configurations.
```

Run split by configuration:

```text
Hop10Rep1, Hop10Rep2, Hop10Rep4,
Hop20Rep1, Hop20Rep2, Hop20Rep4,
Hop40Rep1, Hop40Rep2, Hop40Rep4.
```

Methods:

- `baseline_random_normal`
- `baseline_delayed_normal`
- `baseline_random_tdg_reroute`
- `baseline_delayed_tdg_reroute`
- `tdg_selection_normal`
- `tdg_excess_normal`
- `tdg_full`
- `tdg_excess_full`

Default parameters:

```text
fixed_fractions = 10, 30
tdg_gamma = 50
tdg_removal_mode = anchor_important
impact_weight = 30
max_iterations = config/config.yaml
```

Primary metrics:

- total travel time per iteration
- total reduction after each iteration
- final total travel time
- convergence speed
- per-iteration selected count
- per-iteration reroute time
- per-iteration TDG preparation and selection time
- method total time

Required comparisons:

1. `tdg_selection_normal` vs `baseline_random_normal`
2. `tdg_selection_normal` vs `baseline_delayed_normal`
3. `tdg_excess_normal` vs `tdg_selection_normal`
4. `tdg_excess_normal` vs `baseline_random_normal`
5. `tdg_excess_normal` vs `baseline_delayed_normal`
6. `baseline_random_tdg_reroute` vs `baseline_random_normal`
7. `baseline_delayed_tdg_reroute` vs `baseline_delayed_normal`
8. `tdg_full` and `tdg_excess_full` vs all non-TDG baselines

Interpretation:

```text
Selection is useful if TDG selection + normal reroute beats simple selection +
normal reroute under the same iteration budget.

Reroute is useful if simple selection + TDG reroute beats the same simple
selection + normal reroute.

The full method is strong if TDG selection + TDG reroute gives the fastest and
largest total travel time reduction.
```

Decision rule:

```text
For a strong VLDB-style claim, tdg_full should not only beat random. It should
also beat or clearly match most_delayed while giving better final total travel
time or faster convergence.
```

## Experiment 4: TDG Compression Ablation

Goal:

```text
Show that TDG compression improves efficiency while preserving enough
dependency information for selection and reroute quality.
```

Settings:

- fine TDG or original TDG
- compressed TDG
- several dependency representation thresholds, if supported

Metrics:

- TDG node count
- TDG arc count
- important node count
- compression time
- impact computation time
- selection time
- reroute time
- total travel time after each iteration

Main claim to support:

```text
Compression reduces TDG cost substantially, while preserving the quality trend
of the uncompressed dependency graph.
```

## Experiment 5: End-to-End Baseline Comparison

Goal:

```text
Compare the final GRO method against external baselines under the same
evaluation pipeline.
```

Baselines:

- shortest path initialization
- iterative baseline without TDG
- SVP
- GOR greedy
- SOR
- FAHL

Datasets:

- MH synthetic, summarized by hop/rep
- real taxi amplified query sets

Metrics:

- final total travel time
- average query travel time
- congestion/load metrics if available
- algorithm runtime
- preprocessing/index construction time for FAHL
- query/routing time for FAHL
- memory or index size if available

Fairness notes:

- All methods should output routes and be evaluated by the same traffic
  evaluator.
- FAHL index construction time should be separated from query time.
- FAHL uses a fixed flow distribution derived from a reference route set.
- SOR/GOR use route-induced flow during route construction.
- GRO uses iterative feedback and should be evaluated by convergence and final
  total travel time.

## Experiment 6: Parameter Sensitivity

Goal:

```text
Show that the method does not rely on one fragile parameter setting.
```

Parameters:

- `gamma` for TDG selection
- `impact_weight` for TDG reroute
- selected fraction for simple baselines
- compression threshold, if applicable

Recommended reporting:

- keep one default setting for main experiments
- use one compact sensitivity figure or table
- avoid tuning each dataset separately

Current default:

```text
gamma = 50
removal_mode = anchor_important
impact_weight = 30
simple baseline fractions = 10, 30
```

## Result Files

Current relevant files:

```text
python/results/gro_selection_debug_removal_modes.csv
python/results/gro_simple_selection_baselines_10_30.csv
python/results/gro_reroute_debug.csv
python/results/gro_ablation_Hop10Rep1.csv
python/results/gro_ablation_Hop10Rep2.csv
python/results/gro_ablation_Hop10Rep4.csv
python/results/gro_ablation_Hop20Rep1.csv
python/results/gro_ablation_Hop20Rep2.csv
python/results/gro_ablation_Hop20Rep4.csv
python/results/gro_ablation_Hop40Rep1.csv
python/results/gro_ablation_Hop40Rep2.csv
python/results/gro_ablation_Hop40Rep4.csv
python/results/gro_ablation.csv
```

Command records are kept in:

```text
notes/executables.md
```

Selection result summary is kept in:

```text
notes/gro_selection_debug_results.md
```

## Paper-Level Expected Story

The strongest paper story is not:

```text
We propose another heuristic routing algorithm.
```

The stronger story is:

```text
We introduce a traffic-dependency graph over massive route trajectories and use
it to estimate system-level impact of route modifications. This enables an
iterative optimizer to select and reroute queries using dependency-aware
signals instead of purely local delay or random choices.
```

The experiments should support this story in order:

1. TDG impact scores contain useful selection/reroute signal.
2. TDG reroute avoids recreating congestion better than normal TD-Dijkstra.
3. TDG selection and TDG reroute together improve iterative optimization.
4. The method scales under synthetic and real amplified workloads.

## Open Risks

The current risk is that selection-only improvement over `most_delayed` may be
modest. If this remains true, the paper should avoid overclaiming selection as
the only major contribution.

The likely stronger claim is:

```text
TDG/TIE is a shared dependency-aware mechanism. Its reroute signal is strong,
and its selection signal becomes most useful when evaluated inside the full
iterative optimization loop.
```
