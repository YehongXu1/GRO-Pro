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
fragile.

Dataset:

```text
BJ Synthetic 3 x 3 x 30 for gamma and impact_weight
One representative real-world workload for delta_compress
```

Parameters:

```text
gamma: 25, 50
impact_weight: 0, 30, optionally 50
delta_compress: small/default/large
```

Metrics:

```text
final TTT
average query travel time
selected_fraction
runtime
TDG size for delta_compress
```

Keep this subsection compact. It should justify defaults, not become a second
main evaluation.

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
