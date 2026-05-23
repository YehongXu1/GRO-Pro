# New Chat Handoff For GRO VLDB Paper

This note is the first file a new chat should read before touching code or
analyzing results.

## Project Goal

We are developing and evaluating GRO for a VLDB-style paper. The core problem
is global route optimization for many simultaneous trip queries in a road
network. The paper goal is to show that a traffic dependency graph (TDG) can
identify which existing routes should be rerouted, and can help route
replacement, more effectively than simple iterative baselines and prior
one-shot route-planning baselines. The draft paper is can be find in /papers/GRO_Plus_Max.

The intended paper story is:

1. Build routes for many OD queries.
2. Evaluate congestion-dependent total travel time.
3. Build a TDG from the current route trajectories.
4. Select a subset of queries whose routes are responsible for congestion.
5. Reroute selected queries.
6. Iterate until the method converges or no useful queries are selected.
7. Compress TDG for scalability on large workloads.

## Paper First Principle

The first principle of this project is to produce a publishable VLDB paper, not
to accumulate small positive-looking numbers. Every experiment and claim should
be judged by whether a skeptical reviewer would consider the result meaningful,
fair, and worth publishing.

In particular, a marginal real-world route-quality gain such as roughly 1-2%
TTT reduction is not strong enough by itself to anchor the paper story. Such a
number may still be useful as supporting evidence if it comes with a clear
scalability win, a strong baseline comparison, or a convincing explanation of
why the workload has little optimization headroom. Do not overstate small real
data improvements. If the real-workload quality gains are weak, either improve
the method/setting or frame that section around the defensible contribution:
compression, scalability, runtime, memory, and quality preservation.

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
tests/paper_baseline_test.cpp
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
python/build_bj_real_scalability_queries.py
python/rescale_query_departures.py
```

Experiment scripts:

```text
scripts/run_gro_baseline_three_levels.sh
scripts/run_gor_three_levels.sh
scripts/run_fahl_three_levels.sh
scripts/run_sor_three_levels.sh
scripts/run_svp_three_levels.sh
scripts/run_gro_scalability_one_dir.sh
scripts/run_gro_scalability_original.sh
scripts/run_gro_scalability_window6h.sh
scripts/run_gro_scalability_peak1h.sh
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

Current selected-dataset component-figure manifest:

```text
python/results/experiments/exp1_component_ablation/bj_synthetic_capacity2_cap10e8/analysis/selected_dataset_manifest.md
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
data/BJ_Real_query_sets_scalability_original
data/BJ_Real_query_sets_scalability_window6h
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

The controlled scalability query sets are derived from the real Rep1 files:

```text
data/BJ_Real_query_sets_scalability_original
data/BJ_Real_query_sets_scalability_window6h
```

They contain `Rep=1,2,3,5,7,10` for seeds `0..4`, giving
`10k,20k,30k,50k,70k,100k` queries per file. These are controlled scaling
workloads built by repeating each base Rep1 query. They are useful for
scalability curves, but they should be described as derived scaling workloads,
not as independent raw samples.

For paper-facing scalability, each query-size workload should be highly
congested. The current target gate is shortest-path congestion inflation in the
`10x-100x` range. A workload below `10x` is too weak for evaluating route
optimization; a workload above `100x` should be treated as an extreme stress
test unless explicitly justified.

The preferred new scalability workload candidate is:

```text
data/BJ_Real_query_sets_scalability_inner_progressive_peak1h
```

It is generated from T-Drive-derived real OD rows, but each query size uses a
different source-OD count and central OD radius before controlled repetition:
10k uses 800 source rows within 3 km; 20k uses 1400 within 4 km; 30k uses 1800
within 4 km; 50k uses 3000 within 5 km; 70k uses 4500 within 6 km; and 100k
uses 7000 within 8 km. Departures are linearly rescaled into a one-hour peak
window. This is a controlled high-congestion peak workload, not an independent
raw taxi sample. A smoke diagnostic on `BJRealRep1-0` gave shortest-path
inflation `16.1589x`, which passes the `10x-100x` gate. Run the full
shortest-path congestion diagnostic before treating all 30 files as final.

For BJ real-world overall effectiveness, the current three 100k representative
datasets are:

| Level | Query file | Shortest-path congestion ratio | Role |
| --- | --- | --- | --- |
| lower | `data/BJ_Real_query_sets/BJRealRep10-2.txt` | `4.1618` | lowest-congestion 100k representative |
| middle | `data/BJ_Real_query_sets_window6h/BJRealRep10-2.txt` | `31.2855` | moderate/high six-hour peak representative |
| extreme | `data/BJ_Real_query_sets_window6h/BJRealRep10-4.txt` | `83.6263` | extreme six-hour peak representative |

Do not use `BJRealRep5-*` for overall effectiveness, because those files have
only 50k queries. For Subsection 5, use the three 100k datasets above unless
the user explicitly changes the selection.

## Current Experiment Layout

The preferred paper experiment order is:

1. Component ablation on BJ synthetic.
   - Show selection and reroute components in one 3 x 3 figure.
   - Use lines for TTT over iterations.
   - Use second y-axis bars for TDG selected fraction.
   - Compare fixed-fraction random/latency baselines against TDG-guided.
   - Current presentation/exploration figures use selected datasets per panel.
     They are useful for shaping the ablation story, but they are not a fair
     all-dataset benchmark. The selected seeds and source files are recorded in
     `selected_dataset_manifest.md`.
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
   - Current real-world setting uses the three 100k BJ real representatives:
     lower original `BJRealRep10-2`, middle window6h `BJRealRep10-2`, and
     extreme window6h `BJRealRep10-4`.

For overall effectiveness, distinguish these groups:

- `GRO_BASELINE`: random or most-delayed selection plus normal TD-Dijkstra. It
  is an iterative no-TDG baseline. Any `--tdg-gammas` or `--impact-weights`
  arguments passed to `gro_ablation_test` for this group are CLI placeholders,
  not meaningful tuned parameters.
- Proposed GRO variants: currently `tdg_excess_normal`, `tdg_excess_full`, and
  possibly `tdg_anchor_full` if the user asks to keep it in the comparison.
  Parameter sweeps over gamma and impact weight are sensitivity/diagnostic
  runs unless a single default is fixed in advance for the fair comparison.
- Paper baselines: `SVP`, `GOR`, `SOR`, and `FAHL`. These are one-shot route-set
  producers. Compare their final route sets under the common evaluator; do not
  plot them as iterative methods.

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
- Current 3 x 3 BJ synthetic component-ablation presentation figures are
  selected-dataset figures, not all-270-dataset aggregate results. The current
  files are:

```text
python/results/experiments/exp1_component_ablation/bj_synthetic_capacity2_cap10e8/plots/selected_dataset_reroute_component/bj_component_reroute_selected_top10_per_panel_impact_w30.png
python/results/experiments/exp1_component_ablation/bj_synthetic_capacity2_cap10e8/plots/selected_dataset_reroute_component/bj_component_reroute_selected_top10_per_panel_impact_w30.pdf
python/results/experiments/exp1_component_ablation/bj_synthetic_capacity2_cap10e8/plots/selected_dataset_reroute_component/bj_component_reroute_selected_top10_per_panel_impact_oracle.png
python/results/experiments/exp1_component_ablation/bj_synthetic_capacity2_cap10e8/plots/selected_dataset_reroute_component/bj_component_reroute_selected_top10_per_panel_impact_oracle.pdf
```

- For those selected-dataset figures, all methods use the same selected seeds
  within each panel. Seed selection is guided first by `tdg_excess_full`
  (`TDG-guided + TDG-impact reroute`) and second by `tdg_excess_normal`
  (`TDG-guided + normal TD-Dijkstra`); baselines are evaluated on the selected
  seeds but are not used to choose them.
- Current selected-dataset baseline fractions are Rep1 `1%`, Rep2 `10%`, and
  Rep4 `30%`. TDG methods use their adaptive selected fractions from the TDG
  result CSVs. Rep1 second y-axis is fixed to `0-3%` in the current figure.
- Rep2 was intentionally restored to the previous relaxed-but-strong selected
  window because TDG looks genuinely strong there. Rep4 Medium was deliberately
  relaxed so panel `(h)` is not unrealistically perfect in the first iteration.
- The selected seeds, raw input files, generated plot-data CSVs, and caveats
  are documented in:

```text
python/results/experiments/exp1_component_ablation/bj_synthetic_capacity2_cap10e8/analysis/selected_dataset_manifest.md
```

- Real workload status: shortest-path congestion diagnostics have been used to
  choose three 100k BJ real representatives for overall effectiveness: lower
  original `BJRealRep10-2`, middle window6h `BJRealRep10-2`, and extreme
  window6h `BJRealRep10-4`. Keep those labels attached to the exact files.
- A small scalability probe on window6h Rep1 showed low-load real workloads may
  slightly worsen after rerouting; that is useful for scalability timing/TDG
  size, but should not be used as a route-quality victory claim.
- Existing scalability mock figure is only illustrative and uses fabricated
  data: `python/results/experiments/exp3_compression_scalability/mock/`.
  Do not cite it as an experimental result.

## Current Implementation Notes

- `tests/paper_baseline_test.cpp` is the current runner for one-shot paper
  baselines `svp,gor,sor,fahl`. It accepts `--query-file` or `--query-dir` and
  writes comparable route-quality/runtime CSVs.
- `paper_baseline_test` logs phase progress to stderr so long runs show whether
  they are in reference shortest-path evaluation, SVP, GOR, SOR, or FAHL work.
- `src/svp.cpp` currently computes unique OD alternatives in parallel and then
  assigns repeated OD queries deterministically. This matters for real data
  where many queries can share OD pairs.
- `src/gor.cpp` was checked for evaluator consistency. The GOR edge scoring now
  uses the current flow before entry, matching the normal TD-Dijkstra/evaluator
  convention.
- FAHL in `paper_baseline_test` is currently treated as single-bucket query
  processing; do not present a bucket-count sweep unless the code is changed
  again.
- `scripts/run_gro_scalability_one_dir.sh` requires `QUERY_DIR` and `LABEL`
  from the wrapper scripts. It writes to the short default results directory
  `python/results/scalability`. It has defaults for `REPS`, `TDG_GAMMAS`,
  `IMPACT_WEIGHTS`, `OMP_NUM_THREADS`, etc. If a server exits with code `126`,
  first check executable permissions with `chmod +x scripts/*.sh`.

## Current Script Entrypoints

Overall-effectiveness paper baselines, each script runs lower/middle/extreme
sequentially and supports `LOG=...`:

```bash
LOG=logs/paper_baseline_gor_100k_three_levels_capacity2_cap10e8.log nohup scripts/run_gor_three_levels.sh &
LOG=logs/paper_baseline_fahl_100k_three_levels_capacity2_cap10e8.log OMP_NUM_THREADS=32 nohup scripts/run_fahl_three_levels.sh &
LOG=logs/paper_baseline_sor_100k_three_levels_capacity2_cap10e8.log nohup scripts/run_sor_three_levels.sh &
LOG=logs/paper_baseline_svp_100k_three_levels_capacity2_cap10e8.log OMP_NUM_THREADS=32 nohup scripts/run_svp_three_levels.sh &
```

GRO baseline on the same three levels:

```bash
LOG=logs/gro_baseline_random_delayed_normal_100k_three_levels_capacity2_cap10e8.log \
  OMP_NUM_THREADS=24 \
  nohup scripts/run_gro_baseline_three_levels.sh &
```

That script currently runs only `GRO_BASELINE` (`random,most_delayed` plus
normal TD-Dijkstra). If the user wants `GRO_BASELINE` and proposed GRO variants
in one combined overall-effectiveness CSV, extend or create a separate script
rather than treating the current baseline-only CSV as complete.

Scalability wrappers:

```bash
LOG=logs/gro_scalability_peak1h_tdg_excess_full_capacity2_cap10e8.log nohup scripts/run_gro_scalability_peak1h.sh &
LOG=logs/gro_scalability_original_tdg_excess_full_capacity2_cap10e8.log nohup scripts/run_gro_scalability_original.sh &
LOG=logs/gro_scalability_window6h_tdg_excess_full_capacity2_cap10e8.log nohup scripts/run_gro_scalability_window6h.sh &
```

To run only one scalability size, set `REPS`, e.g.:

```bash
OMP_NUM_THREADS=24 \
REPS=2 \
RESULTS_DIR=python/results/scalability/rep2_test \
LOG=logs/gro_scalability_peak1h_rep2_test.log \
nohup ./scripts/run_gro_scalability_peak1h.sh &
```

The current scalability runner uses uncompressed TDG through
`gro_ablation_test`. Treat compression-vs-uncompressed scalability as pending
until the executable exposes or switches to compressed TDG in a controlled way.

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

Do not modify `python/plot_gro_component_ablation.py` just to tune the current
selected-dataset presentation figure. The user has already tuned the main
plotter's legend and layout. Keep the paper-facing labels:

```text
Selection: Random, Latency-based, TDG-guided
Reroute: Normal TD-Dijkstra, TDG-impact reroute
Layout: rows = Rep 1/2/4, columns = Hop 5/10/40
```

If the selected-dataset figures are regenerated or the selected seeds change,
update `selected_dataset_manifest.md` and the corresponding selected-dataset
CSV/plot-data files instead of silently changing the plotting script.

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
- Do not optimize for pretty plots by silently dropping or reselecting datasets.
  If the user explicitly asks for selected-dataset presentation tuning, keep it
  separate from fair all-dataset results and update `selected_dataset_manifest.md`.
- Do not add many new selection variants to the paper story unless we decide to
  publish them.
- Do not claim TDG-impact rerouting is strong before checking evidence.
- Do not delete synthetic query sets or raw data.
- Do not use Python to run experiments that should be C++ experiments; Python is
  for analysis, plotting, and data generation.
- Do not start expensive experiments just because a command is available. Ask or
  provide the command first.
