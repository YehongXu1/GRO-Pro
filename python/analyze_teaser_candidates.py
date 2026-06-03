#!/usr/bin/env python3
"""Score each (phi, seed) combo. Constraint: BOTH random and most_delayed must oscillate."""

import csv
from pathlib import Path
from collections import defaultdict

BASE = Path("/Users/xyh/Desktop/GRO-Pro/python/results/experiments/exp_teaser/bj_peak1h_100k")
FILES = {
    10: BASE / "iterr_phi10_K10_capacity2_cap10e8.csv",
    30: BASE / "iterr_phi30_K10_capacity2_cap10e8.csv",
}


def load(path):
    data = defaultdict(list)
    with path.open() as f:
        r = csv.DictReader(f)
        for row in r:
            seed = int(row["seed"])
            it = int(row["iteration"])
            tb = int(row["total_before"])
            ta = int(row["total_after"])
            data[(seed, row["selection_method"])].append((it, tb, ta))
    return data


def trace(data, seed, sm):
    rows = sorted(data[(seed, sm)])
    return [rows[0][1]] + [r[2] for r in rows]


def osc_metrics(rel):
    # Use relative TTT; ignore iter 0 = 1.0.
    ups = 0
    biggest_up = 0.0
    biggest_up_iter = -1
    for i in range(1, len(rel)):
        if rel[i] > rel[i - 1] * 1.05:  # 5% increase counts as up-swing
            ups += 1
            jump = rel[i] - rel[i - 1]
            if jump > biggest_up:
                biggest_up = jump
                biggest_up_iter = i
    swing_range = max(rel[1:]) - min(rel[1:])
    return ups, biggest_up, biggest_up_iter, swing_range


for phi, path in FILES.items():
    data = load(path)
    seeds = sorted({s for s, _ in data.keys()})
    print(f"\n================ phi={phi}% ================")
    print(f"{'seed':>4} {'random oscillation':35s} {'most_delayed oscillation':35s} {'r<d iters'}")
    for seed in seeds:
        ttt_r = trace(data, seed, "random")
        ttt_d = trace(data, seed, "most_delayed")
        rel_r = [x / ttt_r[0] for x in ttt_r]
        rel_d = [x / ttt_d[0] for x in ttt_d]

        ups_r, big_r, ir, range_r = osc_metrics(rel_r)
        ups_d, big_d, id_, range_d = osc_metrics(rel_d)
        r_wins = sum(1 for i in range(1, len(rel_r)) if rel_r[i] < rel_d[i])

        r_desc = f"ups={ups_r}, +{big_r:.2f}@it{ir}, range={range_r:.2f}"
        d_desc = f"ups={ups_d}, +{big_d:.2f}@it{id_}, range={range_d:.2f}"
        print(f"  {seed:>2} | {r_desc:33s} | {d_desc:33s} | {r_wins}/10")

    # Per-iter relative traces, all decimals
    print()
    print(f"  Full per-iter relative TTT (iter 1..10):")
    for seed in seeds:
        for sm in ("random", "most_delayed"):
            ttt = trace(data, seed, sm)
            rel = [x / ttt[0] for x in ttt[1:]]
            sline = " ".join(f"{v:6.3f}" for v in rel)
            print(f"  s{seed} {sm:12s}: {sline}")
        print()


# Identify the single best seed where BOTH methods oscillate notably.
print("\n================ BEST single-seed candidates ================")
for phi, path in FILES.items():
    data = load(path)
    seeds = sorted({s for s, _ in data.keys()})
    candidates = []
    for seed in seeds:
        ttt_r = trace(data, seed, "random")
        ttt_d = trace(data, seed, "most_delayed")
        rel_r = [x / ttt_r[0] for x in ttt_r]
        rel_d = [x / ttt_d[0] for x in ttt_d]
        ups_r, _, _, range_r = osc_metrics(rel_r)
        ups_d, _, _, range_d = osc_metrics(rel_d)
        r_wins = sum(1 for i in range(1, len(rel_r)) if rel_r[i] < rel_d[i])

        # Score: both must oscillate, want range>=0.10 for both, and random must win >= 5 iters.
        both_osc = ups_r >= 2 and ups_d >= 3 and range_r >= 0.10 and range_d >= 0.15
        good_story = both_osc and r_wins >= 5
        score = ups_r + ups_d + 0.5 * r_wins
        candidates.append((seed, ups_r, ups_d, range_r, range_d, r_wins, both_osc, good_story, score))
    candidates.sort(key=lambda c: -c[-1])
    print(f"\nphi={phi}%")
    print(f"{'seed':>4} {'r-ups':>6} {'d-ups':>6} {'r-range':>8} {'d-range':>8} {'r<d':>4} {'both_osc':>9} {'good_story':>11} {'score':>6}")
    for c in candidates:
        print(f"  {c[0]:>2} {c[1]:>6} {c[2]:>6} {c[3]:>8.2f} {c[4]:>8.2f} {c[5]:>4} {str(c[6]):>9} {str(c[7]):>11} {c[8]:>6.1f}")
