# GRO Selection Debug Results

This note records the current selection-only diagnostic results.

## Source Files

Raw experiment output:

```text
python/results/gro_selection_debug_removal_modes.csv
```

Analysis script:

```text
python/analyze_selection_debug_results.py
```

Derived result tables:

```text
python/results/gro_selection_debug_removal_modes_comparison_summary.csv
python/results/gro_selection_debug_removal_modes_random_timing_by_gamma.csv
python/results/gro_selection_debug_removal_modes_random_timing_by_query_count.csv
python/results/gro_selection_debug_removal_modes_random_timing_by_hop_rep.csv
python/results/gro_selection_debug_removal_modes_random_timing_by_hop_rep_gamma.csv
```

Fixed-percentage simple baseline output:

```text
python/results/gro_simple_selection_baselines_10_30.csv
```

Comparison between TDG selection and fixed simple baselines:

```text
python/results/gro_simple_selection_baselines_10_30_vs_tdg_selection_rows.csv
python/results/gro_simple_selection_baselines_10_30_vs_tdg_selection_summary.csv
python/results/gro_simple_selection_baselines_10_30_vs_tdg_selection_by_hop_rep.csv
```

Important completeness note:

```text
The fixed simple-baseline file is complete:
720 rows = 180 query sets x 2 methods x 2 fixed fractions.
Each query set has random 10%, random 30%, most_delayed 10%,
and most_delayed 30%.
```

The analysis was run with:

```bash
MPLCONFIGDIR=/private/tmp/gro_mpl conda run -p /Users/xyh/opt/anaconda3/envs/plot \
  python python/analyze_selection_debug_results.py \
  --input python/results/gro_selection_debug_removal_modes.csv \
  --output-dir python/results
```

## Metric Definitions

For each run, TDG selection and random selection remove the same number of
queries from the current route set.

Selection quality:

```text
value_vs_random =
    random_unselected_after_remove - tdg_unselected_after_remove
```

Positive `value_vs_random` means TDG selection is better than random selection,
because the remaining unselected queries have lower aggregate total travel time.

Percentage over random:

```text
percent_vs_random_reduction =
    value_vs_random / random_reduction

random_reduction =
    total_before - random_unselected_after_remove
```

Selection running time:

```text
tdg_selection_time = tdg_prepare_sec + tdg_select_sec
extra_time_vs_random = tdg_selection_time - random_select_sec
```

`random_select_sec` is almost zero, so the most interpretable efficiency number
is the absolute extra time in seconds.

## Overall Result

Across all rows in the current result file:

```text
rows = 1620
query sets = 180
settings = 3 gamma values x 3 removal modes

mean value_vs_random   = +681,617
median value_vs_random = +517,977

mean TDG selection time    = 2.391 sec
median TDG selection time  = 1.337 sec
mean random selection time = 0.000206 sec
mean extra time vs random  = 2.391 sec
```

## By Gamma

This table averages over all query sets and all removal modes under each gamma.

| gamma | mean value vs random | median value vs random | mean TDG time | mean random time | mean extra time | mean selected fraction |
|---:|---:|---:|---:|---:|---:|---:|
| 25 | 714,431 | 628,587 | 1.588s | 0.000206s | 1.588s | 14.2% |
| 50 | 771,255 | 657,496 | 2.363s | 0.000206s | 2.363s | 40.7% |
| 75 | 559,164 | 324,544 | 3.222s | 0.000206s | 3.222s | 69.1% |

Interpretation:

```text
gamma=50 gives the largest average absolute value over random.
gamma=25 is more cost-effective: similar value, fewer selected queries,
and lower selection time.
```

## By Synthetic Configuration

This table averages over all seeds, gamma values, and removal modes for each
`hop, rep` configuration.

| hop | rep | mean value vs random | % vs random reduction | % of total TTT | mean TDG time | mean random time | mean extra time |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 10 | 1 | 141,909 | 1.16% | 0.672% | 0.258s | 0.000094s | 0.258s |
| 10 | 2 | 496,151 | 1.67% | 0.865% | 0.701s | 0.000177s | 0.701s |
| 10 | 4 | 1,393,623 | 2.16% | 1.038% | 1.660s | 0.000344s | 1.660s |
| 20 | 1 | 327,150 | 1.45% | 0.741% | 0.610s | 0.000094s | 0.610s |
| 20 | 2 | 784,946 | 1.54% | 0.682% | 1.688s | 0.000178s | 1.688s |
| 20 | 4 | 1,314,365 | 1.08% | 0.482% | 4.121s | 0.000346s | 4.121s |
| 40 | 1 | 323,874 | 0.93% | 0.448% | 1.091s | 0.000094s | 1.091s |
| 40 | 2 | 457,860 | 0.50% | 0.233% | 3.080s | 0.000178s | 3.080s |
| 40 | 4 | 894,674 | 0.36% | 0.178% | 8.310s | 0.000347s | 8.310s |

Interpretation:

```text
Absolute value usually increases with rep.
Selection time increases strongly with hop and rep.
The relative percentage gain decreases on the largest hop=40 settings,
especially Hop40Rep4.
```

## Current Takeaways

The TDG selection signal is better than same-size random selection on average,
but it is not free. The average extra selection time is seconds, while random
selection is essentially zero-cost.

For selection-only evidence, the most useful statement is:

```text
TDG selection gives a positive average value over random selection, but adds
roughly 0.65s, 1.82s, and 4.70s on average for 1024, 2048, and 4096-query
instances respectively.
```

For parameter choice:

```text
gamma=50 maximizes average value.
gamma=25 gives a better value/time tradeoff.
```

## Fixed 10% / 30% Simple Baseline Comparison

Simple baselines:

```text
random: select a fixed random 10% or 30% of queries.
most_delayed: select the queries with largest
              actual_travel_time - free_flow_shortest_path_time.
```

This comparison is not same-size unless TDG's selected fraction is close to the
baseline fraction. Therefore, selected fractions must be reported together with
the value.

Per-query remaining travel time is also recorded because it is less sensitive
to different selected fractions:

```text
remaining_avg_query_tt = remaining_TTT / (query_count - selected_count)

avg_query_tt_value_vs_baseline =
    baseline_remaining_avg_query_tt - tdg_remaining_avg_query_tt
```

Positive `avg_query_tt_value_vs_baseline` means the queries left after TDG
selection have lower average travel time than the queries left after the
baseline selection.

Results from all 180 query sets:

```text
gamma=25, anchor_important:
  TDG selected fraction = 16.3%
  vs random 10%:       +12,155,168, extra time +1.668s
  vs most_delayed 10%: +7,741,658, extra time +1.667s
  vs random 30%:       -26,627,726
  vs most_delayed 30%: -34,321,828

gamma=50, anchor_important:
  TDG selected fraction = 42.7%
  vs random 30%:       +24,354,289, extra time +2.475s
  vs most_delayed 30%: +16,660,187, extra time +2.474s

gamma=50, all_nodes:
  TDG selected fraction = 41.2%
  vs random 30%:       +22,118,243, extra time +2.416s
  vs most_delayed 30%: +14,424,141, extra time +2.415s
```

Per-query remaining travel-time comparison:

| TDG setting | baseline | TDG selected | baseline selected | baseline avg remaining TT | TDG avg remaining TT | TDG lower by | win rate |
|---|---|---:|---:|---:|---:|---:|---:|
| gamma25 anchor | random 10% | 16.3% | 10.0% | 57,569 | 55,475 | 2,095 | 82.2% |
| gamma25 anchor | most_delayed 10% | 16.3% | 10.0% | 55,751 | 55,475 | 277 | 54.4% |
| gamma50 anchor | random 30% | 42.7% | 30.0% | 52,695 | 47,814 | 4,881 | 97.8% |
| gamma50 anchor | most_delayed 30% | 42.7% | 30.0% | 48,866 | 47,814 | 1,052 | 65.6% |
| gamma50 all_nodes | random 30% | 41.2% | 30.0% | 52,695 | 48,360 | 4,335 | 98.9% |
| gamma50 all_nodes | most_delayed 30% | 41.2% | 30.0% | 48,866 | 48,360 | 506 | 62.2% |

Interpretation:

```text
Against fixed 10% baselines, gamma=25 TDG selection looks better, but TDG
selects about 16%, not 10%.

Against fixed 30% baselines, gamma=25 is worse because it selects much fewer
queries. gamma=50 is better than fixed 30% random and most-delayed, but it
also selects about 41%-43% of queries.

Therefore this fixed-fraction comparison is useful as a heuristic baseline
check, but the same-size random comparison remains the cleanest selection-only
quality test.
```
