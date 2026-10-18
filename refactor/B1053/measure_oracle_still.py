#!/usr/bin/env python3
"""B1053: locate the Options window and its seven tab centres in an oracle
still (1280x800 crop at +0+30, so ROOT y = crop y + 30).

MEASURED, not assumed (verified against 05_options_probe_settle.png, lane 5,
2026-10-18: the window's top border row is y=251, the side borders are
x=59/x=459 -> 400 wide, the tab strip is 28px below the top and 22px tall,
per the XML). The oracle window is UNSCALED (defaults.ini: 400x452 at every
resolution) and drawn at its XML absolute location (RelativePosition false),
NOT fable's centred anchor -- which is exactly the flagged deviation the
side-by-side records.

Detection: the 3D scene's bright runs hug the screen edges (the y=168 run
starts at x=0; the character frame's runs start at x=924+); the Options
window's top border is a 380-430px run that STARTS inside the screen
(x0 >= 30) in the y 200..330 band. The tab borders inside the strip are the
brightness minima: the oracle draws the tab FACE bright and its borders dark.

Output (tab-separated, ROOT coordinates for plan click rows):
  # image=<w>x<h>
  # window ... (crop coords)
  # tabbounds ...
  window  x0  y0  x1  y1
  tab     i  caption  cx  cy
"""
from PIL import Image
import sys

img = Image.open(sys.argv[1]).convert('L')
w, h = img.size
px = img.load()
print(f'# image={w}x{h}')

def longest_run(vals, thr):
    best = (0, 0, 0)
    cur = 0
    s = 0
    for i, v in enumerate(vals):
        if v >= thr:
            if cur == 0:
                s = i
            cur += 1
            if cur > best[0]:
                best = (cur, s, i + 1)
        else:
            cur = 0
    return best

cand = []
for y in range(200, 330):
    run, s, e = longest_run([px[x, y] for x in range(w)], thr=95)
    # The window is 400 wide, drawn at its XML absolute x (RelativePosition
    # false) -- on lane 5's 1280x1024 Xorg it measured at crop x=60. The 3D
    # scene's bright runs hug the left edge (x0 < 30) or the character frame
    # (x0 >= 900), so an interior run of 380..470 in this band is the window.
    if 380 <= run <= 470 and 30 <= s <= 160:
        cand.append((run, y, s, e))
if not cand:
    print('# NO WINDOW BORDER FOUND')
    sys.exit(1)
cand.sort(reverse=True)
run, y_top, x0, x1 = cand[0]
print(f'# top border: run={run} y={y_top} x={x0}..{x1}')

def vcount(x, y0, y1, thr=90):
    return sum(1 for y in range(y0, min(y1, h)) if px[x, y] >= thr)

left = vcount(x0, y_top, y_top + 452)
right = vcount(x1 - 1, y_top, y_top + 452)
print(f'# side borders: left={left}/452 right={right}/452')
if left < 250 or right < 250:
    print('# SIDE BORDERS NOT COHERENT')
    sys.exit(1)

win_x0, win_y0 = x0, y_top
win_w, win_h = 400, 452
win_x1 = win_x0 + win_w
win_y1 = win_y0 + win_h
print(f'window\t{win_x0}\t{win_y0 + 30}\t{win_x1}\t{win_y1 + 30}')

# Tab strip: 28px title below the top border, 22px tall.
strip_y0 = win_y0 + 28
strip_y1 = strip_y0 + 22
# Column brightness profile across the strip (median of the middle rows).
cols = []
for x in range(win_x0, win_x1):
    vals = [px[x, y] for y in range(strip_y0 + 3, strip_y1 - 3)]
    vals.sort()
    cols.append(vals[len(vals) // 2])
# Local minima (darker than both neighbours' 5px-window minima by >= 8) are
# the tab borders; require >= 3 internal minima for 7 tabs.
minima = []
for i in range(1, len(cols) - 1):
    l = min(cols[max(0, i - 5):i])
    r = min(cols[i + 1:i + 6])
    if cols[i] <= l - 8 and cols[i] <= r - 8:
        minima.append(i + win_x0)
# merge adjacent
merged = []
for m in minima:
    if not merged or m - merged[-1] > 4:
        merged.append(m)
    else:
        merged[-1] = (merged[-1] + m) // 2
print(f'# strip y {strip_y0}..{strip_y1}: internal minima {merged}')
if len(merged) < 6:
    print(f'# NOT ENOUGH TAB BORDERS ({len(merged)})')
    sys.exit(1)
bounds = [win_x0 + 1] + merged[:6] + [win_x1 - 1]
print(f'# tabbounds {bounds}')
captions = ['General', 'Display', 'Mouse', 'Keys', 'Filters', 'Colors', 'Mail']
for i in range(7):
    cx = (bounds[i] + bounds[i + 1]) // 2
    cy = (strip_y0 + strip_y1) // 2
    print(f'tab\t{i}\t{captions[i]}\t{cx}\t{cy + 30}')
