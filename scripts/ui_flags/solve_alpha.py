#!/usr/bin/env python3
"""B1002 (#322): solve the official client's Style_Transparent blend alpha.

Two-backdrop per-pixel solve (plan: scripts/eqemu/oracle/plans/b1002_alpha.tsv,
v4). Four oracle frames: two with the window OPEN over backdrop B1 (camera 1)
and two over B2 (camera 2 — a time-of-day change, not a camera turn; v4's
finding was that a yaw turn at night gives no signal because the ground is
uniform). Per body pixel p:

    O1[p] = a*f + (1-a)*B1[p]
    O2[p] = a*f + (1-a)*B2[p]

Two equations, two unknowns (a, f); subtracting eliminates f:

    a = 1 - (O2-O1)/(B2-B1)

The fill's value a is the ROBUST MEDIAN over the solve pixels (the window's
TEXT differs between frames and is an outlier; the fill is the bulk). A window
that composites opaque gives a = 1 (255) on every pixel; a translucent one
gives a < 1 on the fill pixels.

Usage: solve_alpha.py <evidence-dir>
Frames are <dir>/{05_c1_closed,07_o1_open,26_c2_closed,28_o2_open}.png.
"""
import sys

import numpy as np
from PIL import Image


def main() -> int:
    d = sys.argv[1] if len(sys.argv) > 1 else "."
    D = d + "/" if not d.endswith("/") else d

    def L(n):
        return np.asarray(Image.open(D + n + ".png").convert("RGB"), dtype=np.float64)

    B1 = L("05_c1_closed")
    O1 = L("07_o1_open")
    B2 = L("26_c2_closed")
    O2 = L("28_o2_open")

    # The window rect (stable screen position across all 4 frames; located by
    # the pixels that differ from the closed frame in BOTH open frames).
    x0, y0, x1, y1 = 426, 341, 839, 799
    R = lambda A: A[y0:y1, x0:x1]
    bD = R(B2) - R(B1)
    oD = R(O2) - R(O1)
    maxb = np.abs(bD).max(2)
    mask = maxb > 20
    print(f"solve pixels (|B2-B1| max-channel > 20 inside window): {int(mask.sum())}")
    print(f"window rect: x {x0}-{x1}  y {y0}-{y1}")

    a = 1.0 - oD / np.where(np.abs(bD) < 1e-6, 1e-6, bD)
    a_px = a[mask]
    w = maxb[mask]
    res = []
    for ch in range(3):
        col = a_px[:, ch]
        wm = float(np.average(col, weights=w))
        med = float(np.median(col))
        lo, hi = np.percentile(col, [5, 95])
        res.append(wm)
        print(
            f"  ch{ch}: weighted a={wm*255:7.2f}/255  median={med*255:7.2f}  "
            f"p5={lo*255:6.1f} p95={hi*255:6.1f}  n={len(col)}"
        )
    print("weighted mean of 3 channels (255 scale):", round(float(np.mean(res)) * 255, 2))
    print("MEDIAN channel alpha (255 scale):", int(round(float(np.median(res)) * 255)))
    frac_transl = float(np.mean((a_px < 0.98).any(1)))
    print(f"fraction of solve pixels with any channel a<0.98 (translucent): {frac_transl:.2%}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
