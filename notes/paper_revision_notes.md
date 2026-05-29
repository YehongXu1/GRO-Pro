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
theta-pruned TDG-guided excess-relief selection
```

High-level idea:

1. Build a TDG from current route trajectories.
2. Compute TDG impact once as a fixed node-importance signal.
3. Mark important TDG nodes, currently anchor-based with congestion fallback.
4. Define node relief as:

   ```text
   node_relief(v) = normalized_TDG_impact(v) * max(0, flow(v) / capacity(v) - 1)
   ```

   TDG impact propagation should share a child node's impact across its parents:

   ```text
   I(v) = tau_hat(v) + lambda * sum_{u in child(v)} I(u) / |Parent(u)|
   ```

5. Compute each query's one-pass relief score and keep the smallest prefix whose
   cumulative score covers `theta` of the total positive score mass.
6. Greedily select routes from this candidate set that cover the largest
   remaining weighted excess congestion.
7. After selecting a route, update only working TDG flow for covered nodes.
8. Stop when remaining weighted excess mass drops below the `gamma` target.

The parameters are deliberately separated:

```text
theta: candidate-pruning coverage threshold
gamma: residual weighted-excess reduction target
```

For current experiments, use `theta=80` and `gamma=50` unless a sensitivity
experiment says otherwise.

This is different from the draft's original Algorithm 4, which ranks candidates
by TDG impact, tests removability, updates the TDG structurally, and refreshes
rankings when they become stale.

Implementation note: the dynamic residual selector is now implemented with an
exact lazy-greedy priority queue. Stale route scores are safe upper bounds
because virtual removals only decrease working TDG flow, so route relief scores
are monotone non-increasing.

Rerouting note: after each reroute batch is inserted into the working TDG,
recompute TDG impact before routing the next batch. Otherwise later batches use
stale impact penalties and do not react to earlier batch insertions.
Insertion updates only existing TDG nodes covered by the rerouted trajectory; it
does not create new edge-time TDG nodes during rerouting.
The reroute search's BPR travel-time term should use the working TDG flow state
when available, not the stale pre-reroute `TrafficResult` profile.

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

Candidate pruning can be described in the Query Selection section because it is
the first step of QS. However, its main experimental role should be scalability,
not core component quality.

Current paper-facing split:

```text
Method section:
  Candidate pruning is the first step of Query Selection.

Experiment section:
  Component ablation evaluates TDG-excess selection with the full candidate
  pool, so the selection signal is isolated.

  Scalability evaluation evaluates candidate pruning as a search-space
  reduction that lowers selection cost while preserving quality.
```

Candidate pruning and TDG compression are both scalability reductions:

- Candidate identification reduces the **query search space**.
- TDG compression reduces the **TDG/state space**.

They can both appear in the method description, but their primary evaluation
should be in scalability rather than in the main component ablation.

Suggested framing:

```text
Candidate identification is not intended to improve route quality by itself.
It restricts incremental selection to structurally relevant queries so that
TDG-guided selection remains feasible at large scale.
```

This protects the paper if candidate filtering trades a small amount of quality
for substantial runtime savings.

## Paper-Facing Query Selection Structure

Use the current draft's main structure, but rewrite Section 6 around the new
TDG-excess selection:

```text
6 Query Selection
  6.1 Excess-relief signal
  6.2 Score-based candidate pruning
  6.3 Residual excess-relief selection
  6.4 Complexity analysis
```

The code name `score_top` should not appear as the paper method name. Prefer:

```text
Score-based candidate pruning
Relief-based candidate pruning
Excess-relief candidate pruning
```

The pruning rule is simple by design:

```text
1. Compute one-pass route excess-relief score for each query.
2. Sort queries by this score.
3. Keep the smallest prefix whose cumulative score covers theta of the total
   one-pass query relief mass.
4. Run residual excess-relief selection only over this candidate set.
```

This should be framed as a lightweight acceleration step derived from the same
excess-relief objective, not as the main novelty. The main QS contribution is
the combination of:

```text
static theta-pruned relief candidates + dynamic gamma-driven residual selection
```

The dynamic selector remains responsible for handling interaction among query
removals by updating residual TDG flow after each virtual removal.

Implementation names:

```text
paper theta      -> code/config candidate_theta, CLI --candidate-theta
paper gamma      -> code/config gamma, CLI --tdg-gammas
score pruning    -> implementation filter score_top
```

Do not describe `theta` as a query-count fraction. It is a cumulative relief
coverage threshold. Do not describe `gamma` as a candidate-pruning threshold. It
is the residual weighted-excess target for dynamic selection.

Implementation and complexity note:

```text
Naive dynamic selection:
  repeatedly rescore every remaining candidate.

Current implementation:
  exact lazy-greedy heap using stale route scores as upper bounds.
```

This optimization should be described as an implementation acceleration of the
same greedy objective. It should not be framed as a new approximation unless
future code intentionally changes the selection rule.

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

## Component-Balanced Candidate Identification Diagnostic

This section records a diagnostic design that we tested while exploring whether
structural diversity across TDG components should drive candidate pruning. It
is **not** the current paper-facing candidate pruning method.

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
  TDG, current trajectories, normalized TDG impacts, theta

Output:
  Candidate query set C

1. Compute active relief nodes and their weak congestion components.
2. For every query q, scan its TDG nodes once and accumulate A(q, g).
3. For each congestion component g independently:
     a. Sort queries touching g by A(q, g), descending.
     b. Add queries to C until cumulative A(q, g) >= theta * M(g).
4. Return the union C across all components.
5. Run the existing iterative tdg_excess selection only on C.
```

Parameter choice:

```text
Use theta for pruning and gamma for residual selection.
```

The pruning threshold should be independent from the residual selection target.
If `theta=80`, candidate identification keeps enough candidates to cover 80% of
the one-pass relief mass. The later selector still decides which candidates to
actually reroute using `gamma`.

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

This was a reasonable intuition, but experiments show that pure component
coverage loses too much high-value query signal. Keep this as diagnostic
context, not as the recommended paper algorithm.

## Component-Marginal Budget Diagnostic

We also tested a budgeted component-marginal variant:

```text
component_marginal_samek: use the same candidate count as score_top, but rank
                          by marginal component coverage.
component_marginal_budget5: rank by marginal component coverage, keep 5%.
component_marginal_budget3: rank by marginal component coverage, keep 3%.
```

The no-budget `component_marginal` variant was too weak as a filter on
`BJRealRep1-0`: it kept 20.16% of queries, only a small improvement over
`component_balanced` at 26.16%. Treat this level of candidate reduction as not
meaningful unless it produces a much stronger quality or runtime result.

First-five-iteration diagnostic on
`data/BJ_Real_query_sets_scalability_inner_progressive_peak1h`, `Rep=1`,
seeds `0..4`, config `config_bj_capacity2_cap10e8_iter5.yaml`:

```text
method                         avg_best_TTT_reduction   avg_candidate   avg_time
score_top                      94.42%                   5.90%           99.5s
component_marginal_samek       24.99%                   2.19%           65.1s
component_marginal_budget5     57.99%                   5.00%           79.1s
component_balanced             89.83%                   24.81%          174.9s
```

Interpretation:

- Current component-marginal ranking does not reach the `score_top` effect with
  a small candidate budget.
- `budget5` is still far below `score_top`, so `budget3` is not worth running
  as a paper-facing candidate.
- `component_balanced` gets closer only by keeping roughly a quarter of all
  queries, which weakens the scalability story.
- This suggests that component coverage alone is missing the high-delay signal
  that makes `score_top` strong. The next candidate-identification design
  should not be pure component coverage; it needs to combine component coverage
  with a direct query-delay or excess-cost term, or it should remain an
  internal diagnostic rather than a paper contribution.

## Suggested Paper Revision Structure

Do not overhaul the current draft structure. The existing layout is mostly
usable:

```text
1. Introduction
2. Related Works
3. Preliminaries
4. GRO Framework
5. Traffic Impact Estimation
6. Query Selection
7. Query Rerouting
8. Dependency-Preserving TDG Compression / Scalability
```

Needed revisions:

```text
Section 5.3:
  Shorten TDG insert/remove. Current selection uses virtual flow decrement,
  not structural RemoveRoute. Rerouting insert updates existing TDG node flows
  and locally repairs same-edge dependencies.

Section 6:
  Replace old two-level incremental selection with score-based pruning plus
  residual excess-relief selection.

Section 7:
  Mostly keep the current QR structure. Update only the parts that imply impact
  is recomputed before every batch or that insertion is full structural TDG
  maintenance.

Section 8:
  Discuss scalability components: score-based candidate pruning and TDG
  compression. Candidate pruning can be defined in Section 6 but evaluated
  mainly here.
```

This keeps the draft stable while aligning the claims with the current method.

## Experiment Implications

### Component Ablation

Use component ablation mainly to show quality and isolate the TDG-excess
selection signal:

- `baseline_random_normal`
- `baseline_delayed_normal`
- `tdg_excess_normal`
- `tdg_excess_full`

For this figure, use `--candidate-filter all` unless there is a strong reason
not to. This prevents reviewer confusion about whether TDG-excess is better
because of the selection signal or because candidate pruning filtered the query
pool.

Candidate pruning does not need to be in the main 3x3 quality figure. If
included, use a compact auxiliary table only:

```text
All candidates vs score-based pruned candidates
```

Metrics:

- candidate fraction
- selected fraction
- select time
- final TTT
- quality loss or preservation

### Scalability

Scalability should evaluate the full practical method and its reductions:

```text
tdg_excess selection + TDG reroute
score-based pruning + tdg_excess selection + TDG reroute
score-based pruning + tdg_excess selection + TDG reroute + TDG compression
```

The key experimental question is not baseline superiority. It is:

```text
Does score-based candidate pruning reduce selection cost while preserving the
quality of TDG-excess selection?
Does TDG compression further reduce TDG/state cost while preserving quality?
```

Include a limited comparison against all-query selection:

- all-query selection for sizes it can finish, e.g. 10k/20k;
- score-pruned selection for the full curve;
- score-pruned + compressed TDG for the full curve;
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

Current first-five-iteration peak1h scalability evidence is strong for
`score_top`/score-based pruning:

```text
10k: full 398s, score-based 99s,   best TTT reduction 94.27% vs 94.42%
20k: full 1535s, score-based 214s, best TTT reduction 97.45% vs 97.29%
30k: full 3452s, score-based 381s, best TTT reduction 98.94% vs 98.86%
50k: full 8899s, score-based 736s, best TTT reduction 98.31% vs 98.97%
```

Candidate fraction drops to roughly `3%-6%` for these sizes, and selection time
becomes much smaller. This supports evaluating candidate pruning in the
scalability section rather than treating it as a quality ablation.

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
--candidate-filter all|source_congestion|score_top|global_score|component_balanced|component_marginal|...
```

For paper wording, map implementation names to paper names:

```text
score_top/global_score  -> score-based candidate pruning
source_congestion       -> old draft-style filter, diagnostic only
component_balanced      -> component-coverage diagnostic
component_marginal*     -> component-coverage diagnostic
```

The old source-congestion filter should not be treated as the final
paper-facing candidate identification method. Current smoke results show it
retains roughly 93-95% of peak1h queries.

Recommended next implementation / analysis changes:

```text
1. Keep `--candidate-filter all` for component ablation reproducibility.
2. Use `--candidate-filter score_top --candidate-theta 80` for
   candidate-pruned scalability.
3. Add / run the combined score-pruned + compressed-TDG scalability experiment.
4. If needed, write candidate_count, candidate_fraction, and candidate_sec into
   all scalability summaries.
```

Default should remain `all` for old ablation result reproducibility. The
paper-facing scalability script should use `score_top` / score-based pruning
when evaluating practical scalability.

Implemented experimental filters:

```text
source_congestion     old draft-style filter
score_top             global one-pass top relief-score filter
component_balanced    component-balanced relief coverage filter
component_marginal*   component-coverage / marginal diagnostic filters
```

On `BJRealRep1-0` initial routes with the earlier coupled pruning threshold
`gamma=50`, the fast diagnostic reports:

```text
source_congestion_candidate_fraction = 94.93%
score_top_candidate_fraction = 2.78%
component_balanced_candidate_fraction = 26.16%
```

This contrast helped rule out pure component-coverage pruning as the current
paper-facing direction. The practical method should use `score_top`, renamed in
the paper as score-based or relief-based candidate pruning.

Recommended CSV additions:

```text
candidate_theta        already written by gro_ablation_test
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

The candidate-count fields and `candidate_theta` are now written by
`gro_ablation_test`. The detailed selection breakdown already exists internally
in `src/gro.cpp` through `log_select_summary(...)`, but it is not currently
written into `gro_ablation_test` CSV rows.

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
