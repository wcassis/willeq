#!/usr/bin/env python3
# B1195: UI panels by color. The Titanium UI window background is dark navy ~ (22,23,33).
# mask = pixels within tol of that color; connected components -> boxes.
import sys
from collections import deque
from PIL import Image
import numpy as np
im = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(np.int32)
h, w, _ = im.shape
tol = float(sys.argv[2]) if len(sys.argv) > 2 else 45.0
c = np.array([22, 23, 33])
d = np.sqrt(((im - c) ** 2).sum(2))
mask = d < tol
lab = np.zeros((h, w), int)
boxes, n = [], 0
for y in range(h):
    for x in range(w):
        if mask[y, x] and lab[y, x] == 0:
            n += 1
            dq = deque([(y, x)]); lab[y, x] = n
            minx = maxx = x; miny = maxy = y; cnt = 0
            while dq:
                cy, cx = dq.popleft(); cnt += 1
                minx = min(minx, cx); maxx = max(maxx, cx)
                miny = min(miny, cy); maxy = max(maxy, cy)
                for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    ny, nx = cy + dy, cx + dx
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and lab[ny, nx] == 0:
                        lab[ny, nx] = n
                        dq.append((ny, nx))
            if cnt > 800:
                boxes.append((minx, miny, maxx - minx + 1, maxy - miny + 1, cnt))
for b in sorted(boxes, key=lambda t: -t[4]):
    print(f'x={b[0]} y={b[1]} w={b[2]} h={b[3]} px={b[4]}')
print(f'frame {w}x{h} tol={tol}: {len(boxes)} navy regions')
