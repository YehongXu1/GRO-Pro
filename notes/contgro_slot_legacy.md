# ContGRO-Style Slot TDG Diagnostic

## Motivation

The old `ContGRO` result in `compareMH.png` is not only different because of
data parsing or BPR settings. Its TDG method is structurally different from the
current GRO implementation.

The old method couples four ideas:

1. slot-based TDG nodes `(edge, slot)`,
2. route occupancy over the slot range of each traversal,
3. source-congestion candidate discovery,
4. average route impact during query ranking.

The current GRO implementation mostly uses exact event-time TDG nodes and newer
anchor/excess-based selection logic. Therefore, comparing the old figure against
current `tdg_anchor` or `tdg_excess` is not an apples-to-apples comparison.

## New Files

The ContGRO-style diagnostic is implemented separately:

```text
include/gro_slot_legacy.hpp
src/gro_slot_legacy.cpp
tests/gro_slot_legacy_ablation_test.cpp
```

It builds as:

```bash
make gro_slot_legacy_ablation_test
```

This keeps the diagnostic method out of the main GRO algorithm files.

## Implemented Logic

### Slot TDG Construction

Each TDG node is keyed by:

```text
(edge_id, time / slot_width)
```

For each edge-slot node, the stored flow is the maximum observed flow among
traffic events in that slot. The representative time is the event time where
that maximum flow occurs.

The TDG includes:

- same-edge dependency arcs between consecutive active slots when travel time
  overlaps the next slot event,
- route-transition arcs between consecutive edges in a query trajectory.

### Candidate Selection

The diagnostic follows the old source-congestion idea:

1. compute flow/capacity ratios for TDG nodes,
2. use percentile `tau` to find target congested nodes,
3. keep only source congestions, meaning target nodes without a target
   congested ancestor,
4. collect candidate queries from active queries on those source nodes.

### Query Ranking

For each candidate query, the score is the average TDG impact of the slot nodes
visited at route entry times:

```text
score(q) = average impact over q's TDG entry nodes
```

This is intentionally different from the current sum-based scoring. The average
score avoids automatically favoring longer routes.

### Removability And TDG Update

A query is removable only if removing it from all covered route slots does not
reduce any covered TDG node below:

```text
(1 - gamma) * original_flow
```

After a query is selected, the working TDG flow is decreased. Zero-flow slot
nodes are removed and same-edge dependency arcs are repaired.

### Legacy Slot Reroute

The executable also includes a diagnostic `legacy_slot_tdg` reroute mode. Its
edge cost is:

```text
current BPR travel time + slot TDG impact
```

This captures the old ContGRO reroute idea, but it does not yet reproduce the
old conflict batching and per-batch TDG insertion exactly. Therefore:

- `legacy_slot_normal` is the cleaner test for the old selection logic,
- `legacy_slot_tdg` is a useful diagnostic for the old reroute idea, but should
  not be treated as a fully exact ContGRO reproduction yet.

## Methods In The Executable

```text
legacy_slot_normal
```

ContGRO-style slot selection + normal time-dependent Dijkstra reroute.

```text
legacy_slot_tdg
```

ContGRO-style slot selection + slot-impact reroute.

```text
random_normal
```

Random fixed-fraction selection + normal time-dependent Dijkstra reroute. This
is included as a local reference inside the same executable.

## Important Parameters

```text
slot_width
```

Slot granularity. `slot_width = 1` is closest to the old ContGRO code.

```text
tau
```

Percentile used to find target congested TDG nodes.

```text
gamma
```

Maximum allowed removal ratio. `gamma = 50` means the working TDG should not
reduce a covered node below 50% of its original flow.

```text
lambda
```

Backward impact attenuation. The old ContGRO code used `0.8`, so this executable
defaults to `lambda = 80`.

```text
capacity_ignore
```

Edges with capacity at or below this value are ignored when identifying source
congestion. By default the executable uses the traffic config's
`min_bpr_capacity`.

## Smoke Command

```bash
./gro_slot_legacy_ablation_test config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --datasets Hop10Rep1-0 \
  --output python/results/mh/gro_slot_legacy_smoke.csv \
  --methods legacy_slot_normal,legacy_slot_tdg,random_normal \
  --slot-width 1 \
  --tau 90 \
  --gamma 50 \
  --lambda 80 \
  --random-fraction 10 \
  --random-seed 0
```

## Full MH Command

```bash
nohup ./gro_slot_legacy_ablation_test config/config.yaml \
  --query-dir data/MH_Synthetic_query_sets \
  --output python/results/mh/gro_slot_legacy_all.csv \
  --methods legacy_slot_normal,legacy_slot_tdg,random_normal \
  --slot-width 1 \
  --tau 90 \
  --gamma 50 \
  --lambda 80 \
  --random-fraction 10 \
  --random-seed 0 \
  > logs/gro_slot_legacy_all.log 2>&1 &
```

## Full BJ Command

```bash
nohup ./gro_slot_legacy_ablation_test config/config_bj.yaml \
  --query-dir data/BJ_Synthetic_query_sets \
  --output python/results/bj/gro_slot_legacy_all.csv \
  --methods legacy_slot_normal,legacy_slot_tdg,random_normal \
  --slot-width 1 \
  --tau 90 \
  --gamma 50 \
  --lambda 80 \
  --random-fraction 10 \
  --random-seed 0 \
  > logs/bj_slot_legacy_all.log 2>&1 &
```

## Output Columns

The CSV records one row per dataset, method, and iteration. The most important
columns are:

```text
selected_count
candidate_count
source_node_count
tdg_node_count
total_before
total_after
reduction
slot_tdg_sec
select_sec
reroute_sec
evaluate_after_sec
```

This is intended to answer whether the old coupled slot-based selection has the
same qualitative advantage as the original `compareMH.png` result.
