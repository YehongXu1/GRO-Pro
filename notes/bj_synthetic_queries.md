# Beijing Synthetic Query Sets

## Purpose

The MH synthetic sets do not always show the same iterative fluctuation pattern
observed in earlier experiments on Beijing. We therefore generated synthetic
query sets on the Beijing road network, using the same high-level construction
as the paper setup:

```text
target OD length: 5 km, 10 km, 40 km
seed OD count per length: 30
base query count per seed OD: 100
density variants: Rep1, Rep2, Rep4
departure time: 0
```

## Data Location

Generated files:

```text
data/BJ_Synthetic_query_sets/
```

Examples:

```text
Hop5Rep1-0.txt
Hop5Rep2-0.txt
Hop5Rep4-0.txt
Hop10Rep1-0.txt
Hop40Rep4-29.txt
```

The naming keeps the existing experiment parser format:

```text
Hop{target_km}Rep{repeat_count}-{set_id}.txt
```

Here `Hop5`, `Hop10`, and `Hop40` mean target OD length in kilometers, not hop
count.

Summary files:

```text
data/BJ_Synthetic_query_sets/metadata.json
data/BJ_Synthetic_query_sets/seed_summary.csv
```

The current directory was extended incrementally so that earlier experimental
sets were preserved. Use `seed_summary.csv` / `metadata.json` as the exact
record of the generated seed ODs.

## Central-Area Constraint

Seed origins are sampled from central Beijing, approximated as a circle around
Tiananmen:

```text
center: 116.3975, 39.9087
radius: 16 km
```

This is used as a practical proxy for "within the 5th ring". The generated
central candidate set contains 115,143 nodes with outgoing edges.

## Distance Handling

For seed OD selection, the script uses directed Dijkstra on the Beijing graph.
Edge physical length is estimated from endpoint coordinates with haversine
distance.

Target seed shortest-distance tolerance:

```text
relative tolerance = 15%
```

So:

```text
5 km  -> 4.25 to 5.75 km
10 km -> 8.5 to 11.5 km
40 km -> 34 to 46 km
```

For each seed OD, local query ODs are sampled from spatial neighborhoods around
the seed origin and seed destination. This keeps query ODs near the seed OD
without running hundreds of Dijkstra searches for every set.

## Generation Command

```bash
/Users/xyh/opt/anaconda3/envs/plot/bin/python \
  python/generate_bj_synthetic_queries.py \
  --output-dir data/BJ_Synthetic_query_sets \
  --sets-per-distance 30 \
  --queries-per-set 100 \
  --rep-values 1,2,4 \
  --random-seed 0
```

## Experiment Config

Use:

```text
config/config_bj.yaml
```

Example baseline run:

```bash
./mh_synthetic_experiment config/config_bj.yaml \
  --query-dir data/BJ_Synthetic_query_sets \
  --output python/results/bj/bj_synthetic_baseline.csv \
  --algorithms baseline \
  --max-iterations 10
```
