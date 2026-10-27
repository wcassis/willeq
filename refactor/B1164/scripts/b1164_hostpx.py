#!/usr/bin/env python3
"""B1164: a target's on-screen height by DIFFERENCE: the still WITH the target vs a still WITHOUT
it at the same camera. A row of the band x0..x1 counts when >= minpx pixels differ by
max-RGB > thr. Prints every row's count in y0..y1 (the evidence) and the first/last counted row.
y0..y1 is chosen to leave out the name label above the head and, where present, the selection
ring below the boots; the printed counts show where those are.
usage: b1164_hostpx.py <with.png> <without.png> <x0> <x1> <y0> <y1> [thr=40] [minpx=3]"""
import sys, numpy as np, cv2
a = cv2.imread(sys.argv[1]).astype(np.int16); b = cv2.imread(sys.argv[2]).astype(np.int16)
x0, x1, y0, y1 = map(int, sys.argv[3:7])
thr = int(sys.argv[7]) if len(sys.argv) > 7 else 40
mp = int(sys.argv[8]) if len(sys.argv) > 8 else 3
m = np.abs(a - b).max(axis=2) > thr
cnt = m[:, x0:x1 + 1].sum(axis=1)
rows = [y for y in range(y0, y1 + 1) if cnt[y] >= mp]
print('# row counts', ' '.join(f'{y}:{cnt[y]}' for y in range(y0, y1 + 1)))
print(f'{sys.argv[1]} - {sys.argv[2]} band x {x0}..{x1} rows {y0}..{y1} thr {thr} minpx {mp}: '
      f'top {rows[0]} bottom {rows[-1]} height {rows[-1] - rows[0] + 1} px')
