#!/usr/bin/env python3
"""B1170 (#489): where two harness runs of one plan differ, frame by frame.

usage: python3 -P frame_diff_series.py <run_a> <run_b> <out.tsv>
Writes one row per frame index present in both runs: frame, the number of
pixels whose max |BGR difference| exceeds 0, the bounding box of those
pixels (x0 y0 x1 y1, '-' when none). Prints the summary line: frames compared,
frames differing, first and last differing frame. 30 fps harness frames:
frame k is FX time k / 30 s (the plan's OP_Action is at t_begin + cast_ms).
"""
import glob
import os
import sys

import cv2
import numpy as np


def main():
    a, b, out = sys.argv[1], sys.argv[2], sys.argv[3]
    fa = {os.path.basename(p): p for p in glob.glob(os.path.join(a, '*.png'))}
    fb = {os.path.basename(p): p for p in glob.glob(os.path.join(b, '*.png'))}
    names = sorted(set(fa) & set(fb))
    rows = ['frame\tdiff_px\tx0\ty0\tx1\ty1']
    diff_frames = []
    for n in names:
        ia = cv2.imread(fa[n]).astype(np.int16)
        ib = cv2.imread(fb[n]).astype(np.int16)
        d = np.abs(ia - ib).max(axis=2)
        ys, xs = np.nonzero(d)
        idx = int(''.join(c for c in n if c.isdigit()) or -1)
        if len(xs):
            diff_frames.append(idx)
            rows.append('%d\t%d\t%d\t%d\t%d\t%d' % (idx, len(xs), xs.min(), ys.min(), xs.max(), ys.max()))
        else:
            rows.append('%d\t0\t-\t-\t-\t-' % idx)
    open(out, 'w').write('\n'.join(rows) + '\n')
    print('%s vs %s: %d frames, %d differ, first %s, last %s' % (
        a, b, len(names), len(diff_frames),
        diff_frames[0] if diff_frames else '-', diff_frames[-1] if diff_frames else '-'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
