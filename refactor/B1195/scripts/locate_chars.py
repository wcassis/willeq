#!/usr/bin/env python3
# B1195: locate the two characters in a 1280x800 observer still.
# Druid (Testcaster): the LARGEST red blob (the robe) -> bbox -> feet = bottom center, height = bbox h.
# Warrior (Oraclewatch): dense non-background cluster in the band LEFT of the druid's bbox.
import sys
from collections import deque
from PIL import Image
import numpy as np
im = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(np.int32)
h, w, _ = im.shape
R, G, B = im[..., 0], im[..., 1], im[..., 2]
red = (R > 120) & (R - G > 50) & (R - B > 50)
ys, xs = np.where(red)
pts = set(zip(ys.tolist(), xs.tolist()))
# connected components over the red pixels
best = None
seen = set()
for p0 in pts:
    if p0 in seen:
        continue
    dq = deque([p0]); seen.add(p0)
    box = [p0[1], p0[0], p0[1], p0[0]]; cnt = 0
    while dq:
        y, x = dq.popleft(); cnt += 1
        box[0] = min(box[0], x); box[1] = min(box[1], y); box[2] = max(box[2], x); box[3] = max(box[3], y)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                q = (y + dy, x + dx)
                if q in pts and q not in seen:
                    seen.add(q); dq.append(q)
    if best is None or cnt > best[0]:
        best = (cnt, box)
cnt, (x0, y0, x1, y1) = best
print(f'DRUID red blob: x={x0} y={y0} w={x1 - x0 + 1} h={y1 - y0 + 1} px={cnt} feet~({(x0 + x1) // 2},{y1}) height~{y1 - y0 + 1}')
# warrior band: left of the druid, below the sky
band = im[:, 0:x0 - 40]
Hb, Wb, _ = band.shape
Br, Bg, Bb = band[..., 0], band[..., 1], band[..., 2]
Bs = band.max(2) - band.min(2)
grass = (Bg >= Br) & (Bg >= Bb) & (Bs > 15)
sky = (Bb > Br + 8)
dark = (band.max(2) < 80) & (Bs < 40)
light = (band.min(2) > 140) & (Bs < 45)
chars = ~(grass | sky | dark | light)
ys2, xs2 = np.where(chars)
H2, _, _ = np.histogram2d(xs2, ys2, bins=[Wb // 8, Hb // 8])
from numpy.lib.stride_tricks import sliding_window_view
win = sliding_window_view(H2, (24, 40))
iy, ix = np.unravel_index(np.argmax(win), win.shape)
cx0, cy0 = ix * 8, iy * 8
cx1, cy1 = cx0 + 24 * 8, cy0 + 40 * 8
sel = (xs2 >= cx0 - 32) & (xs2 <= cx1 + 32)
fx = int(np.median(xs2[sel])); fy = int(ys2[sel].max()); fy0 = int(ys2[sel].min())
print(f'WARRIOR dense window: x={cx0} y={cy0} w={cx1 - cx0} h={cy1 - cy0}')
print(f'WARRIOR feet~({fx},{fy}) head~{fy0} height~{fy - fy0}')
