## Excess-Relief TDG Selection

The original TDG selection method is still available through:

```text
GROAlgorithm::select_queries(...)
````

We added a separate selection method instead of replacing the original one:

```text
GROAlgorithm::select_queries_by_excess_relief(...)
```

The motivation is to make query selection more directly aligned with the goal of
reducing necessary congestion, instead of removing routes only because they have
high TDG impact. In particular, we do not want excessive removal, either in the
number of selected queries or in the sense of over-clearing congested roads below
their useful capacity. Roads whose flow has already dropped to capacity should no
longer provide additional selection benefit.

### Difference from the Original Selection Method

The original TDG selection method ranks candidate routes by TDG impact and uses a
removability rule as a safety constraint:

```text
do not remove a route if any traversed TDG state would drop below
(1 - gamma) times its original flow
```

This rule can prevent some over-removal, but it does not directly check whether
the next route removal is still useful for relieving congestion. It also relies
on repeated ranking and skip-based stopping behavior: if many high-ranked queries
fail the removability test, the method stops or refreshes the ranking.

The excess-relief method changes the selection criterion. It asks:

```text
How much important over-capacity congestion still remains in the working TDG?
```

Selection continues only while the working TDG still has enough remaining
weighted excess congestion to justify removing more routes.

### Node Relief Score

For each TDG node `v`, the method separates two factors:

```text
node_relief(v) = selection_impact(v) * current_excess(v)
```

where `selection_impact(v)` is a fixed importance weight derived from the
bottom-up TDG impact score:

```text
selection_impact(v) =
    log(1 + min(raw_impact(v), P99(raw_impact)))
    / log(1 + P99(raw_impact))
```

This value is computed once before the greedy selection loop. It represents how
important the TDG node is in the dependency structure.

The dynamic part is `current_excess(v)`:

```text
current_excess(v) =
    max(0, current_flow(v) / capacity(v) - 1)
```

This value depends on the current flow in the working TDG. If a TDG node is no
longer above capacity, its excess becomes zero:

```text
current_flow(v) <= capacity(v)  =>  current_excess(v) = 0
```

Therefore, even a high-impact TDG node will no longer attract more query removals
once its flow has been reduced to capacity.

### Important TDG Nodes

The method does not score every TDG node equally. It first identifies important
TDG nodes, currently using anchor-based marking:

```text
compute_anchor_scores(...)
mark_anchor_tdg_nodes(...)
```

If no anchor node is marked, the method falls back to nodes whose flow exceeds
capacity:

```text
current_flow(v) > capacity(v)
```

Only important TDG nodes can contribute to the excess-relief score.

### Route Score

For a candidate route `r`, the route score is the sum of the relief values of the
important TDG nodes covered by its trajectory:

```text
route_score(r) =
    sum node_relief(v)
    over unique important TDG nodes v covered by r
```

The score is recomputed after each selected query because the working TDG flow
changes after each hypothetical removal.

Conceptually, a route receives high score if it passes through TDG nodes that are
both:

```text
1. important in the TDG dependency structure, and
2. still above capacity in the current working TDG.
```

### Greedy Selection Process

The method maintains a working copy of the TDG. At the beginning, it computes the
initial total excess-relief mass:

```text
initial_mass =
    sum node_relief(v) over all TDG nodes in the working TDG
```

The stopping target is:

```text
target_mass =
    initial_mass * (100 - gamma) / 100
```

Thus, `gamma` controls the desired reduction ratio of weighted excess congestion.
For example, `gamma = 25` means that the selection process tries to reduce about
25% of the initial excess-relief mass. It does not mean selecting 25% of queries.

The greedy loop is:

```text
1. Compute the current excess-relief mass of the working TDG.
2. If current_mass <= target_mass, stop.
3. Scan remaining candidate queries and compute their current route_score.
4. Select the query with the largest positive route_score.
5. Remove this query's trajectory from the working TDG.
6. Repeat.
```

Equivalently:

```text
select one query
update working TDG flow
recompute current congestion status
select the next query
```

This is different from selecting a fixed percentage of queries. The number of
selected queries is determined by how much weighted excess congestion remains.

### What Is Updated During Selection

In this method, the bottom-up TDG impact scores are not recomputed after each
selected query. They are used as fixed node-importance weights.

The dynamic part is the flow in the working TDG:

```text
working_tdg.nodes[v].flow
```

After selecting a query, the method only needs to simulate the flow decrease on
the TDG nodes covered by that query. Since the method does not recompute
bottom-up impact during selection, it does not theoretically need to update the
TDG dependency structure, such as:

```text
route_outgoing / route_incoming
same_edge_parent / same_edge_child
edge_timelines
key_to_node_id
```

For this selection method, a lightweight flow-only update would be sufficient:

```text
for each TDG node v covered by the selected trajectory:
    working_flow(v) -= 1
```

The full structural TDG update is only necessary if the updated TDG will be used
to recompute bottom-up impact, rerouting dependencies, or other structure-based
operations.

### Ranking Implementation Note

The current implementation builds a ranking of all remaining candidate queries
and sorts it in each greedy round. However, the method only uses the highest-score
query in each round.

Therefore, a full sort is not necessary. The same selection result can be
obtained by scanning all remaining candidates once and keeping only the best
query:

```text
best_query = argmax route_score(q)
```

This changes each greedy round from:

```text
scan candidates + sort ranking
```

to:

```text
scan candidates and keep the maximum
```

The selection logic remains the same, but the implementation becomes cheaper.

### Summary

The excess-relief selection method treats bottom-up TDG impact as a fixed
importance weight and uses the current working TDG flow to decide whether a TDG
node still needs relief. A candidate route is useful only if it helps explain
remaining over-capacity congestion on important TDG nodes.

Compared with the original selection method, this design is more directly aligned
with the goal of avoiding excessive removal:

```text
do not keep removing routes once the important congested states have been
sufficiently reduced toward capacity
```

The method is therefore better interpreted as congestion-status-driven selection,
rather than rank-and-removability-driven selection.

```

我建议把最后一句作为你理解这个方法的核心：

> The method is congestion-status-driven: impact tells us which TDG nodes are important, while the current working flow tells us whether they still need relief.

## BPR Small-Capacity Filter

### Motivation

In the MH synthetic graph, edge capacity is not provided directly. The project
currently derives it from free-flow time:

```text
capacity = free_flow_time / 40
```

This creates a small number of very low-capacity roads. Under the BPR travel-time
function, especially with beta = 4 and no travel-time cap, these roads can create
extremely large penalties and dominate total travel time. That makes the
experiment reflect artifacts from capacity estimation rather than the behavior of
the routing methods.

### Change

The BPR evaluator now supports:

```yaml
min_bpr_capacity: 5
```

If an edge has:

```text
capacity <= min_bpr_capacity
```

then the evaluator uses the edge free-flow time directly instead of applying the
BPR penalty. Edges with missing or non-positive capacity also use free-flow time.

### Current Setting

For MH, `min_bpr_capacity = 5` filters only the lowest-capacity tail:

```text
capacity <= 5: 1514 / 36792 edges, about 4.1%
```

This is intentionally conservative. It removes the most extreme artificial
penalties while leaving normal-capacity roads governed by the BPR congestion
model.
