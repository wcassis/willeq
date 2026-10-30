#!/usr/bin/env python3
"""B1167: the landing's lit area and extent per run, in B1164's mask (refactor/B1164/scripts/b1164_regions.py:
max-RGB rise > 80 over the TAIL median baseline, inside the gate crop, minus exclusion boxes), at the
fixed b1130 target anchor (tools/model_viewer/fx_target_anchor.h: feet 940,615, 288 px), normalised
by the caster's 209 px (the gate's unit, cov) and by the target's 288 px (host heights, B1164's
window unit). Two masks: the whole crop, and B1164's window WIN = -1.5,0.6,0.16,1.8 target heights
round the target. Per run it prints the PEAK frame of the landing span (by whole-crop cov) and, at
that frame: cov (caster^2), the window's host^2, the share in the target's column (|dx| < 0.5), and
the lit mask's extent -- p5..p95 of dx and of up (target heights) and its row/column span (px).

usage: window_table.py <label> <frame_fmt> <act_frame> <fps> <span_first> <span_last> <tail_first> <tail_last>
  frame_fmt: python format with {f} (full-frame PNGs); act_frame: the OP_Action frame (float)
Exclusions (full-frame x,y,w,h, the B1130 e35 geometry's two name labels and cast_measure.py's two
UI boxes) are applied to every run alike."""
import sys, numpy as np, cv2
lab, fmt, act, fps, s0, s1, t0, t1 = sys.argv[1:9]
act = float(act); fps = float(fps); s0, s1, t0, t1 = int(s0), int(s1), int(t0), int(t1)
CX, CY, CW, CH = 150, 60, 990, 540
FX, FY, HPX, NPX = 940, 615, 288.0, 209.0
EXC = [(600, 40, 170, 75), (0, 0, 200, 800), (540, 315, 200, 45), (880, 265, 160, 45)]
def load(f):
    return cv2.imread(fmt.format(f=f))[CY:CY + CH, CX:CX + CW].astype(np.int16)
base = np.median(np.stack([load(f) for f in range(t0, t1 + 1)]), axis=0).astype(np.int16)
xx = np.arange(CW)[None, :] + CX; yy = np.arange(CH)[:, None] + CY
dx = (xx - FX) / HPX + 0 * yy; up = (FY - yy) / HPX + 0 * xx
keep = np.ones((CH, CW), bool)
for ex, ey, ew, eh in EXC:
    x0, y0 = max(ex - CX, 0), max(ey - CY, 0); x1, y1 = max(ex + ew - CX, 0), max(ey + eh - CY, 0)
    keep[y0:y1, x0:x1] = False
win = (dx >= -1.5) & (dx <= 0.6) & (up >= 0.16) & (up <= 1.8)
best = None
for f in range(s0, s1 + 1):
    m = ((load(f) - base).max(axis=2) > 80) & keep
    n = int(m.sum())
    if best is None or n > best[1]:
        best = (f, n, m)
f, n, m = best
w = m & win
col = w & (np.abs(dx) < 0.5)
ys, xs = np.nonzero(m)
pdx = np.percentile(dx[m], [5, 95]) if n else (0, 0)
pup = np.percentile(up[m], [5, 95]) if n else (0, 0)
print('label\tpeak_frame\tt_act\tcov_crop\twin_host2\twin_col_host2\twin_out_host2\tdx_p5\tdx_p95\tup_p5\tup_p95\tcols\trows')
print(f'{lab}\t{f}\t{(f - act) / fps:+.3f}\t{n / NPX**2:.3f}\t{w.sum() / HPX**2:.3f}\t{col.sum() / HPX**2:.3f}\t'
      f'{(w.sum() - col.sum()) / HPX**2:.3f}\t{pdx[0]:+.2f}\t{pdx[1]:+.2f}\t{pup[0]:+.2f}\t{pup[1]:+.2f}\t'
      f'{xs.min() + CX if n else 0}..{xs.max() + CX if n else 0}\t{ys.min() + CY if n else 0}..{ys.max() + CY if n else 0}')
