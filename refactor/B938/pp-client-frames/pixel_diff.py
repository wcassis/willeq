#!/usr/bin/env python3
# B938 acceptance-2 per-pixel diff: compares two directories of numbered PNG
# frames (frame_%06d.png) and reports per-frame and aggregate pixel statistics.
# Mirrors B936's parity_results.txt format so the evidence reads the same way.
#
# Usage: pixel_diff.py <dir_a> <dir_b> [--max-abs N] [--out FILE]
#   <dir_a>/<dir_b>  directories of frame_*.png (matched by frame index)
#   --max-abs N      only count pixels whose max abs channel diff exceeds N
#                    (default 0 = exact; use a small N to report the "real"
#                    signal above llvmpipe FP16 rounding noise)
#   --out FILE       also write a per-frame TSV
#
# Exit: 0 always (this is a measurement, not a gate); the caller decides.

import argparse
import glob
import os
import re
import sys

import numpy as np
from PIL import Image


def load_frame(path):
    with Image.open(path) as im:
        im = im.convert("RGB")
    return np.asarray(im, dtype=np.int16)


def frame_index(path):
    m = re.search(r"frame_(\d+)\.png$", os.path.basename(path))
    return int(m.group(1)) if m else -1


def diff_dirs(dir_a, dir_b, max_abs):
    frames_a = {frame_index(p): p for p in glob.glob(os.path.join(dir_a, "frame_*.png"))}
    frames_b = {frame_index(p): p for p in glob.glob(os.path.join(dir_b, "frame_*.png"))}
    common = sorted(set(frames_a) & set(frames_b))
    if not common:
        print(f"NO COMMON FRAMES: {dir_a} has {sorted(frames_a)}, {dir_b} has {sorted(frames_b)}")
        return
    rows = []
    for idx in common:
        a = load_frame(frames_a[idx])
        b = load_frame(frames_b[idx])
        if a.shape != b.shape:
            print(f"frame {idx}: SHAPE MISMATCH {a.shape} vs {b.shape}")
            continue
        d = np.abs(a - b)
        max_abs_frame = int(d.max())
        # differing pixels: any channel differs by more than max_abs
        exceed = (d.max(axis=2) > max_abs)
        differing = int(exceed.sum())
        # mean over the differing pixels (or 0 if none)
        if differing:
            mean_diff = float(d[exceed].mean())
        else:
            mean_diff = 0.0
        rows.append((idx, a.shape[1], a.shape[0], max_abs_frame, differing, mean_diff))
        print(f"frame {idx:6d}  {a.shape[1]}x{a.shape[0]}  max_abs={max_abs_frame:4d}  "
              f"differing_px(>{max_abs})={differing:8d}  mean_diff={mean_diff:8.3f}")
    if not rows:
        return
    # Aggregate: total differing pixels and total pixels compared
    total_compared = sum(r[1] * r[2] for r in rows)
    total_differing = sum(r[4] for r in rows)
    worst_max = max(r[3] for r in rows)
    pct = 100.0 * total_differing / total_compared if total_compared else 0.0
    print("-" * 60)
    print(f"AGGREGATE ({len(rows)} frames, {total_compared} px compared, max_abs threshold {max_abs}):")
    print(f"  worst per-frame max abs diff : {worst_max}")
    print(f"  total differing pixels       : {total_differing}  ({pct:.5f}% of compared)")
    out = args.out
    if out:
        with open(out, "w") as f:
            f.write(f"# pixel_diff {dir_a} vs {dir_b} (max_abs threshold {max_abs})\n")
            f.write("frame\twidth\theight\tmax_abs\tdiffering_px\tmean_diff\n")
            for r in rows:
                f.write("\t".join(str(x) for x in r) + "\n")
            f.write(f"# aggregate\tframes={len(rows)}\tcompared_px={total_compared}\t"
                    f"differing_px={total_differing}\tpct={pct:.5f}\tworst_max_abs={worst_max}\n")
        print(f"  TSV written: {out}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("dir_a")
    ap.add_argument("dir_b")
    ap.add_argument("--max-abs", type=int, default=0)
    ap.add_argument("--out")
    args = ap.parse_args()
    diff_dirs(args.dir_a, args.dir_b, args.max_abs)
