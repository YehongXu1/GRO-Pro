# TDG Impact Normalization

This note records the current change to the GRO rerouting score.

## Problem

Raw TDG impact is recursively accumulated over downstream TDG dependencies:

```text
impact(v) = local_travel_time(v) + lambda * sum impact(child(v))
```

On dense synthetic instances this score is heavy-tailed. A small number of TDG
nodes can receive extremely large propagated impact. Using the raw value
directly in rerouting has two problems:

- it can overflow integer arithmetic if accumulated without saturation;
- even without overflow, the largest few nodes can dominate the reroute score.

## Current Fix

The raw impact computation is still used for TDG-based query selection. For
rerouting, we now precompute a normalized TDG impact vector once per iteration:

```text
clip = P99(raw_node_impact)
scale = P90(current_TDG_node_travel_time)

normalized(v) =
    log(1 + min(raw_impact(v), clip))
    / log(1 + clip)
    * scale
```

The resulting value is an integer cost in travel-time units, approximately
bounded by the current TDG edge travel-time scale.

## Why This Method

- `log1p` keeps the ordering signal but compresses extreme impact values.
- P99 clipping prevents a tiny number of outliers from setting the whole scale.
- P90 TDG node travel time makes the normalized penalty comparable to normal
  edge travel time, so `impact_weight` remains interpretable as a percent.
- The normalized vector is precomputed once, so rerouting only performs table
  lookups in the Dijkstra relaxation loop.

## Reroute Score

The rerouting score is now:

```text
score(e, t) =
    travel_time(e, t)
    + impact_weight * max_normalized_impact_on_edge_interval(e, t) / 100
```

`impact_weight = 0` still behaves like normal time-dependent Dijkstra except
for the shared reroute implementation path.

## Implementation

- `GROAlgorithm::compute_tdg_impact(...)` uses saturated arithmetic to avoid
  integer overflow.
- `GROAlgorithm::normalize_tdg_impacts_for_reroute(...)` computes the log-P99
  normalized vector.
- `GROAlgorithm::run(...)` uses raw impact for selection and normalized impact
  for rerouting.
- `GROAlgorithm::run_tdg_reroute_baseline(...)` also uses normalized impact for
  the TDG reroute baseline.
- `gro_reroute_debug_test` records `normalize_sec` so the overhead can be
  checked directly.

## Cost

Normalization costs one linear pass plus two `nth_element` percentile
computations per iteration:

```text
O(number of TDG nodes)
```

It is not inside the Dijkstra edge-relaxation loop.
