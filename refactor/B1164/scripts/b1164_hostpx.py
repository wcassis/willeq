#!/usr/bin/env python3
"""B1164: measure a spawned target's on-screen height from two settle stills of the same camera:
the still WITH the target and one WITHOUT it (before the #spawn). The target's pixels are the
max-RGB difference > thr inside a search box; prints the bbox (x0..x1, y0..y1) and the height y1-y0+1.
usage: b1164_hostpx.py <with.png> <without.png> <box x,y,w,h> [thr=40] [min_row_px=3]
A row counts when at least min_row_px pixels in it differ (drops isolated noise)."""
import sys, numpy as np, cv2
a = cv2.imread(sys.argv[1]).astype(np.int16); b = cv2.imread(sys.argv[2]).astype(np.int16)
x, y, w, h = map(int, sys.argv[3].split(','))
thr = int(sys.argv[4]) if len(sys.argv) > 4 else 40
mr = int(sys.argv[5]) if len(sys.argv) > 5 else 3
d = (np.abs(a - b).max(2) > thr)[y:y + h, x:x + w]
rows = np.where(d.sum(1) >= mr)[0]; cols = np.where(d.sum(0) >= mr)[0]
print(f'{sys.argv[1]} vs {sys.argv[2]} box {sys.argv[3]} thr {thr}: x {x + cols.min()}..{x + cols.max()} '
      f'y {y + rows.min()}..{y + rows.max()} height {rows.max() - rows.min() + 1} px')
