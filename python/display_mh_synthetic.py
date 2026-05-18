#!/usr/bin/env python3
"""Create an SVG figure for MH synthetic experiment results.

This script intentionally uses only the Python standard library so it works in
minimal environments without matplotlib.
"""

from __future__ import annotations

import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESULT_DIR = ROOT / "python" / "results"

COLORS = {
    "gro": "#1f77b4",
    "baseline": "#d62728",
    "random_baseline": "#d62728",
    "selection_td_baseline": "#2ca02c",
    "normal_selection_gro_reroute": "#9467bd",
}


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as file:
        return list(csv.DictReader(file))


def load_series_rows(path: Path) -> list[dict[str, str]]:
    rows = load_rows(path)
    existing = {
        (row.get("algorithm"), row.get("dataset"), row.get("iteration"))
        for row in rows
    }

    event_path = path.with_name(path.stem + "_events.csv")
    if not event_path.exists():
        return rows

    max_iteration_by_dataset: dict[str, int] = defaultdict(int)
    for row in rows:
        if row.get("iteration", "").isdigit():
            dataset = row.get("dataset", "")
            max_iteration_by_dataset[dataset] = max(
                max_iteration_by_dataset[dataset],
                int(row["iteration"]),
            )

    for row in load_rows(event_path):
        label = row.get("label")
        if label not in {"BASELINE", "BASELINE_FINAL"}:
            continue
        dataset = row.get("dataset", "")
        iteration = row.get("iteration", "")
        if label == "BASELINE_FINAL":
            iteration = str(max_iteration_by_dataset.get(dataset, 10))
        key = (row.get("algorithm"), dataset, iteration)
        if key in existing:
            continue
        rows.append({
            "algorithm": row.get("algorithm", ""),
            "dataset": dataset,
            "hop": row.get("hop", ""),
            "rep": row.get("rep", ""),
            "seed": row.get("seed", ""),
            "label": label,
            "iteration": iteration,
            "total_travel_time": row.get("total_travel_time", ""),
        })
        existing.add(key)

    return rows


def nice_number(value: float) -> str:
    if value >= 1_000_000:
        return f"{value / 1_000_000:.1f}M"
    if value >= 1_000:
        return f"{value / 1_000:.0f}K"
    return f"{value:.0f}"


def polyline(points: list[tuple[float, float]]) -> str:
    return " ".join(f"{x:.2f},{y:.2f}" for x, y in points)


def draw_svg(rows: list[dict[str, str]], output: Path) -> None:
    grouped: dict[tuple[int, int], dict[str, list[tuple[int, float]]]] = defaultdict(
        lambda: defaultdict(list)
    )
    hops = set()
    reps = set()
    algorithms = set()

    for row in rows:
        hop = int(row["hop"])
        rep = int(row["rep"])
        algorithm = row["algorithm"]
        iteration = int(row["iteration"])
        total = float(row["total_travel_time"])
        grouped[(hop, rep)][algorithm].append((iteration, total))
        hops.add(hop)
        reps.add(rep)
        algorithms.add(algorithm)

    hops_sorted = sorted(hops)
    reps_sorted = sorted(reps)
    preferred_order = [
        "gro",
        "baseline",
        "random_baseline",
        "selection_td_baseline",
        "normal_selection_gro_reroute",
    ]
    algorithms_sorted = [name for name in preferred_order if name in algorithms]
    algorithms_sorted += sorted(algorithms - set(algorithms_sorted))

    width = 1320
    height = 900
    margin_left = 72
    margin_right = 30
    margin_top = 76
    margin_bottom = 68
    gap_x = 46
    gap_y = 58
    panel_w = (width - margin_left - margin_right - gap_x * (len(reps_sorted) - 1)) / len(reps_sorted)
    panel_h = (height - margin_top - margin_bottom - gap_y * (len(hops_sorted) - 1)) / len(hops_sorted)

    all_iterations = [
        iteration
        for by_algorithm in grouped.values()
        for points in by_algorithm.values()
        for iteration, _ in points
    ]
    x_min = min(all_iterations) if all_iterations else 0
    x_max = max(all_iterations) if all_iterations else 1
    if x_min == x_max:
        x_max = x_min + 1

    svg: list[str] = []
    svg.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">')
    svg.append('<rect width="100%" height="100%" fill="white"/>')
    svg.append('<style>text{font-family:Arial,Helvetica,sans-serif;fill:#222}.axis{stroke:#333;stroke-width:1}.grid{stroke:#ddd;stroke-width:1}.line{fill:none;stroke-width:2.4}.tick{font-size:11px}.title{font-size:14px;font-weight:700}.label{font-size:13px}.legend{font-size:13px}</style>')
    svg.append(f'<text x="{width / 2:.1f}" y="28" text-anchor="middle" class="title">MH Synthetic: GRO vs GRO Baseline</text>')

    legend_x = margin_left
    legend_y = 50
    for algorithm in algorithms_sorted:
        color = COLORS.get(algorithm, "#555")
        svg.append(f'<line x1="{legend_x}" y1="{legend_y}" x2="{legend_x + 28}" y2="{legend_y}" stroke="{color}" stroke-width="3"/>')
        svg.append(f'<circle cx="{legend_x + 14}" cy="{legend_y}" r="3.5" fill="{color}"/>')
        svg.append(f'<text x="{legend_x + 36}" y="{legend_y + 4}" class="legend">{algorithm}</text>')
        legend_x += 150

    for row_index, hop in enumerate(hops_sorted):
        for col_index, rep in enumerate(reps_sorted):
            x0 = margin_left + col_index * (panel_w + gap_x)
            y0 = margin_top + row_index * (panel_h + gap_y)
            x1 = x0 + panel_w
            y1 = y0 + panel_h
            panel = grouped.get((hop, rep), {})

            values = [
                value
                for points in panel.values()
                for _, value in points
                if value > 0
            ]
            if values:
                y_min = min(values)
                y_max = max(values)
            else:
                y_min, y_max = 1.0, 10.0
            if y_min == y_max:
                y_min *= 0.9
                y_max *= 1.1
            y_min_log = math.log10(max(y_min * 0.95, 1.0))
            y_max_log = math.log10(max(y_max * 1.05, 10.0))

            def sx(iteration: int) -> float:
                return x0 + (iteration - x_min) / (x_max - x_min) * panel_w

            def sy(value: float) -> float:
                value_log = math.log10(max(value, 1.0))
                return y1 - (value_log - y_min_log) / (y_max_log - y_min_log) * panel_h

            svg.append(f'<rect x="{x0:.2f}" y="{y0:.2f}" width="{panel_w:.2f}" height="{panel_h:.2f}" fill="white" stroke="#aaa"/>')
            svg.append(f'<text x="{(x0 + x1) / 2:.2f}" y="{y0 - 10:.2f}" text-anchor="middle" class="title">Hop={hop}, Rep={rep}</text>')

            for tick in range(x_min, x_max + 1):
                tx = sx(tick)
                svg.append(f'<line x1="{tx:.2f}" y1="{y1:.2f}" x2="{tx:.2f}" y2="{y1 + 4:.2f}" class="axis"/>')
                if tick in {x_min, x_max} or tick % 2 == 0:
                    svg.append(f'<text x="{tx:.2f}" y="{y1 + 18:.2f}" text-anchor="middle" class="tick">{tick}</text>')

            for frac in [0.0, 0.25, 0.5, 0.75, 1.0]:
                ty = y1 - frac * panel_h
                value = 10 ** (y_min_log + frac * (y_max_log - y_min_log))
                svg.append(f'<line x1="{x0:.2f}" y1="{ty:.2f}" x2="{x1:.2f}" y2="{ty:.2f}" class="grid"/>')
                svg.append(f'<text x="{x0 - 8:.2f}" y="{ty + 4:.2f}" text-anchor="end" class="tick">{nice_number(value)}</text>')

            svg.append(f'<line x1="{x0:.2f}" y1="{y1:.2f}" x2="{x1:.2f}" y2="{y1:.2f}" class="axis"/>')
            svg.append(f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{x0:.2f}" y2="{y1:.2f}" class="axis"/>')

            for algorithm in algorithms_sorted:
                points = sorted(panel.get(algorithm, []))
                if not points:
                    continue
                coords = [(sx(iteration), sy(value)) for iteration, value in points]
                color = COLORS.get(algorithm, "#555")
                svg.append(f'<polyline points="{polyline(coords)}" class="line" stroke="{color}"/>')
                for x, y in coords:
                    svg.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="3.2" fill="{color}"/>')

    svg.append(f'<text x="{width / 2:.1f}" y="{height - 20}" text-anchor="middle" class="label">Iteration</text>')
    svg.append(f'<text x="20" y="{height / 2:.1f}" transform="rotate(-90 20 {height / 2:.1f})" text-anchor="middle" class="label">Total travel time, log scale</text>')
    svg.append("</svg>")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(svg) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input",
        default=str(RESULT_DIR / "mh_synthetic_seed0.csv"),
        help="CSV generated by run_mh_synthetic_experiments.py.",
    )
    parser.add_argument(
        "--output",
        default=str(RESULT_DIR / "mh_synthetic_seed0.svg"),
        help="Output SVG path.",
    )
    args = parser.parse_args()

    rows = load_series_rows(Path(args.input))
    draw_svg(rows, Path(args.output))
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
