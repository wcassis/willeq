#!/usr/bin/env python3
"""B1164: write video frames f_first..f_last (0-based decode index, the index fx_measure.py's
frames.tsv uses) of an oracle capture as <out>/v<frame:05d>.png.
usage: b1164_extract.py <video.mp4> <out_dir> <f_first> <f_last>"""
import sys, os, cv2
v, out, f0, f1 = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
os.makedirs(out, exist_ok=True)
c = cv2.VideoCapture(v); print('fps', c.get(cv2.CAP_PROP_FPS), 'frames', c.get(cv2.CAP_PROP_FRAME_COUNT))
i = 0
while i <= f1:
    ok, im = c.read()
    if not ok:
        break
    if i >= f0:
        cv2.imwrite(f'{out}/v{i:05d}.png', im)
    i += 1
