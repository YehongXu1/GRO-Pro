# Results Layout

Use experiment-first paths for new outputs. Keep raw CSVs, derived summaries,
and plots inside the same experiment folder so each paper section is easy to
audit.

```text
python/results/
  experiments/
    exp1_component_ablation/
      bj_synthetic_capacity2_cap10e8/
        raw/
        oracle/
        analysis/
        plots/
    exp3_compression_scalability/
      bj_real_window1h/
    exp4_parameter_sensitivity/
      bj_synthetic_capacity2_cap10e8/
        gamma/
          csv/
          plots/
        impact_weight/
          csv/
          plots/
  diagnostics/
    congestion/
      bj/
    debug/
      bj/
      mh/
  archive/
    legacy_tdg_oracle_bj/
    capacity_variant_bj/
    mh_legacy_ablation/
    bj_real_mod3600_smoke/
```

Conventions:

- `raw/`: direct executable outputs.
- `analysis/` or `csv/`: derived summaries and tables.
- `plots/`: figures generated from raw or derived CSVs.
- `diagnostics/`: sanity checks that support dataset interpretation rather
  than a main experiment claim.
- `archive/`: old or superseded outputs kept for traceability.

Avoid placing new files directly under `python/results/bj`, `python/results/mh`,
or `python/results/bj_real`; use the experiment or diagnostic folders above.
