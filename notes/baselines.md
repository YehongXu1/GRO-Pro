# Baseline Algorithms

This note records the baseline methods currently implemented in the project and the role each one can play in the paper. The goal is to make the experimental comparison easy to justify: every method receives the same road network and query set, outputs `std::vector<gro::Route>`, and can be evaluated by the same traffic simulator in `evaluate_traffic(...)`.

## Common Evaluation Protocol

All baselines should be compared under the same graph, OD queries, departure times, BPR travel-time model, and final evaluation pipeline. For route-quality metrics, we evaluate the returned routes with the project traffic evaluator instead of relying on each baseline's internal objective. This keeps total travel time, congestion, and route feasibility comparable.

For efficiency, report the phases separately when applicable:

- **Preprocessing/index construction:** relevant for FAHL.
- **Route/query computation:** time spent producing routes for all queries.
- **Traffic evaluation:** common post-processing cost, usually separated from algorithm time.

For methods using flow information, the flow source should be stated explicitly. GRO, GOR, SOR, and the iterative baseline use route-induced flow during simulation/rerouting. FAHL uses a fixed flow distribution derived from a reference set of routes or trajectories, matching its role as an individual-level flow-aware indexed routing baseline.

## Shortest Path Initialization

**Implementation:** `GROAlgorithm::compute_initial_routes(...)` and shared `shortest_path(...)`.

This is the simplest baseline and also the initialization used by GRO-style methods. Each query is routed independently using free-flow edge travel time. It does not consider congestion, interactions among trips, flow distribution, or route diversity.

**Purpose in the paper:** It provides the lower-complexity reference point. Any congestion-aware method should improve over this baseline under congested demand because shortest paths often concentrate many trips on the same edges.

## Iterative Rerouting Without TDG

**Implementation:** `GROAlgorithm::run_baseline(...)` and `baseline_reroute_queries(...)`.
**Experiment name:** `baseline`.

This is an ablation of GRO. It keeps the iterative rerouting framework but removes the Traffic Dependency Graph, impact propagation, sophisticated query selection, and dependency-aware batching. At each iteration it evaluates the current route set, randomly selects a fixed fraction of queries, and reroutes those queries using current edge flows and the BPR travel-time function.

The random query selection is reproducible through `baseline_random_seed`.
Iteration `i` uses `baseline_random_seed + i`, so the same config produces the
same baseline curve while still selecting a different query subset in each
iteration.

**What it controls for:** This baseline separates the benefit of simply doing repeated congestion-aware rerouting from the benefit of GRO's TDG-based query selection and rerouting coordination.

**Suggested paper framing:** "To isolate the contribution of our dependency-aware selection and batching strategy, we compare with an iterative rerouting baseline that uses the same congestion model but randomly selects trips and reroutes them without TDG guidance."

## TDG Ablations

These experiment names isolate the two TDG components:

```text
baseline                 random selection + normal TD-Dijkstra
tdg_selection_baseline   TDG selection + normal TD-Dijkstra
tdg_reroute_baseline     random selection + TDG reroute/batching
tdg                      TDG selection + TDG reroute/batching
```

## SVP

**Paper:** Zihan Luo et al., "Diversified Top-k Route Planning in Road Network," PVLDB 2022.

**Implementation:** `include/svp.hpp`, `src/svp.cpp`, `compute_svp_baseline_routes(...)`.

SVP is used as a diversified route-planning baseline. Our implementation generates alternative routes for each OD pair by combining forward and backward shortest-path trees through candidate via vertices, then filters candidates by an overlap threshold `theta`. For repeated identical OD queries, the baseline can distribute queries across accepted alternatives in a round-robin manner.

**What it represents:** Diversity without explicit congestion feedback. SVP can reduce route concentration by providing different routes, but it does not directly model time-dependent flow interactions or BPR travel-time feedback during route construction.

**Fairness note:** The original paper targets diversified top-k route planning. In this project, SVP is adapted to produce one route per query and to fit the common `Route`/traffic-evaluation interface. This makes it a reasonable diversity-based baseline rather than a full congestion-optimization method.

## GOR Greedy

**Paper:** "Towards Alleviating Traffic Congestion: Optimal Route Planning for Massive-Scale Trips," IJCAI 2021.

**Implementation:** `include/gor.hpp`, `src/gor.cpp`, `compute_gor_greedy_routes(...)`.

GOR Greedy is an online-style congestion-aware routing baseline. Queries are processed through a global event queue. At each decision point, the algorithm chooses the outgoing edge that minimizes a local score consisting of accumulated travel time, current BPR travel time on the candidate edge, and a remaining shortest-path estimate to the destination.

**What it represents:** A lightweight greedy congestion-aware strategy. It reacts to route-induced flow while constructing routes, but it does not perform global iterative reoptimization or dependency-aware query selection.

**Fairness note:** It uses the same BPR travel-time function as our evaluation. This makes the comparison meaningful, while the greedy nature keeps it clearly distinct from GRO's global iterative design.

## SOR

**Paper:** "Congestion-Mitigating Spatiotemporal Routing in Road Networks."

**Implementation:** `include/sor.hpp`, `src/sor.cpp`, `compute_sor_routes(...)`.

SOR is implemented as a spatiotemporal congestion-mitigation baseline. It maintains edge-time volumes and routes each query by minimizing a spatiotemporal edge metric under a detour constraint. The metric penalizes already loaded edge-time cells, so later routes are encouraged to avoid congested parts of the network.

**What it represents:** A time-aware route assignment baseline with route-induced flow. Compared with GRO, it does not use a TDG to identify which existing trips should be rerouted, and it does not use our impact-based selection and batching strategy.

**Use in paper:** This is a strong congestion-mitigation baseline if we want a method closer to our setting than pure individual routing. If space is limited, it can be grouped with GOR as a congestion-aware assignment baseline.

## FAHL

**Paper:** "An Efficient Labeling Index for Flow-Aware Shortest Path Querying in Road Networks."

**Implementation:** `include/fahl.hpp`, `src/fahl.cpp`, `tests/fahl_test.cpp`.

FAHL is used as an indexed individual-level flow-aware routing baseline. It assumes a fixed flow distribution and builds a static H2H-style labeling index. In our implementation, the flow profile can be derived from reference routes or trajectories. The index stores directed forward and backward labels, using effectively infinite costs for impossible directions, so one-way connectivity is respected.

The query objective uses a scalar flow-aware edge score:

```text
score(e) = alpha * normalized_distance(e)
         + (1 - alpha) * normalized_flow(e)
```

The query then returns a concrete `Route`, allowing the selected path to be evaluated by the same traffic simulator as all other methods.

**What it represents:** Fast individual flow-aware route querying under a fixed external or precomputed flow distribution. It does not update the flow distribution online and does not model the feedback loop between its own routed queries and future flow.

**Fairness note:** Because FAHL is an index-based method, efficiency comparison should separate index construction time from query time. For route-quality comparison, its fixed flow distribution should be derived from a clearly specified reference route set and held fixed for all FAHL queries.

## Comparison Summary

| Method | Main Role | Uses Flow | Route-Induced Feedback | Index | Query Selection |
| --- | --- | --- | --- | --- | --- |
| Shortest Path | Basic independent routing | No | No | No | No |
| Iterative without TDG | GRO ablation | Yes | Yes | No | Random selection |
| SVP | Diversity baseline | No | Indirect only after evaluation | No | No |
| GOR Greedy | Greedy congestion-aware routing | Yes | Yes | No | Event/order driven |
| SOR | Spatiotemporal congestion mitigation | Yes | Yes | No | Sequential assignment |
| FAHL | Indexed flow-aware individual routing | Fixed flow | No | Yes | No |
| GRO | Proposed method | Yes | Yes | No | TDG impact selection and batching |

## Suggested Justification Paragraph

We compare GRO with several complementary baselines. Shortest-path routing measures the effect of ignoring congestion. The iterative rerouting baseline uses the same congestion model as GRO but removes the TDG, impact-based query selection, and dependency-aware batching, isolating the contribution of our main algorithmic components. SVP represents diversity-driven route planning, which can reduce overlap but does not explicitly optimize route-induced congestion. GOR Greedy and SOR are congestion-aware assignment baselines that react to current or accumulated edge-time volumes. FAHL represents an orthogonal indexed individual-level approach: it supports efficient flow-aware shortest-path queries under a fixed flow distribution, but it does not update routes through a system-level feedback loop. All methods are evaluated through the same traffic simulator for fair route-quality comparison.

## Current Code Mapping

```text
include/gro.hpp, src/gro.cpp      GRO and iterative no-TDG baseline
include/svp.hpp, src/svp.cpp      SVP baseline
include/gor.hpp, src/gor.cpp      GOR greedy baseline
include/sor.hpp, src/sor.cpp      SOR baseline
include/fahl.hpp, src/fahl.cpp    FAHL index baseline
tests/*_test.cpp                  Separate tests for each baseline
```
