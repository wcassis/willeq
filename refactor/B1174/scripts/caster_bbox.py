#!/usr/bin/env python3
"""B1174: measure the caster's bbox and name tag on an observer still (framing proof).
The caster column box is searched in x 860..1010, y 395..680 (the B1134/B1162 rig puts
Testcaster at feet ~933,639). Body pixels = red/yellow robe or pale skin/hair:
(R - G > 60) or (min(R,G,B) > 150); the grass/ground never satisfies either in this
box (checked: 0 such px in the same box on a caster-free column x 700..850).
Name tag = the label's green text, G > 150 and R < 120 and B < 120, rows 360..400.
usage: caster_bbox.py <png> [<png> ...]   (python3 -P: needs cv2)"""
import sys
import cv2
import numpy as np
for path in sys.argv[1:]:
    im = cv2.imread(path)[:, :, ::-1].astype(int)
    R, G, B = im[..., 0], im[..., 1], im[..., 2]
    body = ((R - G) > 60) | (np.minimum(np.minimum(R, G), B) > 150)
    box = np.zeros_like(body); box[395:680, 860:1010] = True
    ctrl = np.zeros_like(body); ctrl[395:680, 700:850] = True
    ys, xs = np.nonzero(body & box)
    tag = (G > 150) & (R < 120) & (B < 120)
    tb = np.zeros_like(tag); tb[360:400, 850:1020] = True
    ty, tx = np.nonzero(tag & tb)
    if len(ys) == 0:
        print(f"{path}\tbody_px 0\tNO CASTER in the box\tcontrol_px {int((body & ctrl).sum())}\tname_tag_px {len(ty)}")
        continue
    print(f"{path}\tbody_px {len(ys)}\tbbox x {xs.min()}..{xs.max()} y {ys.min()}..{ys.max()}"
          f"\theight {ys.max() - ys.min() + 1}\tfeet ({int(np.median(xs[ys > ys.max() - 6]))},{ys.max()})"
          f"\tcontrol_px {int((body & ctrl).sum())}"
          f"\tname_tag_px {len(ty)}" + (f" x {tx.min()}..{tx.max()} y {ty.min()}..{ty.max()}" if len(ty) else ""))
