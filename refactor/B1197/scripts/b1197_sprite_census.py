#!/usr/bin/env python3
# B1197: sprite footprint census on RISE-ABOVE-BASELINE (not raw frames).
# willeq e432: baseline = e0 scene run (clean).
# oracle: baseline = median of last 38 frames (committed definition; contains
#   decaying tail light - stated as a caveat in the README).
import numpy as np
import cv2
import glob

def load(d):
    fs = sorted(glob.glob(f'{d}/f*.png'), key=lambda p: int(''.join(c for c in p if c.isdigit())))
    return fs, [cv2.imread(p, cv2.IMREAD_COLOR)[:, :, ::-1].astype(np.float32) for p in fs]

def census(im, label):
    mx = im.max(axis=2)
    m = (mx > 80).astype(np.uint8)
    n, labels, stats, _ = cv2.connectedComponentsWithStats(m, 8)
    areas = stats[1:, cv2.CC_STAT_AREA]
    if len(areas) == 0:
        print(f'  {label}: 0 components')
        return
    order = np.argsort(areas)[::-1]
    top5 = areas[order][:5]
    merged = (areas > 150).sum()
    print(f'  {label}: n={len(areas):5d} total={areas.sum():8d} median={np.median(areas):5.0f} '
          f'p90={np.percentile(areas, 90):6.0f} max={areas.max():6d} >150px={merged:4d} top5={top5.tolist()}')

base = '/home/user/kb-evidence/kb-b1197-rec35-blue/frames'
_, E = load(f'{base}/e432')
_, E0 = load(f'{base}/e0')
print('== willeq e432 isolated, rise above e0 scene ==')
for i in (120, 135, 147, 160, 180, 210, 240, 260):
    df = np.clip(E[i] - E0[i], 0, 255)
    census(df, f'f{i:4d} t={i/30.0:.2f}')

fs_o, O = load(f'{base}/oracle_e35')
base_o = np.median(np.stack(O[-38:]), axis=0)
print('== oracle, rise above last-38 median (tail light contaminates baseline) ==')
for i in (54, 66, 76, 87, 104, 129, 154, 190, 200):
    df = np.clip(O[i] - base_o, 0, 255)
    census(df, f'f{i+1:4d} t_o={i/25.0:.2f}')
