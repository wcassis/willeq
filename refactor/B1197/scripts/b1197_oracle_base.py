#!/usr/bin/env python3
# B1197: is the oracle's light still on during its last-38 baseline window?
# Oracle capture: 201 frames @ 25 fps = 8.04 s. Compare per-frame mean rise
# above frame 1 (the pre-cast frame) across the whole capture.
import numpy as np
import cv2
import glob

d = '/home/user/kb-evidence/kb-b1197-rec35-blue/frames/oracle_e35'
fs = sorted(glob.glob(f'{d}/f*.png'), key=lambda p: int(''.join(c for c in p if c.isdigit())))
print('n frames:', len(fs), fs[0].split('/')[-1], '..', fs[-1].split('/')[-1])
F = [cv2.imread(p, cv2.IMREAD_COLOR)[:, :, ::-1].astype(np.float32) for p in fs]
base = F[0]
print('frame 1 means (R/G/B):', base.mean(axis=(0, 1)).round(1))
print(f"{'i':>4} {'t_s':>6} {'lit80 rise':>12} {'R rise':>8} {'G rise':>8} {'B rise':>8}")
for i in (0, 10, 25, 50, 75, 100, 125, 150, 163, 170, 180, 190, 200):
    df = np.clip(F[i] - base, 0, 255)
    m = df.max(axis=2) > 80
    t = i / 25.0
    print(f'{i:4d} {t:6.2f} {m.sum():12d} {df[m, 0].mean() if m.any() else 0:8.1f} '
          f'{df[m, 1].mean() if m.any() else 0:8.1f} {df[m, 2].mean() if m.any() else 0:8.1f}')
# means of the last 38 vs frame 1
tail38 = np.mean(np.stack(F[-38:]), axis=0)
print('last-38 mean (R/G/B):', tail38.mean(axis=(0, 1)).round(1))
print('last-38 - frame1  :', (tail38 - base).mean(axis=(0, 1)).round(1))
