# New Chat Handoff: Improving Rerouting

This note briefs a new chat whose goal is to **improve the rerouting stage** of GRO.
Read `notes/new_chat_handoff_vldb.md` first for the overall project; this note is
the specific problem and situation for rerouting.

## Project In One Paragraph

GRO is a VLDB-style method for global route optimization over many simultaneous
trip queries on a road network. Per iteration it: evaluates congestion-dependent
total travel time (TTT), builds a Traffic Dependency Graph (TDG) from route
trajectories, selects which queries to reroute (`tdg_excess` excess-relief
selection), reroutes them, and repeats; a compressed TDG is the scalability
mechanism. Core code: `src/gro.cpp`, `include/gro.hpp`. Real experiment driver:
`tests/gro_ablation_test.cpp` (not `GROAlgorithm::run`). The main paper claim is
TDG-guided **selection**; rerouting is a secondary component.

## Why This Chat Exists

A recent commit (`d8b1b22`, 2026-05-29) added three things to selection/reroute:
lazy-greedy selection, a separate `candidate_theta`, and per-batch impact
recompute during rerouting (plus a `reroute_congestion_gate` option). The gate is
now enabled by default at 50% capacity so TDG-impact reroute does not strongly
penalize high-impact but locally low-flow nodes. The
**lazy-greedy update made query selection ~84-371x faster** on BJ peak1h (it is an
exact CELF speedup; same selected set, only faster). 

Consequence: **selection is no longer the bottleneck. Runtime is now dominated by
rerouting and the traffic evaluator.** So rerouting is the thing to improve, both
for speed and for stability (see below).

## Reroute Cost Structure (the key mental model)

`reroute_queries` (`src/gro.cpp:3037`) processes selected queries in batches. Per
batch it: (1) reroutes each query in parallel via `reroute_query`, (2) inserts each
new trajectory into the working TDG one by one, (3) after each batch (except last)
recomputes TDG impact (`src/gro.cpp:3082`). Three cost components:

1. **`reroute_query` (`src/gro.cpp:2911`) — the dominant cost.** A per-query
   time-dependent Dijkstra over the **road graph**, with no goal direction (heap
   keyed only by cumulative score = travel_time + impact penalty; settles every
   node cheaper than the destination). Crucially it **does not shrink with TDG
   compression** — the road graph is not compressed; only the per-edge timeline
   scans get cheaper. This is why compression caps reroute speedup at ~2x.

2. **`insert_trajectory_into_tdg` (`src/gro.cpp:1165`) — wasteful, minor now,
   grows with N.** Line `src/gro.cpp:1264` allocates and zero-inits
   `std::vector<char> touched_seen(tdg.nodes.size())` **on every call**, but it
   only dedups the trajectory's own ~L nodes. Cost is O(rerouted x total_TDG_nodes)
   per iteration ~ O(N^2). At 50k it is a small share (proven by the compression
   ceiling argument below); at 100k+ it can overtake. Cheap fix: dedup with
   sort+unique on `trajectory.tdg_node_ids`, or a reused epoch-stamped scratch.

3. **Inter-batch impact recompute** — `compute_tdg_impact` after each batch,
   (batch_count - 1) times per iteration; batch_count is currently 2-11. Shrinks
   ~27x with compression. Magnitude unknown without the timing breakdown.

### Preliminary inference (to confirm, NOT to assume)

Indirect evidence *suggests* `reroute_query` may be the largest component, but this
is not measured -- do not treat it as established. The hint: compression shrinks
the TDG ~27x, yet at 50k compressed reroute is only ~2.4x faster than uncompressed,
and most of that is just rerouting fewer queries (11.8% vs 21.8% selected) -- the
per-query reroute speedup is only ~1.3x. That is *consistent* with a graph-bound
Dijkstra that barely compresses, but the timing breakdown below is required before
concluding anything.

## What We Have NOT Analyzed Yet

We have not measured where reroute time actually goes. **Do the analysis first; do
not jump to a fix or a redesign.** Open questions for this chat to answer:

- Which of the three reroute components actually dominates, and how does the split
  change with query size? (Needs the timing breakdown below.)
- Why does uncompressed `full` become unstable at 50k (see next section)?
- How does the dominant cost scale with N, and what part (if any) is reducible
  without trading route quality?

Factual observations about the knobs (from reading the code -- context, not a
recommendation):

- `gamma` (`include/gro.hpp:52`) sets how many queries are rerouted: fewer = less
  reroute work but less TTT reduction.
- `conflict_threshold` (`include/gro.hpp:65`, run at 5000) controls batch count and
  thus how many inter-batch recomputes happen; it is near-unlimited now.
- `impact_weight` / `reroute_congestion_gate` (`include/gro.hpp:54,60`) change which
  route is chosen; the gate defaults to 50% capacity and scales the TDG-impact
  penalty from zero at that load to full strength at capacity. The per-edge
  timeline scan in `reroute_query` runs regardless of these values.

## Open Problem: uncompressed 50k instability

New-code uncompressed `full` at 50k peak1h **oscillates and diverges**: mean final
TTT reduction 63% with cross-seed std ~45pp, and one seed went to negative
reduction (worse than shortest path) and terminated early. Compressed is stable
(~93%, std ~6). Per-iteration traces show wild swings (e.g. 96.5 -> 99.2 -> 44.1 ->
99.5 -> 97.5). Leading hypothesis: `conflict_threshold=5000` allows too many
conflicting queries to reroute *blindly* against one impact snapshot in too few
batches -> overcorrection. This is a candidate bug/instability to investigate as
part of improving rerouting (it also makes `full` an unreliable quality baseline).
Test idea: lower `conflict_threshold` (more, smaller batches, fresher impact) and
see if 50k stabilizes -- at the cost of more recompute time.

## FIRST ACTION: get the reroute timing breakdown

The scalability runs had `enable_timing_log` OFF, so the split of reroute into
`reroute_query_sec` vs `insert_trajectory_sec` vs `recompute_impact_sec` is unknown.
Confirm it before optimizing. Re-run ONE config (50k, one seed) with timing on and
read the `REROUTE,...` stderr line. Example (set `enable_timing_log: true` in the
config or pass the timing flag the runner exposes):

```bash
./gro_ablation_test config/config_bj_capacity2_cap10e8_iter5.yaml \
  --query-file data/BJ_Real_query_sets_scalability_inner_progressive_peak1h/BJRealRep5-0.txt \
  --selection-methods tdg_excess --reroute-methods tdg \
  --candidate-filter all --tdg-gammas 50 --impact-weights 15 \
  --conflict-threshold 5000 --random-seed 0 \
  2>&1 | grep -E '^REROUTE|^BATCH'
```

Then attribute reroute time across the three components, ideally across multiple
sizes (e.g. 10k/30k/50k) to see how the split scales. Only after that, discuss
directions. Do not pre-commit to a fix or a redesign.

## Hard Rules (carry these into the new chat)

- **NEVER run experiments locally**, even when asked or when the user says they want
  to "try" one. Draft code/scripts and hand over the exact command; the USER runs
  it. Building the binary to check it compiles is fine; running it to produce
  results is not. (The user once had to kill an experiment the AI started.)
- **Engineering speedups are not paper contributions.** A*/ALT, the lazy-greedy
  selection, and the insert fix are implementation accelerations -- present them as
  such, never as novelty. Do not overclaim.
- **The paper-facing scalability story is MEMORY** (compressed TDG ~27x smaller),
  not wall-clock. Reroute speedups are constant-factor and do not change that.
- Keep diagnostic results separate from fair method results.

## Key Files And Lines

```text
src/gro.cpp:2911   reroute_query              per-query time-dependent Dijkstra
src/gro.cpp:3037   reroute_queries            batch loop, insert, per-batch recompute
src/gro.cpp:3099   baseline_reroute_queries   same per-query Dijkstra structure
src/gro.cpp:1165   insert_trajectory_into_tdg
src/gro.cpp:1264   touched_seen(tdg.nodes.size())   per-call O(total_nodes) allocation
src/gro.cpp:950    normalize_tdg_impacts_for_reroute (uses reroute_congestion_gate)
src/core.cpp:523   reverse_shortest_distances (free-flow reverse SP, used by core)
src/core.cpp:565   bpr_travel_time            BPR cost function
include/gro.hpp:52,54,60,65,66  gamma / impact_weight / reroute_congestion_gate /
                                conflict_threshold / delta_compress
config/config_bj_capacity2_cap10e8_iter5.yaml   (5-iteration scalability config)
tests/gro_ablation_test.cpp     (CLI driver: --reroute-methods normal|tdg, etc.)
```

Data and current results:

```text
data/BJ_Real_query_sets_scalability_inner_progressive_peak1h   (peak1h workload)
python/results/experiments/exp3_compression_scalability/bj_peak1h/
    tmp_..._full_conflict5000...            new-code uncompressed (10k-50k)
    tmp_..._full_compressed_conflict5000... new-code compressed (10k-70k)
python/results/experiments/exp3_compression_scalability/mh_peak1h/   (MH version)
```

## New Chat Prompt Template

```text
We are in /Users/xyh/Desktop/GRO-Pro on a VLDB paper about GRO (TDG-guided
iterative route optimization). Goal of THIS chat: improve the rerouting stage --
but ANALYSIS FIRST, not a fix. Read notes/new_chat_handoff_rerouting.md (and
notes/new_chat_handoff_vldb.md for project context). Do not change code or run
anything yet, and do not propose a redesign. Summarize: why rerouting is now the
bottleneck after the lazy-greedy change, the three reroute cost components from
the code, why TDG compression alone is bounded as a reroute-speed lever, and the
50k instability open problem. Explicitly note that we have NOT measured the
reroute timing split. Then propose a small analysis plan to find out where reroute
time actually goes (the timing breakdown across query sizes is the first step) and
to characterize the 50k instability. Do NOT recommend a specific algorithmic
direction (A*, landmarks, redesigns, etc.) until the analysis is done. You must
NEVER run experiments locally -- draft scripts/commands; I run them. Engineering
speedups are not paper contributions.
```
