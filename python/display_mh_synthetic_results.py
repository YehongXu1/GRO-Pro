#!/usr/bin/env python3
import argparse
import csv
from collections import defaultdict
from pathlib import Path

try:
    import matplotlib.pyplot as plt
except ModuleNotFoundError:
    plt = None
    from PIL import Image, ImageDraw, ImageFont


METHOD_LABELS = {
    "gro": "GRO",
    "gro_baseline": "GRO baseline",
}


def read_rows(path):
    rows = []
    with path.open(newline="", encoding="utf-8") as file:
        for row in csv.DictReader(file):
            if not row["total_travel_time"]:
                continue
            item = dict(row)
            item["hop"] = int(row["hop"])
            item["rep"] = int(row["rep"])
            item["seed"] = int(row["seed"])
            item["iteration"] = int(row["iteration"])
            item["total_travel_time"] = float(row["total_travel_time"])
            item["selected_count"] = float(row["selected_count"] or 0)
            item["candidate_count"] = float(row["candidate_count"] or 0)
            item["reroute_count"] = float(row["reroute_count"] or 0)
            rows.append(item)
    return rows


def average_by_iteration(rows):
    grouped = defaultdict(list)
    for row in rows:
        key = (row["method"], row["hop"], row["rep"], row["iteration"])
        grouped[key].append(row)

    averaged = []
    for (method, hop, rep, iteration), items in grouped.items():
        averaged.append({
            "method": method,
            "hop": hop,
            "rep": rep,
            "iteration": iteration,
            "total_travel_time": sum(x["total_travel_time"] for x in items) / len(items),
            "selected_count": sum(x["selected_count"] for x in items) / len(items),
            "candidate_count": sum(x["candidate_count"] for x in items) / len(items),
            "reroute_count": sum(x["reroute_count"] for x in items) / len(items),
            "seed_count": len(items),
        })
    return averaged


def plot_total_travel_time(rows, output_path):
    if plt is None:
        plot_total_travel_time_pil(rows, output_path)
        return

    rows = [row for row in rows if row["iteration"] >= 0]
    hops = sorted({row["hop"] for row in rows})
    reps = sorted({row["rep"] for row in rows})

    fig, axes = plt.subplots(
        len(hops),
        len(reps),
        figsize=(4.4 * len(reps), 3.2 * len(hops)),
        sharex=True,
        squeeze=False,
    )

    colors = {
        "gro": "#1f77b4",
        "gro_baseline": "#ff7f0e",
    }
    markers = {
        "gro": "o",
        "gro_baseline": "s",
    }

    for row_idx, hop in enumerate(hops):
        for col_idx, rep in enumerate(reps):
            ax = axes[row_idx][col_idx]
            panel_rows = [row for row in rows if row["hop"] == hop and row["rep"] == rep]
            for method in sorted({row["method"] for row in panel_rows}):
                method_rows = sorted(
                    [row for row in panel_rows if row["method"] == method],
                    key=lambda x: x["iteration"],
                )
                if not method_rows:
                    continue
                ax.plot(
                    [row["iteration"] for row in method_rows],
                    [row["total_travel_time"] for row in method_rows],
                    label=METHOD_LABELS.get(method, method),
                    color=colors.get(method),
                    marker=markers.get(method, "o"),
                    linewidth=1.8,
                    markersize=4,
                )

            ax.set_title(f"Hop={hop}, Rep={rep}")
            ax.grid(True, linestyle="--", linewidth=0.5, alpha=0.4)
            ax.ticklabel_format(axis="y", style="sci", scilimits=(0, 0))
            if row_idx == len(hops) - 1:
                ax.set_xlabel("Iteration")
            if col_idx == 0:
                ax.set_ylabel("Total travel time")

    handles, labels = axes[0][0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=max(1, len(labels)))
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    fig.savefig(output_path, dpi=200)
    print(f"[figure] {output_path}")


def plot_selection_counts(rows, output_path):
    if plt is None:
        plot_selection_counts_pil(rows, output_path)
        return

    rows = [row for row in rows if row["iteration"] >= 0]
    gro_rows = [row for row in rows if row["method"] == "gro"]
    if not gro_rows:
        return

    hops = sorted({row["hop"] for row in gro_rows})
    reps = sorted({row["rep"] for row in gro_rows})
    fig, axes = plt.subplots(
        len(hops),
        len(reps),
        figsize=(4.4 * len(reps), 3.2 * len(hops)),
        sharex=True,
        squeeze=False,
    )

    for row_idx, hop in enumerate(hops):
        for col_idx, rep in enumerate(reps):
            ax = axes[row_idx][col_idx]
            panel_rows = sorted(
                [row for row in gro_rows if row["hop"] == hop and row["rep"] == rep],
                key=lambda x: x["iteration"],
            )
            ax.plot(
                [row["iteration"] for row in panel_rows],
                [row["candidate_count"] for row in panel_rows],
                label="candidate",
                marker="o",
                linewidth=1.6,
            )
            ax.plot(
                [row["iteration"] for row in panel_rows],
                [row["selected_count"] for row in panel_rows],
                label="selected",
                marker="s",
                linewidth=1.6,
            )
            ax.set_title(f"Hop={hop}, Rep={rep}")
            ax.grid(True, linestyle="--", linewidth=0.5, alpha=0.4)
            if row_idx == len(hops) - 1:
                ax.set_xlabel("Iteration")
            if col_idx == 0:
                ax.set_ylabel("Query count")

    handles, labels = axes[0][0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=max(1, len(labels)))
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    fig.savefig(output_path, dpi=200)
    print(f"[figure] {output_path}")


def nice_number(value):
    if value >= 1_000_000:
        return f"{value / 1_000_000:.1f}M"
    if value >= 1_000:
        return f"{value / 1_000:.0f}K"
    return f"{value:.0f}"


def draw_text(draw, xy, text, fill=(30, 30, 30), font=None, anchor=None):
    draw.text(xy, text, fill=fill, font=font, anchor=anchor)


def plot_panel(draw, box, series, title, y_label, font, small_font):
    left, top, right, bottom = box
    margin_left = 54
    margin_right = 16
    margin_top = 28
    margin_bottom = 38

    plot_left = left + margin_left
    plot_top = top + margin_top
    plot_right = right - margin_right
    plot_bottom = bottom - margin_bottom

    draw.rectangle([plot_left, plot_top, plot_right, plot_bottom], outline=(170, 170, 170))
    draw_text(draw, ((left + right) / 2, top + 8), title, font=font, anchor="ma")
    draw_text(draw, (left + 6, (plot_top + plot_bottom) / 2), y_label, font=small_font)

    all_points = []
    for points, _, _ in series:
        all_points.extend(points)
    if not all_points:
        return

    min_x = min(x for x, _ in all_points)
    max_x = max(x for x, _ in all_points)
    min_y = min(y for _, y in all_points)
    max_y = max(y for _, y in all_points)
    if min_x == max_x:
        max_x += 1
    if min_y == max_y:
        max_y += 1

    y_padding = (max_y - min_y) * 0.08
    min_y -= y_padding
    max_y += y_padding

    def sx(x):
        return plot_left + (x - min_x) * (plot_right - plot_left) / (max_x - min_x)

    def sy(y):
        return plot_bottom - (y - min_y) * (plot_bottom - plot_top) / (max_y - min_y)

    for i in range(1, 4):
        y = plot_top + i * (plot_bottom - plot_top) / 4
        draw.line([plot_left, y, plot_right, y], fill=(230, 230, 230))

    for value in [min_y, max_y]:
        draw_text(draw, (plot_left - 6, sy(value)), nice_number(value), font=small_font, anchor="ra")

    for value in [min_x, max_x]:
        draw_text(draw, (sx(value), plot_bottom + 8), str(int(value)), font=small_font, anchor="ma")

    for points, color, label in series:
        if len(points) >= 2:
            draw.line([(sx(x), sy(y)) for x, y in points], fill=color, width=3)
        for x, y in points:
            px, py = sx(x), sy(y)
            draw.ellipse([px - 3, py - 3, px + 3, py + 3], fill=color)


def save_pil_grid(rows, output_path, mode):
    rows = [row for row in rows if row["iteration"] >= 0]
    hops = sorted({row["hop"] for row in rows})
    reps = sorted({row["rep"] for row in rows})
    panel_width = 430
    panel_height = 300
    legend_height = 48
    image = Image.new("RGB", (panel_width * len(reps), panel_height * len(hops) + legend_height), "white")
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()
    small_font = ImageFont.load_default()

    if mode == "travel":
        legend = [
            ("GRO", (31, 119, 180)),
            ("GRO baseline", (255, 127, 14)),
        ]
    else:
        legend = [
            ("candidate", (31, 119, 180)),
            ("selected", (255, 127, 14)),
        ]

    x = 16
    for label, color in legend:
        draw.line([x, 20, x + 28, 20], fill=color, width=4)
        draw.ellipse([x + 11, 15, x + 17, 21], fill=color)
        draw_text(draw, (x + 36, 14), label, font=font)
        x += 150

    for row_idx, hop in enumerate(hops):
        for col_idx, rep in enumerate(reps):
            left = col_idx * panel_width
            top = legend_height + row_idx * panel_height
            box = (left, top, left + panel_width, top + panel_height)
            panel_rows = [row for row in rows if row["hop"] == hop and row["rep"] == rep]

            if mode == "travel":
                series = []
                for method, color in [("gro", (31, 119, 180)), ("gro_baseline", (255, 127, 14))]:
                    method_rows = sorted(
                        [row for row in panel_rows if row["method"] == method],
                        key=lambda row: row["iteration"],
                    )
                    points = [(row["iteration"], row["total_travel_time"]) for row in method_rows]
                    if points:
                        series.append((points, color, method))
                y_label = "Travel time"
            else:
                gro_rows = sorted(
                    [row for row in panel_rows if row["method"] == "gro"],
                    key=lambda row: row["iteration"],
                )
                series = [
                    ([(row["iteration"], row["candidate_count"]) for row in gro_rows], (31, 119, 180), "candidate"),
                    ([(row["iteration"], row["selected_count"]) for row in gro_rows], (255, 127, 14), "selected"),
                ]
                y_label = "Query count"

            plot_panel(
                draw,
                box,
                series,
                f"Hop={hop}, Rep={rep}",
                y_label,
                font,
                small_font,
            )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path)
    print(f"[figure] {output_path}")


def plot_total_travel_time_pil(rows, output_path):
    save_pil_grid(rows, output_path, "travel")


def plot_selection_counts_pil(rows, output_path):
    save_pil_grid(rows, output_path, "selection")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", default=Path("tmp/mh_synthetic_results/mh_synthetic_results.csv"), type=Path)
    parser.add_argument("--output-dir", default=Path("tmp/mh_synthetic_results"), type=Path)
    args = parser.parse_args()

    rows = average_by_iteration(read_rows(args.csv))
    args.output_dir.mkdir(parents=True, exist_ok=True)
    plot_total_travel_time(rows, args.output_dir / "mh_synthetic_total_travel_time.png")
    plot_selection_counts(rows, args.output_dir / "mh_synthetic_candidate_selected.png")


if __name__ == "__main__":
    main()
