#!/usr/bin/env python3
"""Measure the b1130 target's standing run in the `solve_r1` frames.

`solve_r1` is a `--camera b1130` (fixed_npc) harness run (this card's
rest-pose solve; the frames are attached evidence, not in the repo).
The target stands on the RIGHT of the frame, isolated from the caster
(column ~640). For each frame we take the target's dark run -- the
pixels darker than THRESHOLD inside the window COL_LO..COL_HI -- and
report its head-top row, feet row, head-top-to-feet height and column
centroid, with B1130's own feet-minus-head convention (its caster:
209 px from head top 389 to feet 598, review/B1130/README.md:73-74).

Usage:
    python3 measure_b1155_target.py <frames-dir> [FRAME|RANGE ...]
    python3 measure_b1155_target.py <frames-dir>          # frames 10-90 and 179

Each line is one frame; the final line is the range across all frames.
"""
import sys

import numpy as np
from PIL import Image

THRESHOLD = 60  # the run's dark edge; 40 clips the feet, 80 floods the backdrop
COL_LO, COL_HI = 850, 1080  # the target's window; the caster is at column ~640
DEFAULT = ["10-90", "179"]


def frames_from_args(args):
    out = []
    for a in args or DEFAULT:
        if "-" in a:
            lo, hi = (int(x) for x in a.split("-"))
            out.extend(range(lo, hi + 1))
        else:
            out.append(int(a))
    return out


def measure(path):
    g = np.array(Image.open(path).convert("RGB")).mean(axis=2)
    dark = g < THRESHOLD
    dark[:, :COL_LO] = False
    dark[:, COL_HI:] = False
    ys, xs = np.where(dark)
    if len(ys) == 0:
        raise SystemExit(f"no dark run in {path}")
    top, feet = int(ys.min()), int(ys.max())
    return top, feet, feet - top, float(xs.mean())


def main():
    d = sys.argv[1]
    tops, feets, hs, cxs = [], [], [], []
    for i in frames_from_args(sys.argv[2:]):
        top, feet, h, cx = measure(f"{d}/frame_{i:05d}.png")
        tops.append(top); feets.append(feet); hs.append(h); cxs.append(cx)
        print(f"frame {i:3d}: head top {top} -> feet {feet} = {h} px, column centroid {cx:.1f}")
    print(f"range over {len(hs)} frames: head top {min(tops)}-{max(tops)}, "
          f"feet {min(feets)}-{max(feets)}, height {min(hs)}-{max(hs)} px, "
          f"column centroid {min(cxs):.1f}-{max(cxs):.1f}")


if __name__ == "__main__":
    main()
