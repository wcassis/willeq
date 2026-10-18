#!/usr/bin/env python3
# Vendored 2026-10-13 from /home/user/eq-oracle/uigap-scripts/step_frames.py.
"""step_frames.py <base> — for every BEGIN/END step in <base>.timeline.tsv,
step the video <base>.mp4 frame by frame (10 fps) and report, mechanically:

  click_frame  : video frame index of the BEGIN mark
  change_frame : first frame after BEGIN where the 56x56 patch centred on the
                 click point (END-mark pointer position) differs from the
                 BEGIN frame by mean |diff| > 12 (0-255 scale) — i.e. when the
                 slot/cursor area visibly changed (icon lifted / dropped /
                 dialog drawn)
  settle_frame : first frame after which that patch stays unchanged (<4) for
                 the rest of the step
  cursor_icon_end : mean |diff| of the patch 8..48 px down-right of the
                 pointer between the END frame and the BEGIN frame (> 12 means
                 something new is drawn at the pointer = an item icon on the
                 cursor appeared; a drop from > 12 back means it left)
Output TSV on stdout. Video x = window x - 1 (window origin is x=-1; see
<base>.mp4.geom).
"""
import sys, subprocess, numpy as np, os, tempfile
from PIL import Image

base = sys.argv[1]
geom = open(base + '.mp4.geom').read().split()
# window origin parse: "... (window origin -1,30)"
g = open(base + '.mp4.geom').read()
worig = g.split('window origin ')[1].rstrip(')\n').split(',')
WX = int(worig[0]); GX = int(geom[0])
dx = WX - GX  # video_x = window_x + dx

rows = [l.rstrip('\n').split('\t') for l in open(base + '.timeline.tsv')]
steps = []
cur = None
for r in rows:
    if len(r) < 4:
        continue
    lab = r[3]
    if lab.startswith('BEGIN '):
        cur = dict(name=lab.split()[1], t0=float(r[1]))
    elif lab.startswith('END ') and cur and lab.split()[1] == cur['name']:
        x, y = map(int, r[2].split(','))
        cur.update(t1=float(r[1]), x=x + dx, y=y)
        steps.append(cur); cur = None

tmp = tempfile.mkdtemp()
print('step\tclick_frame\tchange_frame\tchange_s_after_begin\tsettle_frame\tcursor_patch_diff_end\tpointer_xy_video')
for s in steps:
    out = os.path.join(tmp, s['name'])
    os.makedirs(out, exist_ok=True)
    subprocess.run(['ffmpeg', '-v', 'error', '-ss', '%.2f' % s['t0'], '-t', '%.2f' % (s['t1'] - s['t0'] + 0.3),
                    '-i', base + '.mp4', '-vf', 'fps=10', os.path.join(out, 'f%04d.png')], check=False)
    fr = sorted(os.listdir(out))
    if not fr:
        print('%s\t-\t-\t-\t-\t-\t-' % s['name']); continue
    imgs = [np.asarray(Image.open(os.path.join(out, f)).convert('L'), dtype=np.int16) for f in fr]
    H, W = imgs[0].shape
    x, y = s['x'], s['y']
    def patch(a, x0, y0, x1, y1):
        return a[max(0, y0):min(H, y1), max(0, x0):min(W, x1)]
    p0 = patch(imgs[0], x - 28, y - 28, x + 28, y + 28)
    diffs = [np.abs(patch(a, x - 28, y - 28, x + 28, y + 28) - p0).mean() for a in imgs]
    ch = next((i for i, d in enumerate(diffs) if d > 12), None)
    settle = None
    for i in range(len(imgs) - 1, 0, -1):
        if np.abs(patch(imgs[i], x - 28, y - 28, x + 28, y + 28) - patch(imgs[i - 1], x - 28, y - 28, x + 28, y + 28)).mean() >= 4:
            settle = i; break
    c0 = patch(imgs[0], x + 8, y + 8, x + 48, y + 48)
    c1 = patch(imgs[-1], x + 8, y + 8, x + 48, y + 48)
    cd = np.abs(c1 - c0).mean() if c0.shape == c1.shape else -1
    f0 = int(round(s['t0'] * 10))
    print('%s\t%d\t%s\t%s\t%s\t%.1f\t%d,%d' % (s['name'], f0, '-' if ch is None else f0 + ch,
          '-' if ch is None else '%.1f' % (ch / 10.0), '-' if settle is None else f0 + settle, cd, x, y))
