# GRO Experimental Data Manifest

Last updated: 2026-05-27

This file records the experimental datasets used by the GRO experiments. These
are our project-level experimental datasets or derived query sets. They should
be treated separately from third-party raw data and from transient result CSVs.

The purpose of this manifest is to prepare a later public dataset release. It
documents what each dataset is, how it was derived, which experiment uses it,
and what must be checked before release.

Machine-readable inventory:

```text
data/public_release_inventory.csv
```

## Release Status Labels

- `release-candidate`: generated query data that can likely be published after
  license and privacy review.
- `derived-from-third-party`: generated from an external source; do not publish
  the raw source file unless the license permits it.
- `internal-result-artifact`: plot or selected-subset data used to reproduce
  figures; useful for artifact release, but not a standalone benchmark dataset.
- `external-input`: road-network or coordinate input used by experiments; check
  the original source and license before public redistribution.
- `planned`: dataset family or derivation path that is recorded for future
  work but has not been generated yet.

## Common Query Format

All query-set text files use one query per row:

```text
origin_node destination_node departure_time_seconds
```

Synthetic Beijing query files use:

```text
Hop{target_km}Rep{repeat_count}-{set_id}.txt
```

Real Beijing query files use:

```text
BJRealRep{amplification_factor}-{set_id}.txt
```

## External Inputs

| Path | Status | Notes |
| --- | --- | --- |
| `data/BJ.txt` | external-input | Beijing directed road graph used by all BJ experiments. Verify redistribution rights before public release. |
| `data/BJ_NodeIDLonLat.txt` | external-input | Node coordinates for the BJ graph. Verify redistribution rights before public release. |
| `data/MH.txt` | external-input | Manhattan road graph used by MH experiments. Verify redistribution rights before public release. |
| `data/MH_NodeIDLonLat.txt` | external-input | Node coordinates for the MH graph. Verify redistribution rights before public release. |
| `data/NY.txt` | external-input | New York road graph available locally. Verify experiment role and redistribution rights before public release. |
| `data/NY_NodeIDLonLat.txt` | external-input | Node coordinates for the NY graph. Verify experiment role and redistribution rights before public release. |
| `data/BJ_Real_query_sets/T-drive Taxi Trajectories.zip` | derived-from-third-party | Raw T-Drive GPS archive used only as input. Do not include this zip in our public release unless the upstream license explicitly allows redistribution. |

## BJ Synthetic Query Sets

| Field | Value |
| --- | --- |
| Path | `data/BJ_Synthetic_query_sets/` |
| Status | release-candidate |
| Experiment use | Main synthetic ablation and parameter-sensitivity experiments |
| Query files | 270 |
| Panel dimensions | Hop 5/10/40 km x Rep 1/2/4 x seeds 0-29 |
| Query counts | Rep1 = 100, Rep2 = 200, Rep4 = 400 |
| Departures | All queries depart at time 0 |
| Generator | `python/generate_bj_synthetic_queries.py` |
| Metadata | `data/BJ_Synthetic_query_sets/metadata.json`, `data/BJ_Synthetic_query_sets/seed_summary.csv` |

This is our controlled synthetic workload on the BJ graph. `Hop` means target
OD distance in kilometers, not graph hop count. The generator samples central
Beijing seed ODs around Tiananmen with a 16 km radius and uses directed
shortest-path distance to target 5 km, 10 km, and 40 km OD lengths.

Generation command recorded in:

```text
notes/bj_synthetic_queries.md
```

## BJ Real-Derived Query Sets

| Field | Value |
| --- | --- |
| Path | `data/BJ_Real_query_sets/` |
| Status | release-candidate, derived-from-third-party |
| Experiment use | Real-workload robustness and congestion diagnostics |
| Query files | 15 |
| Dimensions | Rep 1/5/10 x seeds 0-4 |
| Query counts | Rep1 = 10,000; Rep5 = 50,000; Rep10 = 100,000 |
| Source | Public T-Drive Beijing taxi GPS trajectories |
| Generator | `python/generate_bj_real_queries_from_tdrive.py` |
| Metadata | `data/BJ_Real_query_sets/metadata.json`, `data/BJ_Real_query_sets/query_set_summary.csv` |

These files are not raw taxi trajectories. They are derived OD query sets
created by filtering T-Drive GPS points in an 8 km Tiananmen-centered region,
snapping to the BJ graph, extracting trajectory segments, and sampling OD
queries. The raw T-Drive zip should remain out of any public package unless the
upstream license allows redistribution.

Generation details are recorded in:

```text
notes/bj_real_queries.md
```

## BJ Real Time-Window Variants

These variants preserve the OD pairs and relative temporal order within each
file, but rescale departures into controlled windows. They are experimental
traffic-stress workloads, not independent raw samples.

| Path | Status | Query files | Window | Experiment use |
| --- | --- | ---: | --- | --- |
| `data/BJ_Real_query_sets_window1h/` | release-candidate, derived-from-third-party | 15 | `[0, 3600]` seconds | Dense one-hour workload checks and scalability probes |
| `data/BJ_Real_query_sets_window6h/` | release-candidate, derived-from-third-party | 15 | `[0, 21600]` seconds | Controlled real workload with moderate congestion |

Metadata:

```text
data/BJ_Real_query_sets_window1h/departure_rescale_metadata.json
data/BJ_Real_query_sets_window6h/departure_rescale_metadata.json
```

Rescaling script:

```text
python/rescale_query_departures.py
```

## BJ Scalability Query Sets

These directories are for Exp3, compression and scalability. They are derived
from the real query sets and should be described as controlled scalability
workloads.

| Path | Status | Query files | Dimensions | Notes |
| --- | --- | ---: | --- | --- |
| `data/BJ_Real_query_sets_scalability_original/` | release-candidate, derived-from-third-party | 30 | Rep 1/2/3/5/7/10 x seeds 0-4 | Scaled query-count variants without peak-window rescaling. |
| `data/BJ_Real_query_sets_scalability_window6h/` | release-candidate, derived-from-third-party | 30 | Rep 1/2/3/5/7/10 x seeds 0-4 | Six-hour controlled scalability variants. |
| `data/BJ_Real_query_sets_scalability_inner_progressive_peak1h/` | release-candidate, derived-from-third-party | 30 | Rep 1/2/3/5/7/10 x seeds 0-4 | Current high-congestion peak-1h scalability candidate. |

For `data/BJ_Real_query_sets_scalability_inner_progressive_peak1h/`, the
metadata states that the workload samples source rows from the T-Drive-derived
`BJRealRep1` files, repeats selected source rows, filters by central OD radius,
rescales into a 1-hour departure window, and sorts by departure time. Query
counts are:

```text
10,000; 20,000; 30,000; 50,000; 70,000; 100,000
```

Metadata:

```text
data/BJ_Real_query_sets_scalability_original/metadata.json
data/BJ_Real_query_sets_scalability_window6h/metadata.json
data/BJ_Real_query_sets_scalability_inner_progressive_peak1h/metadata.json
```

## Planned MH/NY Taxi Real Query Sets

| Field | Value |
| --- | --- |
| Planned path | `data/MH_Real_query_sets/` |
| Status | planned, derived-from-third-party |
| Intended use | Real Manhattan robustness and possible overall-effectiveness cross-check |
| Preferred source | Public NYC TLC yellow taxi trip records with explicit pickup/dropoff longitude and latitude |
| Fallback source | Modern TLC Parquet trip records plus taxi-zone shapefile or centroids |
| Generator | `python/generate_mh_real_queries_from_tlc.py` |
| Target naming | `MHRealRep{amplification_factor}-{set_id}.txt` |
| Target query counts | Rep1 = 10,000; Rep5 = 50,000; Rep10 = 100,000 |

The preferred derivation should use coordinate-level taxi records, filter trips
inside the MH study area, snap pickup/dropoff coordinates to
`data/MH_NodeIDLonLat.txt`, and write project-format OD departure queries.
Modern TLC Parquet data is easier to obtain but is zone-level
(`PULocationID`/`DOLocationID`), so it should be treated as a coarse fallback
unless a route-level generator cannot use historical coordinate records.
The current generator intentionally expects coordinate-level CSV input and does
not consume zone-only Parquet files.

Design notes are recorded in:

```text
notes/mh_real_queries.md
```

## Paper-Facing Selected Ablation Data

The selected-dataset ablation figure is not an all-dataset benchmark. It is a
paper-facing, fixed selected-subset visualization for the component/reroute
ablation.

| Artifact | Status | Purpose |
| --- | --- | --- |
| `python/results/experiments/exp1_component_ablation/bj_synthetic_capacity2_cap10e8/analysis/selected_dataset_manifest.md` | internal-result-artifact | Authoritative record of selected seeds and figure provenance. |
| `python/results/experiments/exp1_component_ablation/bj_synthetic_capacity2_cap10e8/analysis/selected_datasets_for_component_reroute_3x3_top10_per_panel.csv` | internal-result-artifact | Selected seeds for the impact-oracle figure. |
| `python/results/experiments/exp1_component_ablation/bj_synthetic_capacity2_cap10e8/analysis/plot_data_component_reroute_3x3_selected_top10_per_panel.csv` | internal-result-artifact | Aggregated plot data used by the impact-oracle selected figure. |
| `python/results/experiments/exp1_component_ablation/bj_synthetic_capacity2_cap10e8/analysis/selected_datasets_for_component_reroute_3x3_top10_per_panel_impact_w30.csv` | internal-result-artifact | Selected seeds for the fixed impact-weight-30 figure. |
| `python/results/experiments/exp1_component_ablation/bj_synthetic_capacity2_cap10e8/analysis/plot_data_component_reroute_3x3_selected_top10_per_panel_impact_w30.csv` | internal-result-artifact | Aggregated plot data used by the fixed impact-weight-30 selected figure. |

Important release wording:

```text
These selected-dataset files are included for figure reproducibility. They are
not the full BJ synthetic benchmark and should not be described as unbiased
all-dataset performance results.
```

## Suggested Public Release Bundles

For a public artifact, keep raw third-party data and our generated query sets
separate.

```text
release/
  gro_bj_synthetic_query_sets/
    README.md
    metadata.json
    seed_summary.csv
    Hop*Rep*-*.txt
  gro_bj_real_derived_query_sets/
    README.md
    metadata.json
    query_set_summary.csv
    BJRealRep*.txt
  gro_bj_real_time_window_variants/
    README.md
    window1h/
    window6h/
  gro_bj_scalability_query_sets/
    README.md
    original/
    window6h/
    inner_progressive_peak1h/
  gro_experiment_plot_data/
    README.md
    selected_dataset_manifest.md
    selected_datasets_*.csv
    plot_data_*.csv
```

Each bundle should include:

- a short README describing the source, derivation, row format, and naming;
- the exact generator script and command, or a link to the repository commit;
- metadata JSON/CSV files;
- checksums for all released text files;
- a license/notice file distinguishing our generated data from external input
  data and T-Drive-derived content.

## Release Checklist

- Verify redistribution rights for `data/BJ.txt` and
  `data/BJ_NodeIDLonLat.txt`.
- Verify redistribution rights for `data/MH.txt`, `data/MH_NodeIDLonLat.txt`,
  `data/NY.txt`, and `data/NY_NodeIDLonLat.txt` before any public release.
- Do not publish `T-drive Taxi Trajectories.zip` unless the upstream license
  permits redistribution.
- Do not publish raw NYC TLC trip files inside our artifact unless the source
  license and privacy terms explicitly allow redistribution; prefer scripts,
  metadata, and derived query files.
- Decide whether T-Drive-derived query sets can be released directly, or only
  via scripts plus metadata.
- Remove local system files such as `.DS_Store`.
- Add checksums after final packaging.
- Keep selected-dataset plot artifacts clearly labeled as figure-reproduction
  data, not benchmark data.
