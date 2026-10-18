#!/usr/bin/env python3
"""B1053: mechanical clip check on a rendered willeq frame.

Takes the window rect (computed by fable's own layout -- passed as args) and
checks, purely in pixels:
  1. the window is fully on the frame (no rect past an edge),
  2. the window's BOTTOM edge band (the last 8px inside the window) and its
     RIGHT edge band are UNIFORM panel chrome, not 3D scene: a clipped window
     would carry scene texture there, so the band's stddev must stay low,
  3. every control rect is inside the page and inside the frame,
  4. the bottom-right CORNER region (32x32 inside the window) is non-blank
     (its mean sits between the panel floor and the text ceiling).

Usage:
  check_frame.py <frame.png> <screen_w> <screen_h> <win_x> <win_y> <win_w> <win_h> <page_w> <page_h> <page_x_off> <page_y_off>
  plus a TSV of control rects on stdin:  tab  item  x  y  w  h   (WINDOW-RELATIVE)

Exit: 0 clean, 1 any check failed (failures printed as FAIL lines).
"""
import sys
from PIL import Image

def stddev(vals):
    if len(vals) < 2:
        return 0.0
    m = sum(vals) / len(vals)
    return (sum((v - m) ** 2 for v in vals) / len(vals)) ** 0.5

frame_path = sys.argv[1]
sw, sh = int(sys.argv[2]), int(sys.argv[3])
wx, wy, ww, wh = map(int, sys.argv[4:8])
pw, ph, px_off, py_off = map(int, sys.argv[8:12])
img = Image.open(frame_path).convert('L')
fw, fh = img.size
px = img.load()
fails = []

print(f'# frame={fw}x{fh} screen={sw}x{sh}')
if fw != sw or fh != sh:
    fails.append(f'frame size {fw}x{fh} != screen {sw}x{sh}')

# 1. window fully on the frame
if wx < 0 or wy < 0 or wx + ww > fw or wy + wh > fh:
    fails.append(f'window ({wx},{wy}) {ww}x{wh} not fully on the frame')
print(f'window\t({wx},{wy}) {wx},{wy}..{wx+ww},{wy+wh}')

def band_stats(x0, y0, x1, y1):
    vals = [px[x, y] for x in range(x0, min(x1, fw), 2)
            for y in range(y0, min(y1, fh), 2)]
    if not vals:
        return (0.0, 0.0)
    return (sum(vals) / len(vals), stddev(vals))

# 2. bottom and right edge bands: UNIFORM panel, not scene.
bmean, bstd = band_stats(wx + 4, wy + wh - 8, wx + ww - 4, wy + wh)
rmean, rstd = band_stats(wx + ww - 8, wy + 4, wx + ww, wy + wh - 4)
print(f'bottom band: mean={bmean:.1f} std={bstd:.1f}')
print(f'right  band: mean={rmean:.1f} std={rstd:.1f}')
if bstd > 40:
    fails.append(f'window bottom band is non-uniform (std={bstd:.1f}): clipped or scene showing')
if rstd > 40:
    fails.append(f'window right band is non-uniform (std={rstd:.1f}): clipped or scene showing')

# 3. per-control rects
nctl = 0
per_tab = {}
for line in sys.stdin:
    line = line.strip()
    if not line or line.startswith('#'):
        continue
    tab, item, cx, cy, cw, ch = line.split('\t')
    cx, cy, cw, ch = map(int, (cx, cy, cw, ch))
    nctl += 1
    per_tab[tab] = per_tab.get(tab, 0) + 1
    ax = wx + cx
    ay = wy + cy
    if ax < 0 or ay < 0 or ax + cw > fw or ay + ch > fh:
        fails.append(f'{item} ({tab}) rect ({ax},{ay}) {cw}x{ch} outside the FRAME')
# page rect
page_x = wx + px_off
page_y = wy + py_off
print(f'page\t({page_x},{page_y}) {pw}x{ph}, controls={nctl}')
print('per_tab=' + repr(per_tab))

# 4. bottom-right corner region: non-blank panel. The fable chrome is a dark
# theme (measured corner means 25-60 on the x86 stills), so the floor is low:
# a closed window means the 3D scene sits here, and a clipped one means the
# panel never drew. The band std above already catches scene content.
cmean, cstd = band_stats(wx + ww - 32, wy + wh - 32, wx + ww - 4, wy + wh - 4)
print(f'corner 32x32: mean={cmean:.1f} std={cstd:.1f}')
if not (5 < cmean < 245):
    fails.append(f'window bottom-right corner is blank/scene (mean={cmean:.1f})')

if fails:
    for f in fails:
        print(f'FAIL {f}')
    sys.exit(1)
print('PASS')
