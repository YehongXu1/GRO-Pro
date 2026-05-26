#!/usr/bin/env python3
import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path
from statistics import mean, pstdev


METHODS = [
    {
        "key": "full",
        "label": "Full candidates",
        "color": "#8F6CCF",
        "marker": "circle",
        "default_dir": "python/results/experiments/exp3_compression_scalability/peak1h/tmp_gro_scalability_peak1h_tdg_excess_full_capacity2_cap10e8",
    },
    {
        "key": "score_top",
        "label": "Score pruning",
        "color": "#64BEE8",
        "marker": "square",
        "default_dir": "python/results/experiments/exp3_compression_scalability/peak1h_clean/candidate_score_top_conflict5000_capacity2_cap10e8",
    },
    {
        "key": "score_top_compressed",
        "label": "Score pruning + compressed TDG",
        "color": "#E76F51",
        "marker": "triangle",
        "default_dir": "python/results/experiments/exp3_compression_scalability/peak1h_clean/candidate_score_top_compressed_conflict5000_capacity2_cap10e8",
    },
]


def as_float(row, key, default=0.0):
    text = row.get(key, "")
    if text == "":
        return default
    return float(text)


def load_completed_dataset_metrics(method_key, input_dir, max_iterations):
    dataset_rows = defaultdict(list)
    for path in sorted(Path(input_dir).glob("rep*.csv")):
        with path.open(newline="") as file:
            reader = csv.DictReader(file)
            for row in reader:
                if int(row["iteration"]) >= max_iterations:
                    continue
                dataset_rows[row["dataset"]].append(row)

    metrics = []
    skipped = []
    for dataset, rows in sorted(dataset_rows.items()):
        rows = sorted(rows, key=lambda row: int(row["iteration"]))
        if len(rows) < max_iterations:
            skipped.append((dataset, len(rows)))
            continue
        rows = rows[:max_iterations]

        initial_ttt = as_float(rows[0], "total_before")
        after_ttts = [as_float(row, "total_after") for row in rows]
        best_ttt = min(after_ttts)
        final_ttt = after_ttts[-1]
        method_total_sec = sum(as_float(row, "method_total_sec") for row in rows)
        initial_routes_sec = sum(as_float(row, "initial_routes_sec") for row in rows)

        candidate_fractions = []
        for row in rows:
            value = as_float(row, "candidate_fraction", -1.0)
            if value >= 0:
                candidate_fractions.append(value)

        selected_counts = [as_float(row, "selected_count") for row in rows]
        batch_counts = [as_float(row, "batch_count") for row in rows]
        total_batches = sum(batch_counts)
        total_selected = sum(selected_counts)

        metrics.append(
            {
                "method": method_key,
                "dataset": dataset,
                "rep": int(rows[0]["rep"]),
                "seed": int(rows[0]["seed"]),
                "query_size": int(rows[0]["query_count"]),
                "initial_ttt": initial_ttt,
                "best_ttt": best_ttt,
                "final_ttt": final_ttt,
                "avg_final_travel_time": final_ttt / int(rows[0]["query_count"]),
                "best_ttt_reduction_pct": (initial_ttt - best_ttt) / initial_ttt * 100.0,
                "final_ttt_reduction_pct": (initial_ttt - final_ttt) / initial_ttt * 100.0,
                "ttt_fluctuation_range_pct": (max(after_ttts) - min(after_ttts)) / initial_ttt * 100.0,
                "runtime_sec": method_total_sec,
                "runtime_without_initial_sec": method_total_sec - initial_routes_sec,
                "initial_routes_sec": initial_routes_sec,
                "tdg_prepare_sec": sum(as_float(row, "tdg_prepare_sec") for row in rows),
                "select_sec": sum(as_float(row, "select_sec") for row in rows),
                "candidate_sec": sum(as_float(row, "candidate_sec") for row in rows),
                "normalize_sec": sum(as_float(row, "normalize_sec") for row in rows),
                "batch_sec": sum(as_float(row, "batch_sec") for row in rows),
                "reroute_sec": sum(as_float(row, "reroute_sec") for row in rows),
                "evaluate_sec": sum(
                    as_float(row, "evaluate_before_sec") + as_float(row, "evaluate_after_sec")
                    for row in rows
                ),
                "candidate_fraction_pct": (
                    mean(candidate_fractions) * 100.0 if candidate_fractions else 100.0
                ),
                "selected_fraction_pct": mean(as_float(row, "selected_fraction") for row in rows)
                * 100.0,
                "tdg_node_count": mean(as_float(row, "tdg_node_count") for row in rows),
                "tdg_edge_timeline_count": mean(
                    as_float(row, "tdg_edge_timeline_count") for row in rows
                ),
                "batch_count": mean(batch_counts),
                "avg_batch_size": total_selected / total_batches if total_batches else 0.0,
            }
        )
    return metrics, skipped


def summarize(metrics):
    grouped = defaultdict(list)
    for row in metrics:
        grouped[(row["method"], row["query_size"])].append(row)

    summary_rows = []
    metric_keys = [
        "initial_ttt",
        "best_ttt",
        "final_ttt",
        "avg_final_travel_time",
        "best_ttt_reduction_pct",
        "final_ttt_reduction_pct",
        "ttt_fluctuation_range_pct",
        "runtime_sec",
        "runtime_without_initial_sec",
        "initial_routes_sec",
        "tdg_prepare_sec",
        "select_sec",
        "candidate_sec",
        "normalize_sec",
        "batch_sec",
        "reroute_sec",
        "evaluate_sec",
        "candidate_fraction_pct",
        "selected_fraction_pct",
        "tdg_node_count",
        "tdg_edge_timeline_count",
        "batch_count",
        "avg_batch_size",
    ]
    for (method, query_size), rows in sorted(grouped.items(), key=lambda item: (item[0][0], item[0][1])):
        out = {
            "method": method,
            "query_size": query_size,
            "dataset_count": len(rows),
        }
        for key in metric_keys:
            values = [row[key] for row in rows]
            out[f"{key}_mean"] = mean(values)
            out[f"{key}_std"] = pstdev(values) if len(values) > 1 else 0.0
        summary_rows.append(out)
    return summary_rows


def write_csv(path, rows):
    if not rows:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = list(rows[0].keys())
    with path.open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def fmt(value, digits=3):
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def write_summary_csv(path, rows):
    rounded = []
    for row in rows:
        rounded.append({key: fmt(value) for key, value in row.items()})
    write_csv(path, rounded)


def value_by_method_size(summary_rows, method, query_size, metric):
    for row in summary_rows:
        if row["method"] == method and row["query_size"] == query_size:
            return row[metric]
    return None


def nice_log_ticks(min_value, max_value):
    if min_value <= 0:
        min_value = 1.0
    lo = math.floor(math.log10(min_value))
    hi = math.ceil(math.log10(max_value))
    ticks = []
    for exp in range(lo, hi + 1):
        for mult in [1, 2, 5]:
            value = mult * (10**exp)
            if min_value <= value <= max_value:
                ticks.append(value)
    return ticks


def nice_linear_ticks(min_value, max_value, count=5):
    if max_value <= min_value:
        return [min_value]
    raw_step = (max_value - min_value) / max(1, count - 1)
    exp = math.floor(math.log10(raw_step))
    base = raw_step / (10**exp)
    if base <= 1:
        nice_base = 1
    elif base <= 2:
        nice_base = 2
    elif base <= 5:
        nice_base = 5
    else:
        nice_base = 10
    step = nice_base * (10**exp)
    start = math.floor(min_value / step) * step
    end = math.ceil(max_value / step) * step
    ticks = []
    value = start
    while value <= end + step * 0.5:
        ticks.append(value)
        value += step
    return ticks


def svg_text(x, y, text, size=13, anchor="middle", weight="normal", color="#222"):
    return (
        f'<text x="{x:.1f}" y="{y:.1f}" font-family="Arial, Helvetica, sans-serif" '
        f'font-size="{size}" font-weight="{weight}" fill="{color}" '
        f'text-anchor="{anchor}">{text}</text>'
    )


def svg_marker(x, y, color, marker):
    if marker == "square":
        return f'<rect x="{x-4:.1f}" y="{y-4:.1f}" width="8" height="8" fill="{color}"/>'
    if marker == "triangle":
        return (
            f'<polygon points="{x:.1f},{y-5:.1f} {x-5:.1f},{y+4:.1f} '
            f'{x+5:.1f},{y+4:.1f}" fill="{color}"/>'
        )
    return f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.3" fill="{color}"/>'


def plot_panel(
    parts,
    summary_rows,
    x_values,
    metric,
    title,
    y_label,
    x0,
    y0,
    width,
    height,
    log_y=False,
    y_min=None,
    y_max=None,
):
    values = [
        row[f"{metric}_mean"]
        for row in summary_rows
        if row["query_size"] in x_values and row["method"] in {m["key"] for m in METHODS}
    ]
    if not values:
        return
    min_value = min(values) if y_min is None else y_min
    max_value = max(values) if y_max is None else y_max
    if log_y:
        min_value = max(1e-9, min_value)
        ticks = nice_log_ticks(min_value, max_value)
        scale_y = lambda value: y0 + height - (
            (math.log10(value) - math.log10(min_value))
            / (math.log10(max_value) - math.log10(min_value))
        ) * height
    else:
        pad = (max_value - min_value) * 0.08 if max_value > min_value else 1.0
        min_value = min_value - pad if y_min is None else y_min
        max_value = max_value + pad if y_max is None else y_max
        ticks = nice_linear_ticks(min_value, max_value)
        scale_y = lambda value: y0 + height - (value - min_value) / (max_value - min_value) * height

    x_min = min(x_values)
    x_max = max(x_values)
    scale_x = lambda value: x0 + (value - x_min) / (x_max - x_min) * width

    parts.append(svg_text(x0 + width / 2, y0 - 36, title, size=17, weight="bold"))
    parts.append(
        f'<rect x="{x0:.1f}" y="{y0:.1f}" width="{width:.1f}" height="{height:.1f}" '
        'fill="white" stroke="#222" stroke-width="1"/>'
    )
    for tick in ticks:
        if tick < min_value or tick > max_value:
            continue
        y = scale_y(tick)
        parts.append(
            f'<line x1="{x0:.1f}" y1="{y:.1f}" x2="{x0+width:.1f}" y2="{y:.1f}" '
            'stroke="#ddd" stroke-width="1"/>'
        )
        label = f"{tick:g}"
        if log_y and tick >= 1000:
            label = f"{tick/1000:g}k"
        parts.append(svg_text(x0 - 9, y + 4, label, size=11, anchor="end", color="#444"))

    for xv in x_values:
        x = scale_x(xv)
        parts.append(
            f'<line x1="{x:.1f}" y1="{y0+height:.1f}" x2="{x:.1f}" y2="{y0+height+5:.1f}" '
            'stroke="#222" stroke-width="1"/>'
        )
        parts.append(svg_text(x, y0 + height + 22, f"{xv//1000}k", size=12))

    parts.append(svg_text(x0 + width / 2, y0 + height + 48, "Number of queries", size=13))
    parts.append(
        f'<text x="{x0-62:.1f}" y="{y0+height/2:.1f}" '
        'font-family="Arial, Helvetica, sans-serif" font-size="13" fill="#222" '
        'text-anchor="middle" transform="rotate(-90 '
        f'{x0-62:.1f} {y0+height/2:.1f})">{y_label}</text>'
    )

    for method in METHODS:
        points = []
        for xv in x_values:
            value = value_by_method_size(summary_rows, method["key"], xv, f"{metric}_mean")
            if value is None:
                continue
            points.append((scale_x(xv), scale_y(value), xv, value))
        if not points:
            continue
        path_points = " ".join(f"{x:.1f},{y:.1f}" for x, y, _, _ in points)
        parts.append(
            f'<polyline points="{path_points}" fill="none" stroke="{method["color"]}" '
            'stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"/>'
        )
        for x, y, _, _ in points:
            parts.append(svg_marker(x, y, method["color"], method["marker"]))


def write_svg(path, summary_rows):
    x_values = sorted({row["query_size"] for row in summary_rows})
    width = 1160
    height = 500
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
    ]

    plot_panel(
        parts,
        summary_rows,
        x_values,
        "runtime_without_initial_sec",
        "(a) Running time",
        "Runtime, first 5 iterations (s)",
        92,
        82,
        430,
        300,
        log_y=True,
    )
    plot_panel(
        parts,
        summary_rows,
        x_values,
        "best_ttt_reduction_pct",
        "(b) Best TTT reduction",
        "Best TTT reduction (%)",
        674,
        82,
        430,
        300,
        log_y=False,
        y_min=84,
        y_max=100,
    )

    legend_x = 185
    legend_y = 455
    spacing = 300
    for index, method in enumerate(METHODS):
        x = legend_x + index * spacing
        parts.append(
            f'<line x1="{x:.1f}" y1="{legend_y:.1f}" x2="{x+34:.1f}" y2="{legend_y:.1f}" '
            f'stroke="{method["color"]}" stroke-width="2.6"/>'
        )
        parts.append(svg_marker(x + 17, legend_y, method["color"], method["marker"]))
        parts.append(svg_text(x + 45, legend_y + 4, method["label"], size=13, anchor="start"))

    parts.append("</svg>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(parts))


def write_png_from_svg_like(path, summary_rows):
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        return False

    scale = 2
    width, height = 1160, 500
    image = Image.new("RGB", (width * scale, height * scale), "white")
    draw = ImageDraw.Draw(image)

    def font(size, bold=False):
        candidates = [
            "/System/Library/Fonts/Supplemental/Arial Bold.ttf" if bold else "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
        ]
        for candidate in candidates:
            try:
                return ImageFont.truetype(candidate, size * scale)
            except Exception:
                continue
        return ImageFont.load_default()

    def text(x, y, value, size=13, anchor="mm", bold=False, fill="#222"):
        draw.text((x * scale, y * scale), value, fill=fill, font=font(size, bold), anchor=anchor)

    def rotated_text(x, y, value, size=13, fill="#222"):
        label_font = font(size)
        bbox = draw.textbbox((0, 0), value, font=label_font)
        label_w = bbox[2] - bbox[0]
        label_h = bbox[3] - bbox[1]
        label = Image.new("RGBA", (label_w + 8 * scale, label_h + 8 * scale), (255, 255, 255, 0))
        label_draw = ImageDraw.Draw(label)
        label_draw.text((4 * scale, 4 * scale), value, fill=fill, font=label_font)
        rotated = label.rotate(90, expand=True)
        image.paste(
            rotated,
            (
                int(x * scale - rotated.size[0] / 2),
                int(y * scale - rotated.size[1] / 2),
            ),
            rotated,
        )

    def line(coords, fill, width_px=1):
        draw.line([(x * scale, y * scale) for x, y in coords], fill=fill, width=width_px * scale)

    def marker(x, y, color, marker_name):
        x *= scale
        y *= scale
        if marker_name == "square":
            draw.rectangle([x - 5 * scale, y - 5 * scale, x + 5 * scale, y + 5 * scale], fill=color)
        elif marker_name == "triangle":
            draw.polygon(
                [(x, y - 6 * scale), (x - 6 * scale, y + 5 * scale), (x + 6 * scale, y + 5 * scale)],
                fill=color,
            )
        else:
            draw.ellipse([x - 5 * scale, y - 5 * scale, x + 5 * scale, y + 5 * scale], fill=color)

    def panel(metric, title, ylabel, x0, y0, w, h, log_y=False, y_min=None, y_max=None):
        x_values = sorted({row["query_size"] for row in summary_rows})
        values = [row[f"{metric}_mean"] for row in summary_rows]
        min_value = min(values) if y_min is None else y_min
        max_value = max(values) if y_max is None else y_max
        if log_y:
            min_value = max(1e-9, min_value)
            ticks = nice_log_ticks(min_value, max_value)
            sy = lambda value: y0 + h - (
                (math.log10(value) - math.log10(min_value))
                / (math.log10(max_value) - math.log10(min_value))
            ) * h
        else:
            ticks = nice_linear_ticks(min_value, max_value)
            sy = lambda value: y0 + h - (value - min_value) / (max_value - min_value) * h
        sx = lambda value: x0 + (value - min(x_values)) / (max(x_values) - min(x_values)) * w

        text(x0 + w / 2, y0 - 36, title, size=17, bold=True)
        draw.rectangle(
            [x0 * scale, y0 * scale, (x0 + w) * scale, (y0 + h) * scale],
            outline="#222",
            width=scale,
        )
        for tick in ticks:
            if tick < min_value or tick > max_value:
                continue
            y = sy(tick)
            line([(x0, y), (x0 + w, y)], "#dddddd")
            label = f"{tick:g}"
            if log_y and tick >= 1000:
                label = f"{tick/1000:g}k"
            text(x0 - 9, y + 1, label, size=11, anchor="rm", fill="#444")
        for xv in x_values:
            x = sx(xv)
            line([(x, y0 + h), (x, y0 + h + 5)], "#222")
            text(x, y0 + h + 22, f"{xv//1000}k", size=12)
        text(x0 + w / 2, y0 + h + 48, "Number of queries", size=13)
        rotated_text(x0 - 66, y0 + h / 2, ylabel, size=13)
        for method in METHODS:
            pts = []
            for xv in x_values:
                value = value_by_method_size(summary_rows, method["key"], xv, f"{metric}_mean")
                if value is not None:
                    pts.append((sx(xv), sy(value)))
            if not pts:
                continue
            line(pts, method["color"], 3)
            for x, y in pts:
                marker(x, y, method["color"], method["marker"])

    panel(
        "runtime_without_initial_sec",
        "(a) Running time",
        "Runtime, first 5 iterations (s)",
        92,
        82,
        430,
        300,
        log_y=True,
    )
    panel(
        "best_ttt_reduction_pct",
        "(b) Best TTT reduction",
        "Best TTT reduction (%)",
        674,
        82,
        430,
        300,
        y_min=84,
        y_max=100,
    )
    legend_x, legend_y, spacing = 185, 455, 300
    for index, method in enumerate(METHODS):
        x = legend_x + index * spacing
        line([(x, legend_y), (x + 34, legend_y)], method["color"], 3)
        marker(x + 17, legend_y, method["color"], method["marker"])
        text(x + 45, legend_y + 1, method["label"], size=13, anchor="lm")

    path.parent.mkdir(parents=True, exist_ok=True)
    if path.suffix.lower() == ".pdf":
        image.save(path, "PDF", resolution=300.0)
    else:
        image.save(path)
    return True


def write_mock_style_png(path, summary_rows):
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        return False

    method_keys = ["score_top", "score_top_compressed"]
    query_sizes = sorted(
        {
            row["query_size"]
            for row in summary_rows
            if row["method"] in method_keys
        }
    )
    query_sizes = [
        size
        for size in query_sizes
        if all(
            value_by_method_size(summary_rows, method, size, "tdg_node_count_mean")
            is not None
            for method in method_keys
        )
    ]
    if not query_sizes:
        return False

    scale = 2
    width, height = 2500, 900
    image = Image.new("RGB", (width * scale, height * scale), "white")
    draw = ImageDraw.Draw(image)

    def font(size, bold=False):
        candidates = [
            "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
            if bold
            else "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
        ]
        for candidate in candidates:
            try:
                return ImageFont.truetype(candidate, size * scale)
            except Exception:
                continue
        return ImageFont.load_default()

    def text(x, y, value, size=18, anchor="mm", bold=False, fill="#222"):
        draw.text(
            (x * scale, y * scale),
            value,
            fill=fill,
            font=font(size, bold),
            anchor=anchor,
        )

    def rotated_text(x, y, value, size=18, fill="#222"):
        label_font = font(size)
        bbox = draw.textbbox((0, 0), value, font=label_font)
        label_w = bbox[2] - bbox[0]
        label_h = bbox[3] - bbox[1]
        label = Image.new(
            "RGBA",
            (label_w + 10 * scale, label_h + 10 * scale),
            (255, 255, 255, 0),
        )
        label_draw = ImageDraw.Draw(label)
        label_draw.text((5 * scale, 5 * scale), value, fill=fill, font=label_font)
        rotated = label.rotate(90, expand=True)
        image.paste(
            rotated,
            (
                int(x * scale - rotated.size[0] / 2),
                int(y * scale - rotated.size[1] / 2),
            ),
            rotated,
        )

    def line(points, fill, width_px=2):
        draw.line(
            [(x * scale, y * scale) for x, y in points],
            fill=fill,
            width=width_px * scale,
        )

    def rect(x0, y0, x1, y1, fill, outline=None, width_px=1):
        draw.rectangle(
            [x0 * scale, y0 * scale, x1 * scale, y1 * scale],
            fill=fill,
            outline=outline,
            width=width_px * scale,
        )

    def marker(x, y, color, marker_name, size_px=8):
        x *= scale
        y *= scale
        s = size_px * scale
        if marker_name == "triangle":
            draw.polygon([(x, y - s), (x - s, y + s), (x + s, y + s)], fill=color)
        else:
            draw.ellipse([x - s, y - s, x + s, y + s], fill=color)

    def metric(method, query_size, key):
        return value_by_method_size(summary_rows, method, query_size, key)

    def fmt_millions(value):
        if value >= 1_000_000:
            return f"{value / 1_000_000:.1f}M"
        if value >= 1000:
            return f"{value / 1000:.0f}k"
        return f"{value:g}"

    def linear_scale(values, y0, h, pad=0.08, forced_min=None, forced_max=None):
        lo = min(values) if forced_min is None else forced_min
        hi = max(values) if forced_max is None else forced_max
        if hi <= lo:
            hi = lo + 1
        margin = (hi - lo) * pad
        if forced_min is None:
            lo -= margin
        if forced_max is None:
            hi += margin
        return lo, hi, lambda value: y0 + h - (value - lo) / (hi - lo) * h

    def draw_axes(x0, y0, w, h, title, left_label, right_label=None):
        text(x0 + w / 2, y0 - 24, title, size=22)
        rect(x0, y0, x0 + w, y0 + h, "white", "#222", 2)
        rotated_text(x0 - 58, y0 + h / 2, left_label, size=18)
        if right_label:
            rotated_text(x0 + w + 58, y0 + h / 2, right_label, size=18)
        for q in query_sizes:
            x = x0 + (q - min(query_sizes)) / (max(query_sizes) - min(query_sizes)) * w
            line([(x, y0 + h), (x, y0 + h + 7)], "#222", 1)
            text(x, y0 + h + 30, f"{q // 1000}k", size=17)
        text(x0 + w / 2, y0 + h + 62, "#Queries", size=20)

    def draw_y_ticks(x0, y0, w, h, ticks, scale_y, side="left", formatter=str):
        for tick in ticks:
            y = scale_y(tick)
            if y < y0 - 0.5 or y > y0 + h + 0.5:
                continue
            line([(x0, y), (x0 + w, y)], "#e6e6e6", 1)
            if side == "left":
                text(x0 - 10, y, formatter(tick), size=16, anchor="rm", fill="#333")
            else:
                text(x0 + w + 10, y, formatter(tick), size=16, anchor="lm", fill="#333")

    def x_scale(x0, w, q):
        return x0 + (q - min(query_sizes)) / (max(query_sizes) - min(query_sizes)) * w

    title = "Scalability: TDG vs. Compressed TDG"
    text(width / 2, 28, title, size=25)

    panel_y = 82
    panel_h = 390
    panels = [
        (90, panel_y, 660, panel_h),
        (905, panel_y, 660, panel_h),
        (1740, panel_y, 660, panel_h),
    ]

    tdg_color = "#E76F51"
    comp_color = "#4CB6D6"

    # (a) TDG scale
    x0, y0, w, h = panels[0]
    draw_axes(x0, y0, w, h, "(a) TDG Scale", "TDG nodes", "TDG edge timelines")
    node_values = [
        metric(method, q, "tdg_node_count_mean")
        for method in method_keys
        for q in query_sizes
    ]
    timeline_values = [
        metric(method, q, "tdg_edge_timeline_count_mean")
        for method in method_keys
        for q in query_sizes
    ]
    node_lo, node_hi, sy_nodes = linear_scale(node_values, y0, h, forced_min=0)
    line_lo, line_hi, sy_lines = linear_scale(timeline_values, y0, h, forced_min=0)
    node_ticks = nice_linear_ticks(node_lo, node_hi, 5)
    timeline_ticks = nice_linear_ticks(line_lo, line_hi, 5)
    draw_y_ticks(x0, y0, w, h, node_ticks, sy_nodes, "left", fmt_millions)
    for tick in timeline_ticks:
        y = sy_lines(tick)
        if y < y0 - 0.5 or y > y0 + h + 0.5:
            continue
        text(x0 + w + 10, y, fmt_millions(tick), size=16, anchor="lm", fill="#333")

    legend_items = [
        ("TDG nodes", tdg_color, "circle", False),
        ("Compressed nodes", comp_color, "circle", False),
        ("TDG timelines", tdg_color, "triangle", True),
        ("Compressed timelines", comp_color, "triangle", True),
    ]
    for idx, (label, color, mark, dashed) in enumerate(legend_items):
        lx = x0 + 18
        ly = y0 + 28 + idx * 28
        if dashed:
            line([(lx, ly), (lx + 36, ly)], color, 2)
            for dash_x in [lx + 5, lx + 19, lx + 33]:
                line([(dash_x, ly), (dash_x + 7, ly)], "white", 3)
        else:
            line([(lx, ly), (lx + 36, ly)], color, 2)
        marker(lx + 18, ly, color, mark, 6)
        text(lx + 48, ly + 1, label, size=16, anchor="lm")

    for method, color in [("score_top", tdg_color), ("score_top_compressed", comp_color)]:
        points = [(x_scale(x0, w, q), sy_nodes(metric(method, q, "tdg_node_count_mean"))) for q in query_sizes]
        line(points, color, 3)
        for x, y in points:
            marker(x, y, color, "circle", 7)
        timeline_points = [
            (x_scale(x0, w, q), sy_lines(metric(method, q, "tdg_edge_timeline_count_mean")))
            for q in query_sizes
        ]
        line(timeline_points, color, 2)
        for x, y in timeline_points:
            marker(x, y, color, "triangle", 7)

    # (b) Routing quality
    x0, y0, w, h = panels[1]
    draw_axes(x0, y0, w, h, "(b) Routing Quality", "Avg final travel time", "Best TTT reduction (%)")
    avg_tt_values = [
        metric(method, q, "avg_final_travel_time_mean")
        for method in method_keys
        for q in query_sizes
    ]
    reduction_values = [
        metric(method, q, "best_ttt_reduction_pct_mean")
        for method in method_keys
        for q in query_sizes
    ]
    tt_lo, tt_hi, sy_tt = linear_scale(avg_tt_values, y0, h, forced_min=0)
    red_lo, red_hi, sy_red = linear_scale(reduction_values, y0, h, forced_min=84, forced_max=100)
    draw_y_ticks(x0, y0, w, h, nice_linear_ticks(tt_lo, tt_hi, 5), sy_tt, "left", lambda v: f"{v:.0f}")
    for tick in [85, 90, 95, 100]:
        y = sy_red(tick)
        text(x0 + w + 10, y, f"{tick:g}", size=16, anchor="lm", fill="#333")
    zero_y = sy_red(0) if 84 <= 0 <= 100 else None
    if zero_y is not None:
        line([(x0, zero_y), (x0 + w, zero_y)], "#aaa", 1)

    quality_legend = [
        ("TDG avg TT", tdg_color, "circle", False),
        ("Compressed avg TT", comp_color, "circle", False),
        ("TDG best reduction", tdg_color, "triangle", True),
        ("Compressed best reduction", comp_color, "triangle", True),
    ]
    for idx, (label, color, mark, dashed) in enumerate(quality_legend):
        lx = x0 + 18
        ly = y0 + 28 + idx * 28
        line([(lx, ly), (lx + 36, ly)], color, 2)
        if dashed:
            for dash_x in [lx + 5, lx + 19, lx + 33]:
                line([(dash_x, ly), (dash_x + 7, ly)], "white", 3)
        marker(lx + 18, ly, color, mark, 6)
        text(lx + 48, ly + 1, label, size=16, anchor="lm")

    for method, color in [("score_top", tdg_color), ("score_top_compressed", comp_color)]:
        points = [(x_scale(x0, w, q), sy_tt(metric(method, q, "avg_final_travel_time_mean"))) for q in query_sizes]
        line(points, color, 3)
        for x, y in points:
            marker(x, y, color, "circle", 7)
        red_points = [
            (x_scale(x0, w, q), sy_red(metric(method, q, "best_ttt_reduction_pct_mean")))
            for q in query_sizes
        ]
        line(red_points, color, 2)
        for x, y in red_points:
            marker(x, y, color, "triangle", 7)

    # (c) Runtime breakdown
    x0, y0, w, h = panels[2]
    draw_axes(x0, y0, w, h, "(c) Runtime Breakdown", "Seconds / 5 iterations")
    component_colors = {
        "TDG prep": "#F2C86B",
        "Selection": "#E76F51",
        "Reroute": "#2A9D8F",
        "Evaluation": "#8E6AD8",
    }
    components = [
        ("TDG prep", "tdg_prepare_sec_mean"),
        ("Selection", None),
        ("Reroute", None),
        ("Evaluation", "evaluate_sec_mean"),
    ]

    def component_value(method, q, component):
        if component == "Selection":
            return metric(method, q, "select_sec_mean") + metric(method, q, "candidate_sec_mean")
        if component == "Reroute":
            return (
                metric(method, q, "reroute_sec_mean")
                + metric(method, q, "normalize_sec_mean")
                + metric(method, q, "batch_sec_mean")
            )
        return metric(method, q, dict(components)[component])

    totals = [
        sum(component_value(method, q, component[0]) for component in components)
        for method in method_keys
        for q in query_sizes
    ]
    rt_lo, rt_hi, sy_rt = linear_scale(totals, y0, h, forced_min=0)
    draw_y_ticks(x0, y0, w, h, nice_linear_ticks(rt_lo, rt_hi, 5), sy_rt, "left", lambda v: f"{v:.0f}")

    group_width = w / len(query_sizes)
    bar_w = min(28, group_width * 0.24)
    for idx, q in enumerate(query_sizes):
        cx = x0 + group_width * (idx + 0.5)
        for method_index, method in enumerate(method_keys):
            bx0 = cx + (-bar_w * 0.65 if method_index == 0 else bar_w * 0.65)
            bottom = y0 + h
            for component, _ in components:
                value = component_value(method, q, component)
                top = sy_rt((y0 + h - bottom) * 0 + value)  # overwritten below
                segment_h = (value - rt_lo) / (rt_hi - rt_lo) * h if rt_hi > rt_lo else 0
                top = bottom - segment_h
                rect(
                    bx0 - bar_w / 2,
                    top,
                    bx0 + bar_w / 2,
                    bottom,
                    component_colors[component],
                    "white",
                    1,
                )
                if method == "score_top_compressed":
                    hatch_x = bx0 - bar_w / 2 - 6
                    while hatch_x < bx0 + bar_w / 2 + 12:
                        line(
                            [
                                (hatch_x, bottom),
                                (hatch_x + (bottom - top), top),
                            ],
                            "white",
                            1,
                        )
                        hatch_x += 10
                bottom = top

    # Runtime legend
    legend_y = y0 + 28
    rect(x0 + 20, legend_y - 11, x0 + 55, legend_y + 11, "white", "#777", 2)
    text(x0 + 65, legend_y + 1, "TDG", size=16, anchor="lm")
    rect(x0 + 130, legend_y - 11, x0 + 165, legend_y + 11, "white", "#777", 2)
    for hx in range(126, 168, 10):
        line([(hx, legend_y + 11), (hx + 22, legend_y - 11)], "#777", 1)
    text(x0 + 175, legend_y + 1, "Compressed", size=16, anchor="lm")
    for idx, component in enumerate(component_colors):
        lx = x0 + 20 + (idx % 2) * 190
        ly = legend_y + 35 + (idx // 2) * 30
        rect(lx, ly - 11, lx + 35, ly + 11, component_colors[component], "white", 1)
        text(lx + 45, ly + 1, component, size=16, anchor="lm")

    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)
    return True


def write_paper_scalability_png(path, summary_rows):
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        return False

    method_styles = {
        "full": {
            "label": "Full candidates",
            "color": "#8F6CCF",
            "marker": "circle",
            "dash": None,
        },
        "score_top": {
            "label": "Score pruning",
            "color": "#64BEE8",
            "marker": "square",
            "dash": [14, 8],
        },
        "score_top_compressed": {
            "label": "Score pruning + compressed TDG",
            "color": "#E76F51",
            "marker": "triangle",
            "dash": [14, 7, 4, 7],
        },
    }
    method_order = ["full", "score_top", "score_top_compressed"]
    query_sizes = sorted(
        {
            row["query_size"]
            for row in summary_rows
            if row["method"] in {"score_top", "score_top_compressed"}
        }
    )
    query_sizes = [
        size
        for size in query_sizes
        if value_by_method_size(
            summary_rows, "score_top", size, "runtime_without_initial_sec_mean"
        )
        is not None
        and value_by_method_size(
            summary_rows,
            "score_top_compressed",
            size,
            "runtime_without_initial_sec_mean",
        )
        is not None
    ]
    if not query_sizes:
        return False

    scale = 2
    width, height = 2500, 820
    image = Image.new("RGB", (width * scale, height * scale), "white")
    draw = ImageDraw.Draw(image)

    def font(size, bold=False):
        candidates = [
            "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
            if bold
            else "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
        ]
        for candidate in candidates:
            try:
                return ImageFont.truetype(candidate, size * scale)
            except Exception:
                continue
        return ImageFont.load_default()

    def text(x, y, value, size=18, anchor="mm", bold=False, fill="#222"):
        draw.text(
            (x * scale, y * scale),
            value,
            fill=fill,
            font=font(size, bold),
            anchor=anchor,
        )

    def rotated_text(x, y, value, size=18, fill="#222", bold=False):
        label_font = font(size, bold)
        bbox = draw.textbbox((0, 0), value, font=label_font)
        label_w = bbox[2] - bbox[0]
        label_h = bbox[3] - bbox[1]
        label = Image.new(
            "RGBA",
            (label_w + 10 * scale, label_h + 10 * scale),
            (255, 255, 255, 0),
        )
        label_draw = ImageDraw.Draw(label)
        label_draw.text((5 * scale, 5 * scale), value, fill=fill, font=label_font)
        rotated = label.rotate(90, expand=True)
        image.paste(
            rotated,
            (
                int(x * scale - rotated.size[0] / 2),
                int(y * scale - rotated.size[1] / 2),
            ),
            rotated,
        )

    def line(points, fill, width_px=2):
        draw.line(
            [(x * scale, y * scale) for x, y in points],
            fill=fill,
            width=width_px * scale,
        )

    def styled_line(points, fill, width_px=3, dash=None):
        if not dash:
            line(points, fill, width_px)
            return
        for (x1, y1), (x2, y2) in zip(points, points[1:]):
            length = math.hypot(x2 - x1, y2 - y1)
            if length <= 0:
                continue
            ux = (x2 - x1) / length
            uy = (y2 - y1) / length
            pos = 0.0
            dash_index = 0
            draw_on = True
            while pos < length:
                seg_len = min(dash[dash_index % len(dash)], length - pos)
                if draw_on:
                    sx = x1 + ux * pos
                    sy = y1 + uy * pos
                    ex = x1 + ux * (pos + seg_len)
                    ey = y1 + uy * (pos + seg_len)
                    line([(sx, sy), (ex, ey)], fill, width_px)
                pos += seg_len
                dash_index += 1
                draw_on = not draw_on

    def rect(x0, y0, x1, y1, fill, outline=None, width_px=1):
        draw.rectangle(
            [x0 * scale, y0 * scale, x1 * scale, y1 * scale],
            fill=fill,
            outline=outline,
            width=width_px * scale,
        )

    def marker(x, y, color, marker_name, size_px=9):
        x *= scale
        y *= scale
        s = size_px * scale
        if marker_name == "square":
            draw.rectangle([x - s, y - s, x + s, y + s], fill=color)
        elif marker_name == "triangle":
            draw.polygon([(x, y - s), (x - s, y + s), (x + s, y + s)], fill=color)
        else:
            draw.ellipse([x - s, y - s, x + s, y + s], fill=color)

    def metric(method, query_size, key):
        return value_by_method_size(summary_rows, method, query_size, key)

    def format_tick(value):
        if value >= 1_000_000:
            return f"{value / 1_000_000:g}M"
        if value >= 1000:
            return f"{value / 1000:g}k"
        return f"{value:g}"

    def format_runtime_label(value):
        if value >= 1000:
            return f"{value / 1000:.1f}k"
        if value >= 100:
            return f"{value:.0f}"
        return f"{value:.1f}"

    def panel_frame(x0, y0, w, h, title, y_label):
        text(x0 + w / 2, y0 - 32, title, size=28, bold=True, fill="#2E3642")
        rect(x0, y0, x0 + w, y0 + h, "white", "#BFC7D1", 3)
        rotated_text(x0 - 78, y0 + h / 2, y_label, size=24, bold=True, fill="#2E3642")
        for q in query_sizes:
            x = x0 + (q - min(query_sizes)) / (max(query_sizes) - min(query_sizes)) * w
            line([(x, y0 + h), (x, y0 + h + 8)], "#2E3642", 2)
            text(x, y0 + h + 33, f"{q // 1000}k", size=21, fill="#2E3642")
        text(x0 + w / 2, y0 + h + 67, "Number of queries", size=24, bold=True)

    def y_ticks_linear(min_value, max_value, desired=5):
        return nice_linear_ticks(min_value, max_value, desired)

    def draw_line_panel(
        x0,
        y0,
        w,
        h,
        title,
        y_label,
        metric_key,
        log_y=False,
        y_min=None,
        y_max=None,
        tick_values=None,
        tick_formatter=None,
    ):
        values = []
        for method in method_order:
            for q in query_sizes:
                value = metric(method, q, metric_key)
                if value is not None:
                    values.append(value)
        if not values:
            return

        if log_y:
            lo = max(1e-9, min(values) if y_min is None else y_min)
            hi = max(values) if y_max is None else y_max
            sy = lambda value: y0 + h - (
                (math.log10(value) - math.log10(lo))
                / (math.log10(hi) - math.log10(lo))
            ) * h
            ticks = tick_values or nice_log_ticks(lo, hi)
        else:
            lo = min(values) if y_min is None else y_min
            hi = max(values) if y_max is None else y_max
            if y_min is None or y_max is None:
                pad = (hi - lo) * 0.08 if hi > lo else 1
                if y_min is None:
                    lo -= pad
                if y_max is None:
                    hi += pad
            sy = lambda value: y0 + h - (value - lo) / (hi - lo) * h
            ticks = tick_values or y_ticks_linear(lo, hi)

        sx = lambda q: x0 + (q - min(query_sizes)) / (max(query_sizes) - min(query_sizes)) * w
        panel_frame(x0, y0, w, h, title, y_label)

        for tick in ticks:
            if tick < lo or tick > hi:
                continue
            y = sy(tick)
            styled_line([(x0, y), (x0 + w, y)], "#E8EEF4", 2, [8, 8])
            label = tick_formatter(tick) if tick_formatter else f"{tick:g}"
            text(x0 - 15, y, label, size=23, anchor="rm", fill="#3A4150")

        for method in method_order:
            style = method_styles[method]
            points = []
            for q in query_sizes:
                value = metric(method, q, metric_key)
                if value is not None:
                    points.append((sx(q), sy(value)))
            if not points:
                continue
            styled_line(points, style["color"], 5, style["dash"])
            for x, y in points:
                marker(x, y, style["color"], style["marker"], 10)

    # Compact method legend, styled like the line panels.
    legend_x = 505
    legend_y = 36
    legend_gap = 480
    for index, method in enumerate(method_order):
        style = method_styles[method]
        x = legend_x + index * legend_gap
        styled_line([(x, legend_y), (x + 54, legend_y)], style["color"], 5, style["dash"])
        marker(x + 27, legend_y, style["color"], style["marker"], 10)
        text(x + 68, legend_y + 1, style["label"], size=23, anchor="lm", fill="#2E3642")

    panels = {
        "state": (160, 100, 900, 285),
        "quality": (1400, 100, 900, 285),
        "breakdown": (160, 490, 2140, 200),
    }

    draw_line_panel(
        *panels["state"],
        "(a) TDG State Size",
        "TDG nodes (log scale)",
        "tdg_node_count_mean",
        log_y=True,
        y_min=5_000,
        y_max=4_000_000,
        tick_values=[10_000, 50_000, 100_000, 500_000, 1_000_000, 3_000_000],
        tick_formatter=format_tick,
    )
    draw_line_panel(
        *panels["quality"],
        "(b) Routing Quality",
        "Best TTT reduction (%)",
        "best_ttt_reduction_pct_mean",
        y_min=84,
        y_max=100,
        tick_values=[85, 90, 95, 100],
        tick_formatter=lambda value: f"{value:g}",
    )

    # Runtime composition across query sizes. Bars are absolute runtime; the
    # stacked colors show which stage dominates as methods scale. This wide
    # panel intentionally gives method identity enough room to be legible.
    x0, y0, w, h = panels["breakdown"]
    text(x0 + w / 2, y0 - 32, "(c) Runtime Breakdown", size=28, bold=True, fill="#2E3642")
    rect(x0, y0, x0 + w, y0 + h, "white", "#BFC7D1", 3)
    rotated_text(
        x0 - 78,
        y0 + h / 2,
        "Seconds / 5 iterations",
        size=24,
        bold=True,
        fill="#2E3642",
    )

    component_styles = [
        ("TDG prep", "#B9C4D0", lambda method, q: metric(method, q, "tdg_prepare_sec_mean")),
        (
            "Selection",
            "#E7825C",
            lambda method, q: metric(method, q, "select_sec_mean")
            + metric(method, q, "candidate_sec_mean"),
        ),
        (
            "Reroute",
            "#4DA3A1",
            lambda method, q: metric(method, q, "reroute_sec_mean")
            + metric(method, q, "normalize_sec_mean")
            + metric(method, q, "batch_sec_mean"),
        ),
        ("Evaluation", "#9B8ACB", lambda method, q: metric(method, q, "evaluate_sec_mean")),
    ]

    totals = []
    for q in query_sizes:
        for method in method_order:
            if metric(method, q, "runtime_without_initial_sec_mean") is not None:
                totals.append(metric(method, q, "runtime_without_initial_sec_mean"))
    runtime_max = max(totals)
    runtime_ticks = [0, 2000, 4000, 6000, 8000]
    if runtime_max <= 2000:
        runtime_ticks = [0, 500, 1000, 1500, 2000]
    runtime_top = max(tick for tick in runtime_ticks if tick <= runtime_max)
    while runtime_top < runtime_max:
        runtime_top += runtime_ticks[1] - runtime_ticks[0]
        runtime_ticks.append(runtime_top)
    runtime_top = runtime_ticks[-1]

    for tick in runtime_ticks:
        y = y0 + h - tick / runtime_top * h
        styled_line([(x0, y), (x0 + w, y)], "#E8EEF4", 2, [8, 8])
        text(x0 - 15, y, format_tick(tick), size=23, anchor="rm", fill="#3A4150")

    group_width = w / len(query_sizes)
    bar_w = min(46, group_width * 0.14)
    method_offsets = {
        "full": -bar_w * 1.55,
        "score_top": 0,
        "score_top_compressed": bar_w * 1.55,
    }
    method_short_labels = {
        "full": "Full",
        "score_top": "Score",
        "score_top_compressed": "Comp",
    }
    for idx, q in enumerate(query_sizes):
        cx = x0 + group_width * (idx + 0.5)
        line([(cx, y0 + h), (cx, y0 + h + 7)], "#222", 1)
        text(cx, y0 + h + 55, f"{q // 1000}k", size=21, fill="#2E3642")
        for method in method_order:
            total = metric(method, q, "runtime_without_initial_sec_mean")
            if total is None:
                continue
            bx0 = cx + method_offsets[method] - bar_w / 2
            bx1 = bx0 + bar_w
            bottom = y0 + h
            for _, color, getter in component_styles:
                value = getter(method, q) or 0.0
                segment_h = value / runtime_top * h
                top = bottom - segment_h
                rect(bx0, top, bx1, bottom, color, "white", 1)
                bottom = top
            # Method identity is encoded by position, border color, and the
            # short label below each grouped bar.
            style = method_styles[method]
            rect(bx0, bottom, bx1, y0 + h, None, style["color"], 3)
            text(
                bx0 + bar_w / 2,
                y0 + h + 19,
                method_short_labels[method],
                size=16,
                bold=True,
                fill=style["color"],
            )
            label = format_runtime_label(total)
            label_y = max(y0 + 15, bottom - 13)
            text(
                bx0 + bar_w / 2,
                label_y,
                label,
                size=16,
                bold=True,
                fill=style["color"],
            )
    text(x0 + w / 2, y0 + h + 88, "Number of queries", size=24, bold=True)

    # Component legend lives inside the panel and does not compete with the
    # method legend at the top.
    comp_legend_x = x0 + 28
    comp_legend_y = y0 + 26
    for idx, (label, color, _) in enumerate(component_styles):
        lx = comp_legend_x + (idx % 2) * 205
        ly = comp_legend_y + (idx // 2) * 30
        rect(lx, ly - 12, lx + 32, ly + 12, color, "white", 1)
        text(lx + 44, ly + 1, label, size=18, anchor="lm", fill="#2E3642")

    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)
    return True


def write_scalability_core_png(path, summary_rows):
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        return False

    method_styles = {
        "full": {
            "label": "Full candidates",
            "color": "#8F6CCF",
            "marker": "circle",
            "dash": None,
        },
        "score_top": {
            "label": "Score pruning",
            "color": "#64BEE8",
            "marker": "square",
            "dash": [14, 8],
        },
        "score_top_compressed": {
            "label": "Score pruning + compressed TDG",
            "color": "#E76F51",
            "marker": "triangle",
            "dash": [14, 7, 4, 7],
        },
    }
    method_order = ["full", "score_top", "score_top_compressed"]
    query_sizes = sorted(
        {
            row["query_size"]
            for row in summary_rows
            if row["method"] in {"score_top", "score_top_compressed"}
        }
    )
    query_sizes = [
        size
        for size in query_sizes
        if value_by_method_size(
            summary_rows, "score_top", size, "runtime_without_initial_sec_mean"
        )
        is not None
        and value_by_method_size(
            summary_rows,
            "score_top_compressed",
            size,
            "runtime_without_initial_sec_mean",
        )
        is not None
    ]
    if not query_sizes:
        return False

    scale = 2
    width, height = 2450, 625
    image = Image.new("RGB", (width * scale, height * scale), "white")
    draw = ImageDraw.Draw(image)

    def font(size, bold=False):
        candidates = [
            "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
            if bold
            else "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
        ]
        for candidate in candidates:
            try:
                return ImageFont.truetype(candidate, size * scale)
            except Exception:
                continue
        return ImageFont.load_default()

    def text(x, y, value, size=18, anchor="mm", bold=False, fill="#2E3642"):
        draw.text(
            (x * scale, y * scale),
            value,
            fill=fill,
            font=font(size, bold),
            anchor=anchor,
        )

    def rotated_text(x, y, value, size=20, fill="#2E3642", bold=True):
        label_font = font(size, bold)
        bbox = draw.textbbox((0, 0), value, font=label_font)
        label_w = bbox[2] - bbox[0]
        label_h = bbox[3] - bbox[1]
        label = Image.new(
            "RGBA",
            (label_w + 10 * scale, label_h + 10 * scale),
            (255, 255, 255, 0),
        )
        label_draw = ImageDraw.Draw(label)
        label_draw.text((5 * scale, 5 * scale), value, fill=fill, font=label_font)
        rotated = label.rotate(90, expand=True)
        image.paste(
            rotated,
            (
                int(x * scale - rotated.size[0] / 2),
                int(y * scale - rotated.size[1] / 2),
            ),
            rotated,
        )

    def line(points, fill, width_px=2):
        draw.line(
            [(x * scale, y * scale) for x, y in points],
            fill=fill,
            width=width_px * scale,
        )

    def styled_line(points, fill, width_px=3, dash=None):
        if not dash:
            line(points, fill, width_px)
            return
        for (x1, y1), (x2, y2) in zip(points, points[1:]):
            length = math.hypot(x2 - x1, y2 - y1)
            if length <= 0:
                continue
            ux = (x2 - x1) / length
            uy = (y2 - y1) / length
            pos = 0.0
            dash_index = 0
            draw_on = True
            while pos < length:
                seg_len = min(dash[dash_index % len(dash)], length - pos)
                if draw_on:
                    sx = x1 + ux * pos
                    sy = y1 + uy * pos
                    ex = x1 + ux * (pos + seg_len)
                    ey = y1 + uy * (pos + seg_len)
                    line([(sx, sy), (ex, ey)], fill, width_px)
                pos += seg_len
                dash_index += 1
                draw_on = not draw_on

    def marker(x, y, color, marker_name, size_px=8):
        x *= scale
        y *= scale
        s = size_px * scale
        if marker_name == "square":
            draw.rectangle([x - s, y - s, x + s, y + s], fill=color)
        elif marker_name == "triangle":
            draw.polygon([(x, y - s), (x - s, y + s), (x + s, y + s)], fill=color)
        else:
            draw.ellipse([x - s, y - s, x + s, y + s], fill=color)

    def metric(method, query_size, key):
        return value_by_method_size(summary_rows, method, query_size, key)

    def fmt_runtime(value):
        if value >= 1000:
            return f"{value / 1000:g}k"
        return f"{value:g}"

    def fmt_large(value):
        if value >= 1_000_000_000:
            return f"{value / 1_000_000_000:g}B"
        if value >= 1_000_000:
            return f"{value / 1_000_000:g}M"
        if value >= 1000:
            return f"{value / 1000:g}k"
        return f"{value:g}"

    x_positions = {
        q: idx
        for idx, q in enumerate(query_sizes)
    }

    def draw_panel(
        x0,
        y0,
        w,
        h,
        title,
        y_label,
        series,
        y_min,
        y_max,
        y_ticks,
        log_y=False,
        y_formatter=None,
        legend=True,
    ):
        line([(x0, y0), (x0, y0 + h), (x0 + w, y0 + h)], "#C6CFDA", 3)
        rotated_text(x0 - 82, y0 + h / 2, y_label, size=24)

        def sx(query_size):
            if len(query_sizes) == 1:
                return x0 + w / 2
            return x0 + x_positions[query_size] / (len(query_sizes) - 1) * w

        if log_y:
            lo = max(1e-9, y_min)
            hi = y_max

            def sy(value):
                return y0 + h - (
                    (math.log10(value) - math.log10(lo))
                    / (math.log10(hi) - math.log10(lo))
                ) * h

        else:
            lo = y_min
            hi = y_max

            def sy(value):
                return y0 + h - (value - lo) / (hi - lo) * h

        for tick in y_ticks:
            if tick < lo or tick > hi:
                continue
            y = sy(tick)
            styled_line([(x0, y), (x0 + w, y)], "#E8EEF4", 2, [8, 8])
            if y_formatter:
                label = y_formatter(tick)
            elif log_y:
                label = fmt_runtime(tick)
            else:
                label = f"{tick:g}"
            text(x0 - 16, y, label, size=24, anchor="rm", fill="#3A4150")

        for q in query_sizes:
            x = sx(q)
            line([(x, y0 + h), (x, y0 + h + 8)], "#2E3642", 2)
            text(x, y0 + h + 34, f"{q // 1000}k", size=21, fill="#2E3642")
        text(x0 + w / 2, y0 + h + 67, "Number of queries", size=25, bold=True)
        text(x0 + w / 2, y0 + h + 108, title, size=29, bold=True)

        for item in series:
            points = []
            for q in query_sizes:
                if q not in item["values"]:
                    continue
                value = item["values"][q]
                if value is None:
                    continue
                points.append((sx(q), sy(value)))
            if not points:
                continue
            styled_line(points, item["color"], 5, item.get("dash"))
            for x, y in points:
                marker(x, y, item["color"], item.get("marker", "circle"), 8)

        if legend:
            lx = x0 + 18
            ly = y0 + 22
            for idx, item in enumerate(series):
                row_y = ly + idx * 27
                styled_line(
                    [(lx, row_y), (lx + 38, row_y)],
                    item["color"],
                    4,
                    item.get("dash"),
                )
                marker(lx + 19, row_y, item["color"], item.get("marker", "circle"), 6)
                text(lx + 52, row_y, item["label"], size=19, anchor="lm")

    def make_metric_series(metric_key):
        series = []
        for method in method_order:
            style = method_styles[method]
            series.append(
                {
                    "label": style["label"],
                    "color": style["color"],
                    "marker": style["marker"],
                    "dash": style["dash"],
                    "values": {
                        q: metric(method, q, metric_key)
                        for q in query_sizes
                        if metric(method, q, metric_key) is not None
                    },
                }
            )
        return series

    legend_x = 430
    legend_y = 38
    legend_gap = 520
    for index, method in enumerate(method_order):
        style = method_styles[method]
        x = legend_x + index * legend_gap
        styled_line([(x, legend_y), (x + 52, legend_y)], style["color"], 5, style["dash"])
        marker(x + 26, legend_y, style["color"], style["marker"], 8)
        text(x + 68, legend_y + 1, style["label"], size=24, anchor="lm")

    runtime_series = make_metric_series("runtime_without_initial_sec_mean")
    tdg_series = make_metric_series("tdg_node_count_mean")
    ttt_reduction_series = make_metric_series("best_ttt_reduction_pct_mean")

    draw_panel(
        130,
        115,
        600,
        315,
        "(a) Runtime",
        "Runtime (s)",
        runtime_series,
        10,
        10000,
        [10, 100, 1000, 10000],
        log_y=True,
        y_formatter=fmt_runtime,
        legend=False,
    )
    draw_panel(
        925,
        115,
        600,
        315,
        "(b) TDG Size",
        "TDG nodes",
        tdg_series,
        5000,
        4_000_000,
        [10_000, 100_000, 1_000_000],
        log_y=True,
        y_formatter=fmt_large,
        legend=False,
    )
    draw_panel(
        1720,
        115,
        600,
        315,
        "(c) TTT Reduction",
        "TTT reduction (%)",
        ttt_reduction_series,
        84,
        100,
        [85, 90, 95, 100],
        log_y=False,
        y_formatter=lambda value: f"{value:g}",
        legend=False,
    )

    path.parent.mkdir(parents=True, exist_ok=True)
    if path.suffix.lower() == ".pdf":
        image.save(path, "PDF", resolution=300.0)
    else:
        image.save(path)
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output-dir",
        default="python/results/experiments/exp3_compression_scalability/peak1h_clean/plots",
    )
    parser.add_argument("--max-iterations", type=int, default=5)
    for method in METHODS:
        parser.add_argument(f"--{method['key']}-dir", default=method["default_dir"])
    args = parser.parse_args()

    all_metrics = []
    skipped_by_method = {}
    for method in METHODS:
        metrics, skipped = load_completed_dataset_metrics(
            method["key"], getattr(args, f"{method['key']}_dir"), args.max_iterations
        )
        all_metrics.extend(metrics)
        skipped_by_method[method["key"]] = skipped

    summary_rows = summarize(all_metrics)
    output_dir = Path(args.output_dir)
    write_summary_csv(output_dir / "scalability_summary.csv", summary_rows)
    write_csv(output_dir / "scalability_dataset_metrics.csv", all_metrics)
    write_svg(output_dir / "scalability_runtime_quality.svg", summary_rows)
    write_png_from_svg_like(output_dir / "scalability_runtime_quality.png", summary_rows)
    write_scalability_core_png(output_dir / "tdg_vs_compressed_scalability.png", summary_rows)
    write_scalability_core_png(output_dir / "scalability_paper_figure.png", summary_rows)
    write_scalability_core_png(output_dir / "scalability_paper_figure.pdf", summary_rows)
    write_scalability_core_png(output_dir / "scalability_core_metrics.png", summary_rows)

    print(f"wrote {output_dir / 'scalability_summary.csv'}")
    print(f"wrote {output_dir / 'scalability_dataset_metrics.csv'}")
    print(f"wrote {output_dir / 'scalability_runtime_quality.svg'}")
    if (output_dir / "scalability_runtime_quality.png").exists():
        print(f"wrote {output_dir / 'scalability_runtime_quality.png'}")
    if (output_dir / "tdg_vs_compressed_scalability.png").exists():
        print(f"wrote {output_dir / 'tdg_vs_compressed_scalability.png'}")
    if (output_dir / "scalability_paper_figure.png").exists():
        print(f"wrote {output_dir / 'scalability_paper_figure.png'}")
    if (output_dir / "scalability_paper_figure.pdf").exists():
        print(f"wrote {output_dir / 'scalability_paper_figure.pdf'}")
    if (output_dir / "scalability_core_metrics.png").exists():
        print(f"wrote {output_dir / 'scalability_core_metrics.png'}")
    for method_key, skipped in skipped_by_method.items():
        if skipped:
            print(f"skipped incomplete {method_key}: {skipped}")


if __name__ == "__main__":
    main()
