#!/usr/bin/env python3
"""B1168 (#487): does the B1152 "in-flight missile" move from caster to target?

For each video frame, the >=180 grayscale pixels inside the B1152 deep-gap box
G (x 660-760, y 440-620 of the 1280x800 observer video; the box and threshold
are the ones in refactor/B1152/missile_oracle.tsv's header) are counted and
their centroid is taken.  The target (the observer, Oraclewatch) stands LEFT
of the box and the caster (Testcaster, column ~935) RIGHT of it, so a
caster->target flight has a centroid moving to smaller x, and a 9.05-unit
flight in 0.30 s crosses the 100-px box in a few frames.

usage: python3 -P gap_track.py <video.mp4> <first> <last> <out.tsv>
prints and writes: frame, count, centroid x, centroid y ("-" when empty)
"""
import sys

import cv2
import numpy as np

X0, X1, Y0, Y1 = 660, 760, 440, 620


def main():
    video, first, last, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    cap = cv2.VideoCapture(video)
    cap.set(cv2.CAP_PROP_POS_FRAMES, first)
    lines = ["frame\tcount\tcx\tcy"]
    for f in range(first, last + 1):
        ok, img = cap.read()
        if not ok:
            break
        g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)[Y0:Y1, X0:X1]
        ys, xs = np.nonzero(g >= 180)
        if len(xs):
            lines.append("%d\t%d\t%.1f\t%.1f" % (f, len(xs), X0 + xs.mean(), Y0 + ys.mean()))
        else:
            lines.append("%d\t0\t-\t-" % f)
    open(out, "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
