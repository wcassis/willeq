#!/usr/bin/env python3
"""P41: honest per-pixel vs lightweight diff stats + crop for the 3 befallen
golden poses. Reads /mnt/projects/fable/videos/p41_lighting/{perpixel,lightweight}_f{N}.png,
writes a diff heatmap + a crop of the largest-diff region, and prints stats.
"""
import json
from pathlib import Path
from PIL import Image, ImageChops

OUT = Path("/mnt/projects/fable/videos/p41_lighting")
STATS = {}

for f in (0, 1, 2):
    a = Image.open(OUT / f"perpixel_f{f}.png").convert("RGB")
    b = Image.open(OUT / f"lightweight_f{f}.png").convert("RGB")
    assert a.size == b.size
    diff = ImageChops.difference(a, b)
    bbox = diff.getbbox()
    px = list(diff.getdata())
    n = len(px)
    # per-pixel max channel delta
    deltas = [max(p) for p in px]
    changed = sum(1 for d in deltas if d > 0)
    mean_delta = sum(deltas) / n
    max_delta = max(deltas)
    diff.save(OUT / f"diff_f{f}.png")

    # crop: 200x200 window centered on the pixel of max delta
    if max_delta > 0:
        idx = deltas.index(max_delta)
        w, h = a.size
        cx, cy = idx % w, idx // w
        half = 100
        box = (max(0, cx - half), max(0, cy - half), min(w, cx + half), min(h, cy + half))
        a.crop(box).save(OUT / f"crop_perpixel_f{f}.png")
        b.crop(box).save(OUT / f"crop_lightweight_f{f}.png")
    else:
        box = None

    STATS[f] = {
        "size": list(a.size),
        "pct_pixels_differ": round(100.0 * changed / n, 3),
        "mean_channel_delta": round(mean_delta, 3),
        "max_channel_delta": max_delta,
        "bbox": bbox,
        "crop_box": box,
    }
    print(f"f{f}: {STATS[f]}")

with open(OUT / "diff_stats.json", "w") as fh:
    json.dump(STATS, fh, indent=2)
print("wrote", OUT / "diff_stats.json")
