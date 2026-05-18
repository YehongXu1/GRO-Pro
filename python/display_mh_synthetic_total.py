#!/usr/bin/env python3
"""Plot mean total travel time from an existing MH synthetic C++ result CSV."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
RESULT_DIR = ROOT / "python" / "results"

ALIASES = {
    "gro": "tdg",
    "random_baseline": "baseline",
    "selection_td_baseline": "tdg_selection_baseline",
    "normal_selection_gro_reroute": "tdg_reroute_baseline",
}

ORDER = [
    "tdg",
    "baseline",
    "tdg_selection_baseline",
    "tdg_reroute_baseline",
]

LABELS = {
    "tdg": "tdg",
    "baseline": "baseline",
    "tdg_selection_baseline": "tdg selection + baseline reroute",
    "tdg_reroute_baseline": "baseline selection + tdg reroute",
}

COLORS = {
    "tdg": (31, 119, 180),
    "baseline": (214, 39, 40),
    "tdg_selection_baseline": (44, 160, 44),
    "tdg_reroute_baseline": (148, 103, 189),
}


def font(size: int, bold: bool = False):
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


def read_rows(path: Path):
    raw = defaultdict(lambda: defaultdict(dict))
    meta = {}
    with path.open(newline="", encoding="utf-8") as file:
        for row in csv.DictReader(file):
            algorithm = ALIASES.get(row["algorithm"], row["algorithm"])
            if algorithm not in ORDER:
                continue
            dataset = row["dataset"]
            iteration = int(row["iteration"])
            raw[dataset][algorithm][iteration] = float(row["total_travel_time"])
            meta[dataset] = (int(row["hop"]), int(row["rep"]), int(row["seed"]))
    return raw, meta


def aggregate(raw, meta):
    values = defaultdict(list)
    panel_seeds = defaultdict(set)
    skipped = 0
    for dataset, by_algorithm in raw.items():
        if not all(algorithm in by_algorithm for algorithm in ORDER):
            skipped += 1
            continue
        common_iterations = set(by_algorithm[ORDER[0]])
        for algorithm in ORDER[1:]:
            common_iterations &= set(by_algorithm[algorithm])
        if not common_iterations:
            skipped += 1
            continue
        hop, rep, seed = meta[dataset]
        panel_seeds[(hop, rep)].add(seed)
        for algorithm in ORDER:
            for iteration in common_iterations:
                values[(hop, rep, algorithm, iteration)].append(by_algorithm[algorithm][iteration])

    rows = []
    for (hop, rep, algorithm, iteration), totals in sorted(values.items()):
        rows.append(
            {
                "hop": hop,
                "rep": rep,
                "algorithm": algorithm,
                "iteration": iteration,
                "seed_count": len(totals),
                "mean_total_travel_time": sum(totals) / len(totals),
            }
        )
    return rows, panel_seeds, skipped


def write_summary(rows, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(
            file,
            fieldnames=["hop", "rep", "algorithm", "iteration", "seed_count", "mean_total_travel_time"],
        )
        writer.writeheader()
        writer.writerows(rows)


def draw_line(draw, points, color):
    if len(points) >= 2:
        draw.line(points, fill=color, width=3)
    for x, y in points:
        draw.ellipse((x - 3.5, y - 3.5, x + 3.5, y + 3.5), fill=color)


def plot(rows, panel_seeds, output: Path) -> None:
    hops = sorted({row["hop"] for row in rows})
    reps = sorted({row["rep"] for row in rows})
    grouped = defaultdict(list)
    for row in rows:
        grouped[(row["hop"], row["rep"], row["algorithm"])].append(row)

    width, height = 1700, 1120
    image = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(image)

    title_font = font(24, True)
    panel_font = font(17, True)
    label_font = font(15)
    small_font = font(13)

    draw.text((width / 2, 28), "MH Synthetic: mean total travel time", fill=(25, 25, 25), font=title_font, anchor="ma")

    legend_x, legend_y = 95, 70
    for algorithm in ORDER:
        color = COLORS[algorithm]
        draw.line((legend_x, legend_y, legend_x + 34, legend_y), fill=color, width=4)
        draw.ellipse((legend_x + 13, legend_y - 4, legend_x + 21, legend_y + 4), fill=color)
        draw.text((legend_x + 43, legend_y), LABELS[algorithm], fill=(35, 35, 35), font=label_font, anchor="lm")
        legend_x += 360

    margin_left, margin_right = 96, 42
    margin_top, margin_bottom = 112, 78
    gap_x, gap_y = 58, 70
    panel_w = (width - margin_left - margin_right - gap_x * (len(reps) - 1)) / len(reps)
    panel_h = (height - margin_top - margin_bottom - gap_y * (len(hops) - 1)) / len(hops)

    all_iterations = [row["iteration"] for row in rows]
    x_min, x_max = min(all_iterations), max(all_iterations)
    if x_min == x_max:
        x_max += 1

    for row_index, hop in enumerate(hops):
        for col_index, rep in enumerate(reps):
            left = margin_left + col_index * (panel_w + gap_x)
            top = margin_top + row_index * (panel_h + gap_y)
            right = left + panel_w
            bottom = top + panel_h

            panel_rows = [
                row
                for row in rows
                if row["hop"] == hop and row["rep"] == rep
            ]
            y_values = [row["mean_total_travel_time"] for row in panel_rows]
            y_min = min(y_values)
            y_max = max(y_values)
            padding = max((y_max - y_min) * 0.08, 1.0)
            y_min -= padding
            y_max += padding

            def sx(iteration: int) -> float:
                return left + (iteration - x_min) / (x_max - x_min) * panel_w

            def sy(value: float) -> float:
                return bottom - (value - y_min) / (y_max - y_min) * panel_h

            draw.rectangle((left, top, right, bottom), outline=(155, 155, 155), width=1)
            draw.text(
                ((left + right) / 2, top - 20),
                f"Hop={hop}, Rep={rep}, n={len(panel_seeds[(hop, rep)])}",
                fill=(25, 25, 25),
                font=panel_font,
                anchor="mm",
            )

            for fraction in [0.0, 0.25, 0.5, 0.75, 1.0]:
                value = y_min + fraction * (y_max - y_min)
                y = sy(value)
                draw.line((left, y, right, y), fill=(226, 226, 226), width=1)
                draw.text((left - 8, y), nice_number(value), fill=(55, 55, 55), font=small_font, anchor="rm")

            for tick in range(x_min, x_max + 1):
                x = sx(tick)
                draw.line((x, bottom, x, bottom + 5), fill=(60, 60, 60), width=1)
                if tick == x_min or tick == x_max or tick % 2 == 0:
                    draw.text((x, bottom + 18), str(tick), fill=(55, 55, 55), font=small_font, anchor="mm")

            for algorithm in ORDER:
                points = sorted(grouped[(hop, rep, algorithm)], key=lambda item: item["iteration"])
                coords = [(sx(item["iteration"]), sy(item["mean_total_travel_time"])) for item in points]
                draw_line(draw, coords, COLORS[algorithm])

    draw.text((width / 2, height - 35), "Iteration", fill=(30, 30, 30), font=label_font, anchor="mm")
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", default=str(RESULT_DIR / "mh_synthetic_all_cpp.csv"))
    parser.add_argument("--output", default=str(RESULT_DIR / "mh_synthetic_all_cpp_mean_total.png"))
    parser.add_argument("--summary-output", default=str(RESULT_DIR / "mh_synthetic_all_cpp_mean_total.csv"))
    args = parser.parse_args()

    raw, meta = read_rows(Path(args.input))
    rows, panel_seeds, skipped = aggregate(raw, meta)
    write_summary(rows, Path(args.summary_output))
    plot(rows, panel_seeds, Path(args.output))

    print(f"datasets: {len(raw)}, skipped incomplete: {skipped}")
    print(f"wrote {args.output}")
    print(f"wrote {args.summary_output}")


if __name__ == "__main__":
    main()
