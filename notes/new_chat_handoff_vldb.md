# New Chat Handoff For GRO VLDB Paper

This note is the first file a new chat should read before touching code or
analyzing results.

## Project Goal

We are developing and evaluating GRO for a VLDB-style paper. The core problem
is global route optimization for many simultaneous trip queries in a road
network. The paper goal is to show that a traffic dependency graph (TDG) can
identify which existing routes should be rerouted, and can help route
replacement, more effectively than simple iterative baselines and prior
one-shot route-planning baselines.

The intended paper story is:

1. Build routes for many OD queries.
2. Evaluate congestion-dependent total travel time.
3. Build a TDG from the current route trajectories.
4. Select a subset of queries whose routes are responsible for congestion.
5. Reroute selected queries.
6. Iterate until the method converges or no useful queries are selected.
7. Compress TDG for scalability on large workloads.

## Current Main Claim

The main claim should be about TDG-guided iterative optimization:

- TDG-guided selection can select fewer queries than fixed-fraction baselines
  but reduce total travel time faster.
- TDG-aware rerouting is a secondary component; current evidence is weaker than
  selection, so do not overclaim it until experiments support it.
- TDG compression is the scalability component and should be evaluated mainly
  on larger real-world workloads.

## Important Experiment Principle

Do not mix method roles:

- `baseline_random_normal`, `baseline_delayed_normal`, and GRO variants are
  iterative and can be plotted over iterations.
- SVP, GOR, SOR, and FAHL are paper baselines but are not naturally iterative
  over the same rerouting loop. Do not force them into the iterative-ablation
  figure.
- Oracle/best-param-by-iteration files are diagnostic only. They answer
  whether the TDG family contains a good choice, not whether the final method is
  deployable or fair.

## Current Method Names

Use these names consistently:

| Name | Selection | Reroute | Role |
| --- | --- | --- | --- |
| `baseline_random_normal` | fixed random fraction | normal TD-Dijkstra | simple iterative baseline |
| `baseline_delayed_normal` | most-delayed fixed fraction | normal TD-Dijkstra | strongest simple iterative baseline |
| `baseline_delayed_tdg_reroute` | most-delayed fixed fraction | TDG-impact reroute | reroute ablation only |
| `tdg_excess_normal` | TDG-guided selection | normal TD-Dijkstra | main selection ablation |
| `tdg_excess_full` | TDG-guided selection | TDG-impact reroute | complete GRO candidate |
| `tdg_anchor_*` | older TDG anchor selection | normal/full | diagnostic, probably not paper-facing |
| `tdg_bpr_relief_*` | relief-style TDG selection | normal/full | diagnostic/alternative selection |
| `SVP` | diversified routing baseline | one-shot | prior-paper baseline |
| `GOR` | greedy congestion-aware routing | one-shot | prior-paper baseline |
| `SOR` | spatiotemporal routing baseline | one-shot | prior-paper baseline |
| `FAHL` | flow-aware H2H-style index | one-shot query processing | prior-paper baseline |

For paper writing, prefer one public name for our selection method, e.g.
`TDG-guided`, unless a new experiment clearly justifies multiple variants.

## Key Files

Core C++:

```text
include/core.hpp, src/core.cpp
include/gro.hpp, src/gro.cpp
src/gro_baseline.cpp
include/gro_slot_legacy.hpp, src/gro_slot_legacy.cpp
include/svp.hpp, src/svp.cpp
include/gor.hpp, src/gor.cpp
include/sor.hpp, src/sor.cpp
include/fahl.hpp, src/fahl.cpp
```

Test/experiment runners:

```text
tests/gro_ablation_test.cpp
tests/gro_baseline_test.cpp
tests/gro_selection_debug_test.cpp
tests/gro_reroute_debug_test.cpp
tests/gro_slot_legacy_ablation_test.cpp
```

Python analysis/plotting:

```text
python/plot_gro_component_ablation.py
python/plot_baseline_fraction_ttt.py
python/build_tdg_oracle_ablation.py
python/generate_bj_synthetic_queries.py
python/generate_bj_real_queries_from_tdrive.py
python/rescale_query_departures.py
```

Important notes:

```text
notes/gro_experiment_plan.md
notes/executables.md
notes/gro_algorithm_changes.md
notes/tdg_compression.md
notes/tdg_impact_normalization.md
notes/contgro_slot_legacy.md
notes/bj_synthetic_queries.md
notes/bj_real_queries.md
```

## Data Layout

Synthetic query sets:

```text
data/MH_Synthetic_query_sets
data/BJ_Synthetic_query_sets
```

BJ synthetic is currently the main ablation dataset:

```text
Hop: 5, 10, 40
Rep: 1, 2, 4
Seed/id: 0-29
Total: 3 x 3 x 30 configurations
```

Real BJ taxi-derived query sets:

```text
data/BJ_Real_query_sets
data/BJ_Real_query_sets_window6h
```

The real sets are generated from T-Drive taxi trajectories, filtered around
Tiananmen within an 8 km radius. See `notes/bj_real_queries.md`.

`data/BJ_Real_query_sets` preserves the original relative departure span, which
is about six days. This makes congestion weak for low amplification factors.
`data/BJ_Real_query_sets_window6h` linearly rescales each query file's departure
times into a six-hour window `[0, 21600]`. This is meant to be a controlled
real-workload peak setting. It is not a modulo workload.

The one-hour modulo real workload, if present locally, should be treated as too
aggressive for paper results unless explicitly re-validated. It can create
unrealistically huge congestion ratios.

## Current Experiment Layout

The preferred paper experiment order is:

1. Component ablation on BJ synthetic.
   - Show selection and reroute components in one 3 x 3 figure.
   - Use lines for TTT over iterations.
   - Use second y-axis bars for TDG selected fraction.
   - Compare fixed-fraction random/latency baselines against TDG-guided.
2. Compression and scalability on real-world workloads.
   - Vary query count.
   - Report TDG size, runtime, memory, and TTT preservation.
3. Parameter sensitivity.
   - Use focused settings, not a huge grid.
   - Gamma/impact weight can use BJ synthetic; compression parameter should use
     a representative real-world workload.
4. Overall effectiveness against paper baselines.
   - Compare final route quality/runtime against SVP, GOR, SOR, FAHL.
   - Be explicit that their paradigms differ from iterative GRO.

## Current Important Results And Caveats

- BJ synthetic with `capacity2_cap10e8` shows TDG-guided selection can select
  less than fixed 10% baselines while reducing TTT faster in many configurations.
- Reroute improvement from TDG-impact rerouting is not yet strong. Do not claim
  rerouting is the main contribution unless later results improve.
- Earlier observations suggested MH synthetic may show more reroute benefit,
  while BJ synthetic shows stronger selection benefit. Do not present this as a
  paper story. Reviewers are unlikely to accept a main result where different
  datasets support different components. Use BJ synthetic as the main ablation
  setting unless we later produce a unified, stable result.
- `Rep=1, Hop=40` is odd: normal-reroute methods barely improve. One objective
  outlier under the diagnostic oracle plot is `Hop40Rep1-24`, but the larger
  issue seems configuration-level rather than one corrupted dataset.
- The `best_param_by_iter` and `tdg_oracle` files are diagnostic lower envelopes.
  They are useful for debugging, but they are not final fair method results.
- Real workload status: the six-hour query files have been generated, but the
  full shortest-path congestion diagnostic was intentionally stopped by the
  user. Do not assume the six-hour workload has been validated yet. Provide the
  command and let the user run it, or ask before running.

## Execution Rule For Future Chats

Do not start long-running experiments unless the user explicitly asks you to run
them. It is okay to build small scripts, inspect files, summarize existing CSVs,
and provide commands. For expensive C++ experiments or diagnostics, prefer
giving the exact command line first. If the user says to run it, then run it.

## Current Plotting Script

`python/plot_gro_component_ablation.py` is the main 3 x 3 component-ablation
plotter. It supports:

```text
--show-selection-fraction
--baseline-fraction
--tdg-selection-method
--tdg-removal-mode
--tdg-gamma
--tdg-impact-weight
--exclude-datasets
--exclude-dataset-file
```

The second y-axis is now auto-scaled per subplot, using the local TDG selected
fraction and the baseline fraction line.

## New Chat Prompt Template

When opening a new chat, paste this:

```text
We are working in /Users/xyh/Desktop/GRO-Pro on a VLDB-style paper about GRO:
TDG-guided iterative global route optimization. Please first read
notes/new_chat_handoff_vldb.md, notes/gro_experiment_plan.md, and
notes/executables.md. Do not change code yet. Summarize the current goal,
method names, experiment layout, and the difference between fair method results
and diagnostic oracle/best-param-by-iteration results. Then wait for my next
instruction.
```

For a coding task, add:

```text
Before editing, inspect the relevant C++/Python files and existing notes. Keep
changes scoped. Do not delete data or results unless I explicitly ask. If a
result file is diagnostic, name it clearly as diagnostic and do not present it
as the final fair method. Do not start long-running C++ experiments unless I
explicitly ask you to run them; otherwise give me the command.
```

## What A New Chat Should Avoid

- Do not rename methods casually.
- Do not treat every baseline as iterative.
- Do not optimize for pretty plots by silently dropping datasets.
- Do not add many new selection variants to the paper story unless we decide to
  publish them.
- Do not claim TDG-impact rerouting is strong before checking evidence.
- Do not delete synthetic query sets or raw data.
- Do not use Python to run experiments that should be C++ experiments; Python is
  for analysis, plotting, and data generation.
- Do not start expensive experiments just because a command is available. Ask or
  provide the command first.
