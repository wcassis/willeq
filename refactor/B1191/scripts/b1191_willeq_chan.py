#!/usr/bin/env python3
"""B1191: per-channel measurement for a WILLEQ harness frame dir (frame_*.png, 30fps).
Same sets/baseline as b1191_oracle_chan.py: tail = median of last 1.5 s (45 frames
at 30 fps); lit80 = max channel diff > 80; px>150 = max channel diff > 150.
Outputs window aggregates + peak frame (max lit80 n) per-channel stats.
"""
import os, sys
import numpy as np, cv2

fdir = sys.argv[1]
files = sorted((os.path.join(fdir, f) for f in os.listdir(fdir) if f.startswith('frame_') and f.endswith('.png')),
               key=lambda p: int(os.path.basename(p)[6:11]))
print('frames', len(files))

def load(p):
    return cv2.imread(p, cv2.IMREAD_COLOR)[:, :, ::-1].astype(np.float32)

frames = [load(p) for p in files]
tail = np.median(np.stack(frames[-45:]), axis=0)

def sets(img):
    mx = (img - tail).max(axis=2)
    return mx > 80, mx > 150

peak_idx = max(range(len(frames)), key=lambda i: sets(frames[i])[0].sum())
print(f'peak frame {peak_idx} (t={peak_idx/30:.2f}s)')

def chan_means(img, mask, name):
    if not mask.any():
        print(f'{name} n=0'); return
    px = img[mask]
    m = px.mean(axis=0)
    print(f'{name} n={int(mask.sum())} R={m[0]:.1f} G={m[1]:.1f} B={m[2]:.1f}')
    for c, nm in zip(range(3), 'RGB'):
        h, _ = np.histogram(px[:, c], bins=16, range=(0, 255))
        print(f'  {nm} hist16: ' + ' '.join(str(int(v)) for v in h))

acc = {'lit80': [], 'px>150': []}
for img in frames:
    l, p = sets(img)
    if l.any(): acc['lit80'].append(img[l])
    if p.any(): acc['px>150'].append(img[p])
for name, rows in acc.items():
    if rows:
        allpx = np.vstack(rows)
        m = allpx.mean(axis=0)
        print(f'{name} window n={allpx.shape[0]} R={m[0]:.1f} G={m[1]:.1f} B={m[2]:.1f}')

print('--- peak frame ---')
img = frames[peak_idx]
l, p = sets(img)
chan_means(img, l, 'lit80')
chan_means(img, p, 'px>150')
print('TAIL median per-channel:', ' '.join(f'{v:.1f}' for v in tail.mean(axis=(0, 1))))
