#!/usr/bin/env python3
# B1195: precise feet/head/height by vertical width profile in a narrow x band around a character.
# feet = lowest row where the non-background run width < 60 (the legs); below it the shadow widens.
# head = top of the tallest contiguous non-background vertical run (the model, not the nameplate).
import sys
from PIL import Image
import numpy as np
im = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(np.int32)
h, w, _ = im.shape
cx = int(sys.argv[2]); half = 18
band = im[:, cx - half:cx + half]
R, G, B = band[..., 0], band[..., 1], band[..., 2]
Bs = band.max(2) - band.min(2)
bg = ((G >= R) & (G >= B) & (Bs > 15) & (G > 40)) | (B > R + 8) | (band.max(2) < 80) & (Bs < 40) | (band.min(2) > 140) & (Bs < 45)
fg = ~bg
widths = fg.sum(1)
# feet: from the bottom (y=780 up) skip grass/bg rows; first fg row with width<60
feet = None
for y in range(780, 300, -1):
    if widths[y] > 3 and widths[y] < 60:
        feet = y
        break
    if widths[y] == 0 and feet is None:
        continue
# head: contiguous fg runs in width profile, keep the tallest
runs = []
y = 300
while y < 780:
    if widths[y] > 5:
        y0 = y
        while y < 780 and widths[y] > 2:
            y += 1
        runs.append((y - y0, y0, y))
    else:
        y += 1
if runs:
    L, y0, y1 = max(runs)
    print(f'cx={cx}: feet~{feet} head~{y0} height~{feet - y0 if feet else None} (tallest run {L}px, y {y0}-{y1})')
else:
    print(f'cx={cx}: no character run found')
