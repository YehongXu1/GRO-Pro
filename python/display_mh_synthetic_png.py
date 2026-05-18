#!/usr/bin/env python3
"""Create a PNG figure for MH synthetic experiment results using Pillow."""

from __future__ import annotations

import argparse
import math
import sys
from collections import defaultdict
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
from display_mh_synthetic import load_series_rows  # noqa: E402


ROOT = Path(__file__).resolve().parents[1]
RESULT_DIR = ROOT / "python" / "results"
COLORS = {
    "gro": (31, 119, 180),
    "baseline": (214, 39, 40),
    "random_baseline": (214, 39, 40),
    "selection_td_baseline": (44, 160, 44),
    "normal_selection_gro_reroute": (148, 103, 189),
}


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = [
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf" if bold else "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    ]
    for path in candidates:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            pass
    return ImageFont.load_default()


def nice_number(value: float) -> str:
    if value >= 1_000_000:
        return f"{value / 1_000_000:.1f}M"
    if value >= 1_000:
        return f"{value / 1_000:.0f}K"
    return f"{value:.0f}"


def draw_polyline(draw: ImageDraw.ImageDraw, points: list[tuple[float, float]], color: tuple[int, int, int]) -> None:
    if len(points) >= 2:
        draw.line(points, fill=color, width=3, joint="curve")
    for x, y in points:
        draw.ellipse((x - 4, y - 4, x + 4, y + 4), fill=color)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", default=str(RESULT_DIR / "mh_synthetic_seed0.csv"))
    parser.add_argument("--output", default=str(RESULT_DIR / "mh_synthetic_seed0.png"))
    args = parser.parse_args()

    rows = load_series_rows(Path(args.input))
    grouped: dict[tuple[int, int], dict[str, list[tuple[int, float]]]] = defaultdict(lambda: defaultdict(list))
    hops: set[int] = set()
    reps: set[int] = set()
    algorithms: set[str] = set()

    for row in rows:
        if not row.get("total_travel_time"):
            continue
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
    algorithms_sorted = [a for a in preferred_order if a in algorithms]

    width, height = 1600, 1080
    image = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(image)
    title_font = font(20, True)
    small_font = font(14)
    label_font = font(16)

    margin_left, margin_right = 90, 40
    margin_top, margin_bottom = 95, 80
    gap_x, gap_y = 60, 70
    panel_w = (width - margin_left - margin_right - gap_x * (len(reps_sorted) - 1)) / len(reps_sorted)
    panel_h = (height - margin_top - margin_bottom - gap_y * (len(hops_sorted) - 1)) / len(hops_sorted)

    all_iterations = [it for algs in grouped.values() for pts in algs.values() for it, _ in pts]
    x_min, x_max = min(all_iterations), max(all_iterations)

    draw.text((width / 2, 26), "MH Synthetic: GRO vs GRO Baseline", fill=(30, 30, 30), font=title_font, anchor="mm")
    legend_x, legend_y = margin_left, 62
    for algorithm in algorithms_sorted:
        color = COLORS.get(algorithm, (80, 80, 80))
        draw.line((legend_x, legend_y, legend_x + 35, legend_y), fill=color, width=4)
        draw.ellipse((legend_x + 14, legend_y - 4, legend_x + 22, legend_y + 4), fill=color)
        draw.text((legend_x + 45, legend_y), algorithm, fill=(30, 30, 30), font=label_font, anchor="lm")
        legend_x += 180

    for row_index, hop in enumerate(hops_sorted):
        for col_index, rep in enumerate(reps_sorted):
            x0 = margin_left + col_index * (panel_w + gap_x)
            y0 = margin_top + row_index * (panel_h + gap_y)
            x1, y1 = x0 + panel_w, y0 + panel_h
            panel = grouped[(hop, rep)]
            values = [value for pts in panel.values() for _, value in pts if value > 0]
            y_min, y_max = min(values), max(values)
            y_min_log = math.log10(max(y_min * 0.95, 1.0))
            y_max_log = math.log10(max(y_max * 1.05, 10.0))

            def sx(iteration: int) -> float:
                return x0 + (iteration - x_min) / (x_max - x_min) * panel_w

            def sy(value: float) -> float:
                value_log = math.log10(max(value, 1.0))
                return y1 - (value_log - y_min_log) / (y_max_log - y_min_log) * panel_h

            draw.rectangle((x0, y0, x1, y1), outline=(150, 150, 150), width=1)
            draw.text(((x0 + x1) / 2, y0 - 18), f"Hop={hop}, Rep={rep}", fill=(30, 30, 30), font=label_font, anchor="mm")

            for frac in [0, 0.25, 0.5, 0.75, 1]:
                ty = y1 - frac * panel_h
                value = 10 ** (y_min_log + frac * (y_max_log - y_min_log))
                draw.line((x0, ty, x1, ty), fill=(225, 225, 225), width=1)
                draw.text((x0 - 8, ty), nice_number(value), fill=(50, 50, 50), font=small_font, anchor="rm")

            for tick in range(x_min, x_max + 1):
                tx = sx(tick)
                draw.line((tx, y1, tx, y1 + 5), fill=(50, 50, 50), width=1)
                if tick in {x_min, x_max} or tick % 2 == 0:
                    draw.text((tx, y1 + 18), str(tick), fill=(50, 50, 50), font=small_font, anchor="mm")

            for algorithm in algorithms_sorted:
                points = sorted(panel.get(algorithm, []))
                coords = [(sx(iteration), sy(value)) for iteration, value in points]
                draw_polyline(draw, coords, COLORS.get(algorithm, (80, 80, 80)))

    draw.text((width / 2, height - 32), "Iteration", fill=(30, 30, 30), font=label_font, anchor="mm")
    image.save(args.output)
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
