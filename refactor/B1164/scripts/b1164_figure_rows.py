#!/usr/bin/env python3
"""B1164: the vertical extent of a standing figure in ONE still, without a figure-free frame:
per row, count the pixels in the figure's column band [x0,x1] whose colour departs from that
row's background (the per-channel median of the two side bands [x0-W,x0) and (x1,x1+W]) by more
than thr (max-channel); the figure's rows are the first..last row with >= minpx such pixels in
a run of >= run consecutive rows. Prints the top row, the bottom row and the height.
usage: b1164_figure_rows.py <still.png> <x0> <x1> <y0> <y1> [thr=45] [minpx=4] [W=60] [run=4]"""
import sys, numpy as np, cv2
im = cv2.imread(sys.argv[1]).astype(np.int16)
x0, x1, y0, y1 = map(int, sys.argv[2:6])
thr = int(sys.argv[6]) if len(sys.argv) > 6 else 45
minpx = int(sys.argv[7]) if len(sys.argv) > 7 else 4
W = int(sys.argv[8]) if len(sys.argv) > 8 else 60
run = int(sys.argv[9]) if len(sys.argv) > 9 else 4
hit = []
for y in range(y0, y1 + 1):
    side = np.concatenate([im[y, x0 - W:x0], im[y, x1 + 1:x1 + 1 + W]])
    bg = np.median(side, axis=0)
    dev = np.abs(im[y, x0:x1 + 1] - bg).max(axis=1) > thr
    hit.append(dev.sum() >= minpx)
hit = np.array(hit)
ok = np.convolve(hit.astype(int), np.ones(run, int), 'valid') == run   # run-start rows
starts = np.where(ok)[0]
top = y0 + starts.min(); bot = y0 + starts.max() + run - 1
print(f'{sys.argv[1]} band x {x0}..{x1} rows {y0}..{y1} thr {thr} minpx {minpx} W {W} run {run}: '
      f'top {top} bottom {bot} height {bot - top + 1} px')
