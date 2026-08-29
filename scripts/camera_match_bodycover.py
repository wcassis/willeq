#!/usr/bin/env python3
"""T310 Leg C -- how much of the viewport does the player's OWN body cover?

The one number that makes fable's swing frames comparable to T307's official-
client frames even though the two were shot in different places: T307 filmed
the official client at LEVEL pitch showing only the spearhead sweeping in low,
with the body BELOW the frustum; T309 measured fable putting the player's own
FACE across the WHOLE viewport on every swing. "Whole viewport" and "a
spearhead" are separable by area, and area does not care what the background
is.

Method (T309's, reused deliberately): pick the modal REST frame -- the frame
the capture spends most of its time on, which is the between-swings view --
and report, for every frame, the fraction of pixels differing from it by more
than the threshold in any channel. Own geometry entering the view is the only
thing that moves in a static capture, so that fraction IS its screen coverage.

Prints a per-frame TSV and a summary: max coverage, and the number of frames
above 50% (a face-fill) and above 10% (something drawn).

Usage: camera_match_bodycover.py --dir FRAMEDIR [--threshold 12] [--tsv]
"""
import argparse, glob, os, sys
import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--dir", required=True)
    p.add_argument("--glob", default="frame_*.png")
    p.add_argument("--threshold", type=int, default=12)
    p.add_argument("--tsv", action="store_true")
    p.add_argument("--skip", type=int, default=30,
                   help="ignore the first N frames (zone load / black screen)")
    a = p.parse_args()

    files = sorted(glob.glob(os.path.join(a.dir, a.glob)))
    skip = a.skip
    if len(files) < 3:
        print("need at least 3 frames", file=sys.stderr)
        return 2

    # THE BASELINE IS A PER-PIXEL MEDIAN, not "the most common frame".
    # The first attempt used the modal frame (md5) on the reasoning that a
    # static camera renders identical bytes between swings. It does not:
    # qeynos2 has wandering NPCs and a moving sky, so NO two frames were ever
    # byte-identical, the mode fell back to frame_000000 -- which is the BLACK
    # LOADING SCREEN -- and every frame then scored 100% coverage. A median
    # over a subsample is immune to that: transient own-body geometry is a
    # minority of frames at any pixel, so the median is the rest view.
    usable = files[skip:] if len(files) > skip + 10 else files
    step = max(1, len(usable) // 48)
    sample = usable[::step]
    stack = np.stack([np.asarray(Image.open(f).convert("RGB"), dtype=np.uint8)
                      for f in sample])
    base = np.median(stack, axis=0).astype(np.int16)
    base_file = "median of %d frames" % len(sample)

    rows = []
    for f in usable:
        d = np.abs(load(f) - base).max(axis=2)
        rows.append((os.path.basename(f), float((d > a.threshold).mean() * 100.0)))

    if a.tsv:
        for n, c in rows:
            print("%s\t%.4f" % (n, c))
    cov = [c for _, c in rows]
    peak = max(rows, key=lambda r: r[1])
    print("BASELINE\t%s" % base_file)
    print("MAXCOVER\t%.4f\t%s" % (peak[1], peak[0]))
    print("FRAMES_OVER_50PCT\t%d" % sum(1 for c in cov if c > 50))
    print("FRAMES_OVER_10PCT\t%d" % sum(1 for c in cov if c > 10))
    print("FRAMES\t%d (of %d, first %d skipped)" % (len(usable), len(files), len(files) - len(usable)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
