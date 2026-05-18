# Current TDG Compression Logic

This note summarizes the TDG compression logic currently implemented in
`GROAlgorithm::compress_tdg(...)` in `src/gro.cpp`.

## Goal

The original TDG can contain one node for each `(edge, entry time)` event on
every trajectory. This can be large because long trajectories and many queries
produce many edge-entry events.

The compressed TDG keeps fewer representative nodes while preserving two kinds
of dependencies:

- route dependency: the order in which a query visits selected TDG nodes;
- same-edge temporal dependency: whether flow on an earlier event can affect a
  later event on the same edge.

## Main Parameters

The compression is controlled by these options in `AlgorithmOptions`:

- `delta_compress`: length of each trajectory compression window, in seconds.
- `anchor_window`: look-back window used to judge whether an edge entry is a
  significant flow-change anchor.
- `anchor_threshold`: flow-deviation threshold, measured as a percentage of
  edge capacity.

If `delta_compress` is not positive, the code falls back to `delta_initial`,
then `delta_min`, then `1`. If `anchor_window` is not positive, it falls back to
`delta_compress`.

## Step 1: Collect Entry Times

For each edge, the code scans `result.edge_profiles[edge_id]` and stores the
times of all entry events:

```text
entry_times_by_edge[edge_id] = all event.time where event.type == true
```

These entry times are later used as fallback representative nodes when a window
does not contain a strong anchor.

## Step 2: Detect Anchor Events

For each edge, the code computes `anchor_scores`.

An entry event becomes an anchor if the current flow at that time differs enough
from the recent average flow in the previous `anchor_window`.

Conceptually, for an edge entry at time `t`:

```text
window_start = max(first_profile_time, t - anchor_window)
duration = t - window_start
window_area = integral of historical flow over [window_start, t)
current_area = current_flow_at_t * duration
deviation = abs(current_area - window_area)
threshold = anchor_threshold% * capacity * duration
```

The event is kept as an anchor if:

```text
deviation >= anchor_threshold% * capacity * duration
```

In the code this is written as:

```text
deviation * 100 >= anchor_threshold * capacity * duration
```

The first entry event on each edge is always marked as an anchor with score `0`.

## Step 3: Select Representative Nodes Per Trajectory

For each trajectory, the compressed TDG selects representative `(edge, time)`
nodes.

The code always keeps:

- the first edge-entry event of the trajectory;
- the last edge-entry event of the trajectory.

Between them, it scans the trajectory using windows of length `delta_compress`:

```text
[trajectory_start, trajectory_start + delta_compress)
[trajectory_start + delta_compress, trajectory_start + 2 * delta_compress)
...
```

For each compression window, the code searches all trajectory segments that
overlap the window.

It then chooses one representative node for that window:

1. If there is an anchor event inside the overlapping edge/time interval, choose
   the earliest such anchor.
2. Otherwise, choose the earliest raw entry event inside the overlapping
   edge/time interval.
3. If neither exists, no node is added for that window.

Every selected `(edge, time)` is inserted into the TDG through `add_node`.
Duplicate `(edge, time)` pairs share the same TDG node.

## Step 4: Add Route Dependencies

For each trajectory, the selected nodes form a compressed route sketch:

```text
route_nodes = [n0, n1, n2, ...]
```

The code adds route arcs between consecutive selected nodes:

```text
n0 -> n1 -> n2 -> ...
```

These arcs are stored in:

- `tdg.route_outgoing`
- `tdg.route_incoming`

Duplicate route arcs are avoided.

## Step 5: Build Edge Timelines

After all compressed TDG nodes are selected, the code inserts them into
per-edge timelines:

```text
tdg.edge_timelines[edge_id][time] = TimeLineEvent(...)
```

Each timeline event stores:

- `time`
- `node_id`
- `flow`
- `same_edge_parent`
- `same_edge_child`

## Step 6: Add Same-Edge Temporal Dependencies

For each edge timeline, the code scans events in time order.

Two consecutive events on the same edge are connected if the earlier vehicle
would still be on the edge when the later event happens:

```text
earlier_time + bpr_travel_time(edge, earlier_flow) > later_time
```

If true, the code sets:

```text
earlier.same_edge_child = later.node_id
later.same_edge_parent = earlier.node_id
```

This captures same-edge flow dependency in the compressed TDG.

## Step 7: Rebuild Trajectory-To-TDG Mapping

At the end, the code calls `collect_trajectory_tdg_nodes(...)` for every
trajectory.

This repopulates:

```text
trajectory.tdg_node_ids
```

Those IDs are later used by selection, batching, route removal, and impact-based
rerouting.

## Important Behavioral Notes

- Compression is trajectory-window based, not global time-bucket based.
- Each trajectory window adds at most one representative node, plus the forced
  first and last trajectory nodes.
- Anchor nodes are selected based on flow deviation from recent historical flow.
- If no anchor exists in a window, the implementation still tries to keep the
  earliest raw entry event in that window as a fallback.
- Same-edge dependency is rebuilt after compression using the current compressed
  edge timelines and BPR travel time.
- The current `run(...)` may be switched between `build_tdg(...)` and
  `compress_tdg(...)` in `src/gro.cpp`; this note describes the compression
  function itself.

## High-Level Summary

The current compression logic keeps route structure coarsely by selecting a
small number of representative nodes per trajectory, while preserving important
traffic-sensitive points through anchor detection. It is designed to reduce TDG
size while still keeping nodes around meaningful flow changes and maintaining
same-edge temporal interactions.
