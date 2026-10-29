#!/usr/bin/env python3
# B1195: character extent in a tight box. fg = not grass, not sky, not dark-shadow.
# (no light exclusion: white/blonde hair is light + low-sat and IS the character.)
import sys
from PIL import Image
import numpy as np
im = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(np.int32)
x0, y0, x1, y1 = [int(v) for v in sys.argv[2:6]]
box = im[y0:y1, x0:x1]
R, G, B = box[..., 0], box[..., 1], box[..., 2]
Bs = box.max(2) - box.min(2)
grass = (G >= R) & (G >= B) & (Bs > 15) & (G > 40)
sky = (B > R + 8)
dark = (box.max(2) < 80) & (Bs < 40)
fg = ~(grass | sky | dark)
ys, xs = np.where(fg)
print(f'box=({x0},{y0})-({x1},{y1}) px={len(xs)}')
print(f'head_y={y0 + ys.min()} feet_y={y0 + ys.max()} height={ys.max() - ys.min()}')
print(f'x_lo={x0 + xs.min()} x_hi={x0 + xs.max()} x_center={x0 + int(xs.mean())}')
# row profile every 10px
for y in range(0, ys.max() - ys.min(), 10):
    row = (ys >= ys.min() + y) & (ys < ys.min() + y + 10)
    print(f'  y={y0 + ys.min() + y:4d}: {row.sum():5d} px')
