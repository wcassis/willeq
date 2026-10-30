#!/usr/bin/env python3
# B1195 probe: extract observer-video frames at named times and diff each against the t=465
# baseline; report the lit-pixel bbox + centroid (max-channel rise > 80, as fx_timing.py).
import sys, subprocess, cv2, numpy as np

VIDEO = sys.argv[1]
OUT = sys.argv[2]
TIMES = [float(t) for t in sys.argv[3:]]
base_t = TIMES[0]

def grab(t):
    p = f'{OUT}/f_{t:.0f}.png'
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', str(t), '-i', VIDEO, '-frames:v', '1', p], check=True)
    return p

base = cv2.imread(grab(base_t)).astype(np.int32)
for t in TIMES:
    im = cv2.imread(grab(t)).astype(np.int32)
    rise = im - base
    m = rise.max(axis=2) > 80
    n = int(m.sum())
    if n:
        ys, xs = np.where(m)
        print(f't={t:6.1f} lit={n:6d} bbox=({xs.min()},{ys.min()})-({xs.max()},{ys.max()}) '
              f'centroid=({xs.mean():.0f},{ys.mean():.0f})')
    else:
        print(f't={t:6.1f} lit=0')
