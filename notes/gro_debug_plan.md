# GRO Component Debug Plan

This note is the working record for debugging why the current TDG-based GRO
curve does not outperform the iterative baseline on MH synthetic datasets.
Future experiments should update this file with commands, result summaries,
and conclusions.

## Current Symptom

Using `python/results/mh_synthetic_all_cpp.csv`, the averaged total travel time
plot shows that `tdg` and `tdg_selection_baseline` often improve more slowly
than `baseline`.

Current method names:

```text
baseline                 random selection + normal TD-Dijkstra
tdg_selection_baseline   TDG selection + normal TD-Dijkstra
tdg_reroute_baseline     random selection + TDG reroute/batching
tdg                      TDG selection + TDG reroute/batching
```

Initial observation from the existing CSV:

- `tdg_selection_baseline` is usually worse than `baseline`.
- `tdg_reroute_baseline` is sometimes close to or better than `baseline`.
- This suggests selection is the first component to inspect, but reroute
  weighting and batching still need isolated tests.

## Main Debug Questions

1. Does candidate selection keep enough useful queries?
2. Does TDG selection choose better queries than random selection when the
   selected-query count is controlled?
3. Does TDG reroute improve selected routes, or does its score push routes into
   worse traffic outcomes?
4. Does batching help or hurt reroute quality?
5. Are parameters such as `theta_percentile`, `gamma`, `impact_weight`,
   `lambda`, and `conflict_threshold` causing the current behavior?

## Required Output Columns

The current experiment CSV only records total travel time by iteration. That is
not enough. The next code change should make `AlgorithmResult` store per-
iteration diagnostic rows and make `mh_synthetic_experiment.cpp` write them.

Principle: store raw measurements only. Do not store values that can be
computed directly from other columns, such as `selected_fraction`,
`candidate_fraction`, or `delta_total`.

Core CSV columns:

```text
run_id
dataset
hop
rep
seed
algorithm
query_count
iteration
total_before
total_after
candidate_count
selected_count
batch_count
initial_total_travel_time
final_total_travel_time
```

For the selection diagnostic, parameters are recorded directly in each method
row so the result stays in one CSV file:

```text
run_id
dataset
hop
rep
seed
algorithm
query_count
lambda
gamma
theta_percentile
random_seed
```

Core TDG structure columns:

```text
tdg_node_count
tdg_route_arc_count
tdg_same_edge_arc_count
tdg_edge_timeline_count
tdg_max_out_degree
tdg_max_flow
tdg_mean_flow
tdg_max_congestion_ratio
tdg_mean_congestion_ratio
```

Optional timing columns:

```text
evaluate_sec
tdg_sec
impact_sec
candidate_sec
select_sec
batch_sec
reroute_sec
iteration_sec
```

Optional selection-detail columns:

```text
selection_rounds
rejected_count
mean_selected_score
max_selected_score
mean_all_query_score
max_all_query_score
```

Optional batch-detail columns:

```text
mean_batch_size
max_batch_size
```

Important interpretation rule: iteration `i` currently evaluates `total_before`
before rerouting. The effect of rerouting in iteration `i` should be measured
with a fresh `evaluate_traffic(...)` after route replacement and written as
`total_after`.

## Debug Step 1: Output Sanity

Goal: verify that every algorithm reports comparable per-iteration statistics.

Checks:

- `baseline` selected count should equal `baseline_fraction_to_reroute *
  query_count`, up to integer truncation.
- `selected_count` is a core measurement for every iterative method.
- `tdg_selection_baseline` and `tdg` should report both `candidate_count` and
  `selected_count`.
- `tdg_reroute_baseline` and `tdg` should report `batch_count`.
- TDG-based methods should report TDG structure statistics, including node
  count, route arcs, same-edge arcs, timeline count, degree, flow, and
  congestion-ratio summaries.
- Every run should have one metadata row recording the key parameters used.

Expected outcome:

- We can identify whether a bad iteration is associated with too few selected
  queries, unusual TDG structure, or a particular parameter setting.

Result log:

```text
Date:
Command:
Dataset/filter:
Observation:
Conclusion:
Next action:
```

## Debug Step 2: TDG Selection Without Candidate Filter

Priority: test whether the TDG selection method itself is meaningful before
debugging candidate filtering. In this step, assume there is no candidate
filter: the candidate set is all queries.

Core question:

```text
If TDG selection can choose from all queries, does it find the queries that
should be rerouted?
```

Hypothesis: TDG selection may pick high-impact routes, but those routes may not
be the routes whose rerouting reduces system total travel time.

Current code area:

- `GROAlgorithm::select_queries(...)`
- Route score is sum of TDG node impacts on the current trajectory.
- Removability constraint uses `gamma`.
- Ranking is refreshed after removals.

Primary diagnostic: no-filter selection vs random selection with the same
selection size, measured by the total-travel-time reduction of the remaining
queries after removing the selected queries.

Do not use candidate filtering in this step. Do not reroute in this step. The
only question is whether TDG selection, when allowed to choose from all queries,
selects a set of `k` queries whose removal gives the remaining queries a larger
total travel time decrease than removing `k` randomly selected queries.

For one small dataset and one iteration:

1. Evaluate current routes and keep the resulting trajectories.
2. Build TDG and compute TDG impacts.
3. Run TDG selection with `candidate_set = all queries`; call the selected set
   `S`.
4. Run random selection with exactly `|S|` selected queries; call that set `R`.
5. For `S`, remove those queries from the current route set, re-evaluate traffic
   on the remaining queries only, and record remaining-query total travel time.
6. For `R`, do the same removal-and-re-evaluation.
7. Compare how much the remaining-query total travel time decreases under TDG
   selection vs random selection.

Definition:

```text
unselected_after_remove(S) = total travel time after evaluating only queries
                             not in S with their current routes
```

Use the same definition for the random set `R`. Since `S` and `R` have the same
size and share the same `total_before`, compare
`tdg_unselected_after_remove` and `random_unselected_after_remove` directly.
A smaller value means the selected set relieved more congestion from the
remaining current routes.

Core metrics:

```text
removal_mode
selected_count
important_node_count
total_before
tdg_unselected_after_remove
random_unselected_after_remove
tdg_prepare_sec
tdg_select_sec
random_select_sec
```

TDG impact-score sanity metrics:

```text
mean_tdg_selected_impact_score
mean_random_selected_impact_score
mean_all_query_impact_score
```

Parameter experiments after the basic comparison:

```text
gamma = 0.0, 0.25, 0.5, 0.75, 1.0
removal_mode = all_nodes, congestion_important, anchor_important
```

Fairness controls:

- Compare TDG selection against random selection with the same selected-query
  count per iteration.
- If TDG selects 120 queries, random should also select 120, not 30% of all
  queries.
- Do not reroute selected queries. Removing selected queries and re-evaluating
  the remaining current routes isolates selection quality from reroute quality.
- In `congestion_important`, the removal constraint checks only TDG nodes whose
  congestion ratio is at or above `theta_percentile`.
- In `anchor_important`, the removal constraint checks only anchor TDG nodes
  with positive anchor score from the TDG compression anchor detector.

Metrics:

- `selected_count`
- `important_node_count`
- `total_before`
- `tdg_unselected_after_remove`
- `random_unselected_after_remove`
- `tdg_prepare_sec`
- `tdg_select_sec`
- `random_select_sec`

Impact-score diagnostics:

```text
mean_tdg_selected_impact_score
mean_random_selected_impact_score
mean_all_query_impact_score
```

These columns let us check whether lower
`tdg_unselected_after_remove` is correlated with selecting routes that have
larger TDG impact scores.

Interpretation:

- If `tdg_unselected_after_remove` is not smaller than
  `random_unselected_after_remove`, the TDG selection method is not selecting
  queries that better relieve the remaining traffic than random selection.
- Very low selected counts mean `gamma` or candidate filtering may be too
  restrictive. In this no-filter step, that points to `gamma` or the
  removability logic, not candidate filtering.

Result log:

```text
Date:
Command:
Dataset/filter:
Selected count:
Random selection same count:
After-remove values:
Observation:
Conclusion:
Next action:
```

## Debug Step 3: Candidate Filter

Only debug candidate filtering after Step 2 shows that TDG selection has signal
when it can choose from all queries.

Hypothesis: candidate selection may remove useful queries before the TDG
selection step ever sees them.

Current code area:

- `GROAlgorithm::select_candidates(...)`
- Uses `theta_percentile`
- Targets nodes with congestion ratio `> theta`

Primary diagnostic: candidate filter vs no-filter TDG selection.

For one small dataset and one iteration:

1. Run Step 2's no-filter TDG selection to get `S_all`.
2. Build candidate set `C`.
3. Run TDG selection using candidate set `C` to get `S_filter`.
4. For both `S_all` and `S_filter`, remove selected queries and re-evaluate
   only the remaining current routes.
5. Compare candidate-filtered selection against no-filter selection using the
   same after-remove remaining-query total-travel-time metric from Step 2.

Core metrics:

```text
candidate_count
selected_count_no_filter
selected_count_with_filter
candidate_selected_overlap_count
unselected_after_no_filter_selection
unselected_after_candidate_filter_selection
```

Structural diagnostics:

```text
tdg_size
theta_value
targeted_tdg_node_count
source_tdg_node_count
tdg_nodes_above_theta
tdg_nodes_equal_theta
tdg_nodes_below_theta
targeted_query_count
source_query_count
candidate_count
```

These structural numbers matter because the current implementation uses
`congestion_ratio > theta`. If many TDG nodes have exactly the same congestion
ratio as `theta`, the strict comparison can make the candidate set much smaller
than intended.

Candidate-filter ablations:

```text
tdg_selection_all_queries
    Step 2 reference: no candidate filter

tdg_selection_candidate_filter
    current TDG selection using candidate set C

random_same_size_as_candidate
    random query set with |C| queries, repeated several seeds

theta_strict_vs_inclusive
    compare congestion_ratio > theta vs congestion_ratio >= theta
```

Interpretation:

- Low candidate recall of `tdg_selection_all_queries` means the filter changes
  what the TDG selector is allowed to do.
- High overlap but poor selection gain means candidate filtering is probably
  not the bottleneck.
- High `tdg_nodes_equal_theta` plus low `candidate_count` suggests the strict
  `> theta` threshold is unstable and should be tested against `>= theta`.

Secondary parameter sweep, only after the coverage test:

```text
theta_percentile = 50, 70, 80, 90, 95
```

Result log:

```text
Date:
Command:
Dataset/filter:
Iteration:
Candidate count:
Diagnostic top-K:
Recall/precision:
Observation:
Conclusion:
Next action:
```

## Debug Step 4: TDG Reroute Weight

Hypothesis: `impact_weight` may be too weak or too strong. The reroute score is:

```text
score = travel_time + impact_weight * impact / 100
```

Current code area:

- `GROAlgorithm::reroute_query(...)`

Experiments:

```text
impact_weight = 0.0, 0.05, 0.15, 0.30, 0.50, 1.00
```

Sanity check:

- `impact_weight = 0.0` should behave like normal TD-Dijkstra, except for
  batching/implementation differences.
- If `impact_weight = 0.0` differs strongly from `baseline_reroute_queries`,
  inspect reroute implementation first.

Metrics:

- `selected_count`
- `batch_count`
- `total_before`
- `total_after`
- `impact_weight`

Interpretation:

- If larger `impact_weight` increases system total travel time, the impact
  penalty is too aggressive or poorly scaled.
- If `impact_weight = 0.0` is already poor, the issue may be reroute mechanics
  or batching, not the impact score.

Result log:

```text
Date:
Command:
Dataset/filter:
impact_weight values:
Observation:
Conclusion:
Next action:
```

## Debug Step 5: Batching

Hypothesis: batching may reroute queries together that should be sequentially
updated, or it may create too many small batches.

Current code area:

- `GROAlgorithm::batch_queries(...)`
- Uses `conflict_threshold`

Experiments:

```text
conflict_threshold = 0, 25, 50, 70, 100
```

Metrics:

- `selected_count`
- `batch_count`
- `mean_batch_size`
- `max_batch_size`
- `total_before`
- `total_after`
- runtime

Interpretation:

- If smaller conflict threshold improves quality but increases time, batching
  is trading quality for efficiency.
- If batch settings barely change quality, selection/reroute scoring is more
  important.

Result log:

```text
Date:
Command:
Dataset/filter:
conflict_threshold values:
Observation:
Conclusion:
Next action:
```

## Debug Step 6: TDG Impact Propagation

Hypothesis: `lambda` may over-propagate or under-propagate impact along the TDG.

Current code area:

- `GROAlgorithm::compute_tdg_impact(...)`

Experiments:

```text
lambda = 0.0, 0.25, 0.45, 0.70, 1.00
```

Metrics:

- `mean_selected_score`
- `mean_all_query_score`
- selected-query overlap across lambda values
- `total_before`
- `total_after`

Interpretation:

- If selected queries change a lot but quality does not improve, impact
  propagation may not be aligned with the objective.
- If `lambda = 0.0` performs better, downstream propagation may be introducing
  noise.

Result log:

```text
Date:
Command:
Dataset/filter:
lambda values:
Observation:
Conclusion:
Next action:
```

## Recommended Experiment Order

1. Implement per-iteration diagnostic output.
2. Run one small panel:

```bash
./mh_synthetic_experiment config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/mh_synthetic_debug_hop10_rep1_seed0.csv \
  --algorithms tdg,baseline,tdg_selection_baseline,tdg_reroute_baseline \
  --hop 10 \
  --rep 1 \
  --seed 0
```

3. Check whether counts and deltas make sense.
4. Run TDG selection without candidate filter on one seed and compare its
   remaining-query total-travel-time reduction against random selection with
   the same selected count.
5. If Step 4 shows TDG selection has signal, run candidate-filter coverage
   tests.
6. Run reroute weight tests on one seed.
7. Run batching tests on one seed.
8. Only after the mechanism is understood, run all MH synthetic datasets.

## Running Notes

Add dated entries below. Keep each entry short and evidence-based.

### Entry Template

```text
Date:
Code version / commit:
Command:
Datasets:
Parameters:
Main numbers:
Conclusion:
Next action:
```

### Results

```text
Date: 2026-05-19
Code version / commit: local working tree
Command:
./gro_selection_debug_test config/config.yaml \
  --query-file data/MH_Synthetic_query_sets/Hop10Rep1-0.txt \
  --output python/results/gro_selection_debug_hop10rep1_seed0_gamma_iterations.csv \
  --gamma-values 0,25,50,75,100 \
  --random-seed 0
Datasets: Hop10Rep1-0
Parameters: recorded directly in each CSV row
Main numbers:
gamma0:   selected_count=0,    TDG gain=0,       random gain=0
gamma25:  selected_count=14,   TDG gain=84430,   random gain=54401
gamma50:  selected_count=436,  TDG gain=2306677, random gain=2514932
gamma75:  selected_count=692,  TDG gain=1867478, random gain=2219682
gamma100: selected_count=1024, TDG gain=0,       random gain=0
Conclusion:
Smoke run succeeded. On this single seed, TDG selection is better than random
when gamma=25, but worse than random when gamma=50 or gamma=75. gamma=0 and
gamma=100 are degenerate in this diagnostic.
Next action:
Run the diagnostic across more seeds/panels before drawing a conclusion.
```
