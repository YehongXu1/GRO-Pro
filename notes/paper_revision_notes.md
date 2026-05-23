# GRO Paper Revision Notes

This note records writing and structure decisions for the GRO VLDB paper. It is
separate from `new_chat_handoff_vldb.md`: the handoff is operational context;
this file is for paper story, positioning, and revision choices.

## First Principle

The goal is a publishable VLDB paper, not a collection of positive-looking
numbers. Every claim should be judged by whether a skeptical reviewer would
consider it meaningful, fair, and worth publishing.

Small real-data TTT gains, e.g. 1-2%, should not anchor the paper. They can only
support the story when paired with a strong scalability/runtime/memory result or
a clear explanation that the workload has limited optimization headroom.

## Current Story Shift

The selection method has changed from the draft's original two-level TDG
selection to the newer `tdg_excess` selection.

The current main method should be described as:

```text
TDG-guided excess-relief selection
```

High-level idea:

1. Build a TDG from current route trajectories.
2. Compute TDG impact once as a fixed node-importance signal.
3. Mark important TDG nodes, currently anchor-based with congestion fallback.
4. Define node relief as:

   ```text
   node_relief(v) = normalized_TDG_impact(v) * max(0, flow(v) / capacity(v) - 1)
   ```

5. Greedily select routes that cover the largest remaining weighted excess
   congestion.
6. After selecting a route, update only working TDG flow for covered nodes.
7. Stop when remaining weighted excess mass drops below the `gamma` target.

This is different from the draft's original Algorithm 4, which ranks candidates
by TDG impact, tests removability, updates the TDG structurally, and refreshes
rankings when they become stale.

## Candidate Identification Repositioning

The draft currently treats candidate query identification as part of Query
Selection. This is still conceptually correct, but the paper story should be
repositioned.

The old source-congestion candidate identification in the draft should not be
used as a paper-facing result unless it is redesigned. On the current highly
congested peak workload it keeps too many queries:

```text
BJRealRep1-0, theta=90: candidate_fraction = 94.93%
BJRealRep1-1, theta=90: candidate_fraction = 93.25%
BJRealRep1-0, theta=50: candidate_fraction = 96-97% in early iterations
```

The reason is structural: the old rule keeps a query if it touches any source
congestion node. In a highly congested peak workload, most routes touch at least
one such node, so the OR-style filter has weak pruning power.

Candidate identification and TDG compression are both scalability reductions:

- Candidate identification reduces the **query search space**.
- TDG compression reduces the **TDG/state space**.

They should be discussed together in a scalability-oriented section rather than
presented as quality-improving core selection components.

Suggested framing:

```text
Candidate identification is not intended to improve route quality by itself.
It restricts incremental selection to structurally relevant queries so that
TDG-guided selection remains feasible at large scale.
```

This protects the paper if candidate filtering trades a small amount of quality
for substantial runtime savings.

## Congestion Pattern Findings

We ran a congestion-pattern diagnostic on the peak1h scalability workload,
using the initial routes and initial TDG only. The goal was to understand
whether congestion is concentrated in a few bottlenecks or distributed across
many interacting congestion regions.

Diagnostic command shape:

```text
./congestion_pattern_diagnostic config/config_bj_capacity2_cap10e8_iter5.yaml \
  --query-file data/BJ_Real_query_sets_scalability_inner_progressive_peak1h/BJRealRep1-0.txt
```

Key findings:

```text
BJRealRep1-0:
active_relief_components = 4341
source_candidate_fraction = 94.93%
positive_score_query_fraction = 95.28%
active_query_component_mean = 3.23
active_query_component_p90 = 6
multi_component_query_fraction = 80.44%
component_top10_mass_share = 34.79%
top10pct_query_score_share = 83.92%
top10pct_component_mass_share = 50.83%

BJRealRep1-1:
active_relief_components = 4422
source_candidate_fraction = 93.25%
positive_score_query_fraction = 95.86%
active_query_component_mean = 3.35
active_query_component_p90 = 6
multi_component_query_fraction = 79.08%
component_top10_mass_share = 42.84%
top10pct_query_score_share = 88.01%
top10pct_component_mass_share = 55.32%
```

Interpretation:

- Congestion is not a single dominant bottleneck. Active relief mass is spread
  over thousands of weak TDG components.
- Most active queries touch multiple congestion components, so removing one
  route can change the marginal value of many other routes.
- Global static top-score pruning is unbalanced: the top 10% query-score mass
  covers only about half of the component mass. Score concentration and
  structural coverage are different.
- The core challenge is therefore not simply identifying high-score queries.
  The challenge is preserving enough structurally diverse candidates so the
  later dynamic selection step can handle interactions among query removals.

## Proposed Candidate Identification Algorithm

Working name:

```text
Component-Balanced Candidate Identification
```

Goal:

```text
Construct a small candidate set that preserves coverage across congestion
components, rather than selecting only globally high-scoring routes.
```

This aligns candidate identification with the current selection view:

```text
Query removal benefits are not independent. They interact through shared TDG
bottlenecks, so candidate identification should keep options across different
congestion components before the iterative excess-relief selector makes
state-dependent choices.
```

Definitions:

1. Compute the same positive relief nodes used by `tdg_excess`:

   ```text
   relief(v) = normalized_TDG_impact(v) * max(0, flow(v) / capacity(v) - 1)
   ```

   A TDG node is active if `relief(v) > 0`.

2. Build congestion components by taking weak connected components on the
   subgraph induced by active TDG nodes. Connectivity uses the same TDG
   dependency links as the method:

   ```text
   route dependency edges + same-edge timeline edges
   ```

3. For each component `g`, define its relief mass:

   ```text
   M(g) = sum_{v in g} relief(v)
   ```

4. For each query `q` and component `g` touched by its current trajectory,
   define a component contribution:

   ```text
   A(q, g) = sum_{unique v in trajectory(q) intersect g} relief(v)
   ```

Algorithm:

```text
Input:
  TDG, current trajectories, normalized TDG impacts, gamma

Output:
  Candidate query set C

1. Compute active relief nodes and their weak congestion components.
2. For every query q, scan its TDG nodes once and accumulate A(q, g).
3. For each congestion component g independently:
     a. Sort queries touching g by A(q, g), descending.
     b. Add queries to C until cumulative A(q, g) >= gamma * M(g).
4. Return the union C across all components.
5. Run the existing iterative tdg_excess selection only on C.
```

Parameter choice:

```text
No new paper-facing parameter is introduced.
```

The algorithm reuses the existing `gamma` from excess-relief selection. If
`gamma=50`, candidate identification keeps enough component-local candidates
to cover 50% of each component's one-pass relief mass. The later selector still
decides which candidates to actually reroute.

Why this matches the intuition:

- It is not global top-k pruning.
- Every congestion component receives candidate support proportional to its own
  relief mass.
- A query that bridges multiple components is naturally valuable because it
  appears in multiple component lists but is inserted into the candidate set
  only once.
- The final `tdg_excess` selector still handles marginal interactions by
  updating working TDG flow after each selected query.

Efficiency:

Let `P` be the number of nonzero query-component pairs. The diagnostic suggests
`P` is manageable because active queries touch about 3-4 components on average
for the 10k peak workload.

Complexity:

```text
Build active components: O(|TDG nodes| + |TDG links|)
Build query-component pairs: O(total trajectory length)
Sort component query lists: O(sum_g |Q_g| log |Q_g|)
```

This should be much cheaper than iterative all-query greedy selection, and it
keeps candidate identification conceptually separate from final route removal:
candidate identification preserves balanced search space; selection performs
state-dependent removal.

## Suggested Paper Structure

One cleaner structure is:

```text
1. Introduction
2. Problem Definition
3. Overview
4. Traffic Dependency Graph
   4.1 TDG construction
   4.2 TDG impact computation
   4.3 TDG update under route modification
5. TDG-Guided Global Route Optimization
   5.1 Excess-relief query selection
   5.2 TDG-aware rerouting
   5.3 Iterative optimization loop
6. Scalability Techniques
   6.1 Candidate query identification as query-space reduction
   6.2 TDG compression as state-space reduction
   6.3 Complexity discussion
7. Experiments
```

Alternative if the paper needs fewer structural changes:

```text
6. Query Selection
   6.1 Excess-relief selection assuming a candidate set
   6.2 Candidate identification for scalability
7. TDG-Aware Rerouting
8. Scalability
   8.1 Candidate filtering
   8.2 TDG compression
```

The first structure is cleaner: it separates the quality mechanism from the
scalability mechanisms.

## Experiment Implications

### Component Ablation

Use component ablation mainly to show quality:

- `baseline_random_normal`
- `baseline_delayed_normal`
- `tdg_excess_normal`
- `tdg_excess_full`

Candidate identification does not need to be in the main 3x3 quality figure.
If included, use a compact table:

```text
All candidates vs component-balanced candidates
```

Metrics:

- candidate fraction
- selected fraction
- select time
- final TTT
- quality loss or preservation

### Scalability

Scalability should use the full paper-facing method:

```text
component-balanced candidate identification + tdg_excess selection + TDG compression
```

Also include a limited comparison against all-query selection:

- all-query selection for sizes it can finish, e.g. 10k/20k;
- candidate-filtered selection for the full curve;
- timeout or infeasible status for all-query selection at larger sizes if true.

Key metrics:

- candidate fraction
- selected fraction
- TDG node/edge count
- compressed TDG node/edge count
- build/compress time
- impact time
- selection time breakdown
- reroute time
- method total time
- memory usage if available
- final TTT and TTT preservation

### Real Scalability Workload

Paper-facing scalability workloads should be highly congested. The current gate
is shortest-path congestion inflation in the `10x-100x` range.

Current preferred candidate directory:

```text
data/BJ_Real_query_sets_scalability_inner_progressive_peak1h
```

This is a controlled high-congestion peak workload built from T-Drive-derived
real OD rows. Each query size uses a different source-OD count and central OD
radius before controlled repetition, so the scalability curve is not just a
simple `rep` sweep over the same OD distribution.

Before treating it as final, run shortest-path congestion diagnostics over all
30 files and verify every dataset passes the `10x-100x` gate.

## Current Implementation Gap

The code has the old source-congestion candidate identification:

```text
GROAlgorithm::select_candidates(...)
```

It identifies source congestion TDG nodes using a high-percentile congestion
ratio threshold and collects queries whose routes traverse those nodes.

The `gro_ablation_test` path now exposes candidate-filter switches:

```text
--candidate-filter all|source_congestion|score_top|component_balanced
```

This is useful for measuring the old source-congestion filter, but it should
not be treated as the final paper-facing candidate identification method.
Current smoke results show it retains roughly 93-95% of peak1h queries.

Recommended next implementation change:

```text
Write component-level coverage metrics to the experiment CSV.
```

Default should remain `all` for old ablation result reproducibility. The
paper-facing scalability script should eventually use `component_balanced`
if experiments confirm it preserves quality while reducing `select_sec`.

Implemented experimental filters:

```text
source_congestion     old draft-style filter
score_top             global one-pass top relief-score filter
component_balanced    component-balanced relief coverage filter
```

On `BJRealRep1-0` initial routes with `gamma=50`, the fast diagnostic reports:

```text
source_congestion_candidate_fraction = 94.93%
score_top_candidate_fraction = 2.78%
component_balanced_candidate_fraction = 26.16%
```

This gives a useful experimental contrast: `score_top` is aggressive but may
lose structural coverage; `component_balanced` keeps a larger but more balanced
candidate set for the downstream dynamic selector.

Recommended CSV additions:

```text
candidate_count
candidate_fraction
candidate_sec
component_count
covered_component_mass_fraction
covered_query_score_fraction
select_prepare_sec
select_rank_sec
select_scan_sec
select_remove_sec
```

The selection breakdown already exists internally in `src/gro.cpp` through
`log_select_summary(...)`, but it is not currently written into
`gro_ablation_test` CSV rows.

## Writing Cautions

- Do not claim TDG-impact rerouting is the main contribution unless the evidence
  becomes much stronger.
- Do not present oracle or best-param-by-iteration files as fair method results.
- Do not claim small real-world route-quality improvements are meaningful by
  themselves.
- Be explicit when a real workload is controlled or derived from T-Drive rows
  rather than an independent raw taxi sample.
- If candidate filtering reduces runtime but slightly hurts final TTT, frame it
  as a scalability tradeoff and report quality preservation honestly.
