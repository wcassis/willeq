#!/usr/bin/env python3
"""B1159: the per-frame gap-box census of the leg video, the B1152 method
(review/B1152/TRACE.md sec. 8: "the deep-gap census -- the bright pixel count in a
fixed box x 660-760, y 440-620 (the observer's standing spot in the observer's
own video), threshold grayscale >= 180 -- against the video frames").

usage: census_frames.py <capture dir> <out.tsv>
  reads <capture dir>/video.mp4 (frame 0 = video.calibration.tsv calibrated_t0);
  writes one row per frame: frame, t_video (seconds from frame 0; fps 30), bright_px;
  plus a summary of the excursions above the pre-cast baseline (the B1152 per-cell
  census format: frames, t range, counts).
"""
import sys
from pathlib import Path

import cv2

cap, out = Path(sys.argv[1]), Path(sys.argv[2])
off = float([l.split('\t') for l in open(cap / 'video.calibration.tsv')][1][3])
FPS = 30
X0, X1, Y0, Y1, TH = 660, 760, 440, 620, 180

v = cv2.VideoCapture(str(cap / 'video.mp4'))
rows = []
i = 0
while True:
    ok, f = v.read()
    if not ok:
        break
    box = f[Y0:Y1, X0:X1]
    g = cv2.cvtColor(box, cv2.COLOR_BGR2GRAY)
    rows.append((i, i / FPS, int((g >= TH).sum())))
    i += 1
v.release()

base = sum(c for _, _, c in rows[:60]) / max(1, len(rows[:60]))
thr = max(35, base * 3)
print(f'frames {i}; baseline (first 2 s) bright_px = {base:.0f}; excursion threshold = {thr:.0f}')
runs = []
cur = None
for fr, tv, c in rows:
    if c >= thr:
        cur = [fr, fr, c, c, tv, tv] if cur is None else cur
        if cur is not None:
            cur[0] = min(cur[0], fr); cur[1] = max(cur[1], fr)
            cur[2] = min(cur[2], c); cur[3] = max(cur[3], c)
            cur[4] = min(cur[4], tv); cur[5] = max(cur[5], tv)
    else:
        if cur:
            runs.append(cur); cur = None
if cur:
    runs.append(cur)
for r in runs:
    print(f'RUN frames {r[0]}-{r[1]}  t_video {r[4]:+.3f}..{r[5]:+.3f}  bright {r[2]}..{r[3]}')
with open(out, 'w') as s:
    s.write('frame\tt_video\tbright_px\n')
    for fr, tv, c in rows:
        s.write(f'{fr}\t{tv:+.6f}\t{c}\n')
print(f'wrote {out} ({i} rows)')
