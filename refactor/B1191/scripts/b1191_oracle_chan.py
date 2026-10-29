#!/usr/bin/env python3
"""B1191: per-channel oracle measurement for one record, one:
 - window aggregate over px>150 and lit80 (max-channel-diff sets)
 - the PEAK frame (max lit80 n) with per-channel means + 16-bin histograms
Sets and baseline: same definitions as review/B1122/scripts/fx_timing.py
(tail = median of last 1.5 s; lit80 = max channel diff vs tail > 80;
px>150 = max channel diff vs tail > 150).
"""
import os, sys, subprocess, tempfile
import numpy as np

vid_dir, crop, peak, lo, hi, tailn = sys.argv[1:7]
peak, lo, hi, tailn = int(peak), int(lo), int(hi), int(tailn)
video = os.path.join(vid_dir, 'video.mp4')
x, y, w, h = [int(v) for v in crop.split(',')]
out = tempfile.mkdtemp(prefix='b1191_orc_')
n = hi - lo + 1
pr = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                     '-show_entries', 'stream=r_frame_rate', '-of', 'default=nw=1:nk=1', video],
                    capture_output=True, text=True)
num, den = pr.stdout.strip().split('/')
fps = float(num) / float(den)
cmd = ['ffmpeg', '-ss', str(lo / fps), '-i', video,
       '-v', 'error',
       '-vf', f'crop={w}:{h}:{x}:{y}',
       '-frames:v', str(n),
       os.path.join(out, 'f%06d.png')]
subprocess.run(cmd, check=True)
files = sorted(os.listdir(out))
print('extracted', len(files), 'frames at fps', fps)

import cv2
def load(name):
    bgr = cv2.imread(os.path.join(out, name), cv2.IMREAD_COLOR)
    return bgr[:, :, ::-1].astype(np.float32)  # RGB

frames = [load(f) for f in files]
tail = np.median(np.stack(frames[-tailn:]), axis=0)

def sets(img):
    diff = img - tail
    mx = diff.max(axis=2)
    lit = mx > 80
    px = mx > 150
    return lit, px

# per-frame lit80 n to locate the peak
peak_idx = max(range(len(frames)), key=lambda i: sets(frames[i])[0].sum())
print(f'peak frame idx {peak_idx} (video f{lo + peak_idx}; expected {peak}; delta {peak - (lo + peak_idx)})')

def chan_means(img, mask, name):
    if not mask.any():
        print(f'{name} n=0')
        return
    px = img[mask]
    m = px.mean(axis=0)
    print(f'{name} n={int(mask.sum())} R={m[0]:.1f} G={m[1]:.1f} B={m[2]:.1f}')
    hist = []
    for c in range(3):
        h, _ = np.histogram(px[:, c], bins=16, range=(0, 255))
        hist.append(' '.join(str(int(v)) for v in h))
    for c, htxt in zip('RGB', hist):
        print(f'  {c} hist16: {htxt}')

# window aggregates
lit_w, px_w = np.zeros(frames[0].shape[:2], bool), np.zeros(frames[0].shape[:2], bool)
acc = {'lit80': [], 'px>150': []}
for img in frames:
    l, p = sets(img)
    lit_w |= l; px_w |= p
    if l.any(): acc['lit80'].append(img[l])
    if p.any(): acc['px>150'].append(img[p])
for name, rows in acc.items():
    if rows:
        allpx = np.vstack(rows)
        m = allpx.mean(axis=0)
        print(f'{name} window n={allpx.shape[0]} R={m[0]:.1f} G={m[1]:.1f} B={m[2]:.1f}')

# peak frame stats
print('--- peak frame ---')
img = frames[peak_idx]
l, p = sets(img)
chan_means(img, l, 'lit80')
chan_means(img, p, 'px>150')
print('TAIL median per-channel:', ' '.join(f'{v:.1f}' for v in tail.mean(axis=(0, 1))))
print('DIR', out)
