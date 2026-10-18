#!/usr/bin/env python3
"""B1053: emit fable's own Options window + control rects for a resolution.

Replicates options_layout.h exactly (the window is UNSCALED 400x452, centred
via placeScreenCentre and clamped on screen; the page is 387x402 at window
offset (PAGE_X, TITLE_H+TAB_H)). Prints:
  line 1:  the check_frame.py argument list  (wx wy ww wh pw ph px_off py_off)
  rest:    control rects, window-relative, one per line:  tab  item  x  y  w  h
          (a window-relative x = page_x_off + control.x, y = page_y_off + control.y)
"""
import re
import sys

HEADER = '/home/user/src/willeq-wt/kb-b1053-options-oracle/include/client/graphics/ui/options_layout.h'
header = open(HEADER).read()

WIN_W, WIN_H = 400, 452
TITLE_H, TAB_H = 28, 22
PAGE_W, PAGE_H = 387, 402
PAGE_X = (WIN_W - PAGE_W) // 2  # 6
PAGE_Y = TITLE_H + TAB_H        # 50

sw, sh = int(sys.argv[1]), int(sys.argv[2])
# placeScreenCentre then clampOnScreen:
x = (sw - WIN_W) // 2
y = (sh - WIN_H) // 2
if x + WIN_W > sw: x = sw - WIN_W
if y + WIN_H > sh: y = sh - WIN_H
if x < 0: x = 0
if y < 0: y = 0
wx, wy = x, y

pat = re.compile(r'\{Tab::(\w+),\s*Kind::\w+,\s*"([^"]+)",\s*'
                 r'(\\?-?\d+),\s*(\\?-?\d+),\s*(\d+),\s*(\d+),')
rows = pat.findall(header)
print(f'# {wx} {wy} {WIN_W} {WIN_H} {PAGE_W} {PAGE_H} {PAGE_X} {PAGE_Y}')
for tab, item, cx, cy, cw, ch in rows:
    # window-relative control origin:
    rx = PAGE_X + int(cx)
    ry = PAGE_Y + int(cy)
    print(f'{tab}\t{item}\t{rx}\t{ry}\t{cw}\t{ch}')
