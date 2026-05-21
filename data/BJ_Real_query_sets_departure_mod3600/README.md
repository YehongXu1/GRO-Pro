# BJ Real Query Sets With Departure Modulo 3600

This directory is derived from `data/BJ_Real_query_sets`.
Each `BJRealRep*.txt` file keeps the original origin and destination columns, and replaces the third column with `departure_time_seconds % 3600`.
The original real query directory is unchanged.

Use this as a diagnostic/high-concurrency real workload variant, and name result files with `departure_mod3600` or `mod3600` so they are not confused with the original fair real workload.
