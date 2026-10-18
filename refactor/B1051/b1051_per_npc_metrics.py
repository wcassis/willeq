#!/usr/bin/env python3
"""B1051 after-count leg -- x86 per-NPC texture-diversity metric (step 1 of the
card: "commit the pixel re-derivation script QA used (per-NPC gold/red fraction)
under refactor/B1051/ and run it on both" -- the ETC1 and RGBA stills).

QA's FAIL (MR !173 note 5811) re-derived the stills as: body-region
coarse-unique colours 60 (ETC1, ETC1-quantized) vs 462 (RGBA), gold fraction
0.001 vs 0.253, red fraction 0.000 vs 0.054. This script re-runs that measure
on any still the same way:

  * crop: the guard torso crop, x 0.30w..0.72w, y 0.12h..0.72h of the 1280x720
    frame (the crop b1051_pixel_check.py used for the device A/B; QA's
    numbers came from the same central-figure region of the torso stills);
  * gold fraction: pixels with r>150, g>120, b<120 (the guard's gold
    chest/tabard -- visible in the RGBA baseline, the pre-fix and the
    "fixed" ETC1 stills);
  * red fraction: r>150, g<110, b<110 (the emblem's red chevron);
  * coarse-unique: distinct cells of a 16-level-per-channel quantized
    histogram ((r,g,b)//16);
  * top-5 colour-bucket share: the pre-fix smear's signature is one dominant
    mottle (top bucket share ~0.5+); a multi-texture body spreads it.

A still that shows DISTINCT per-part armour (QA's pass bar) has gold, red and
dark-limb populations AND a coarse-unique count in the multi-texture band
(>~200 for this crop); the flat smear has gold ~0 and a single dominant bucket.
Prints one line per file; exit 0 always (the numbers are the result).
"""
import sys

try:
    from PIL import Image
    import numpy as np
except Exception as e:  # noqa: BLE001
    print("NOPIL", e)
    sys.exit(0)


def metrics(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    box = (int(w * 0.30), int(h * 0.12), int(w * 0.72), int(h * 0.72))
    a = np.asarray(im.crop(box), dtype=np.int32)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    gold = float(np.mean((r > 150) & (g > 120) & (b < 120)))
    red = float(np.mean((r > 150) & (g < 110) & (b < 110)))
    q = (a // 16)
    uniq = len(set(map(tuple, q.reshape(-1, 3).tolist())))
    flat = q.reshape(-1, 3).astype(np.int64)
    codes = flat[:, 0] * 256 * 256 + flat[:, 1] * 256 + flat[:, 2]
    vals, counts = np.unique(codes, return_counts=True)
    order = np.argsort(-counts)
    top5 = float(counts[order[:5]].sum() / codes.size)
    print(
        f"{path} crop={box} gold={gold:.4f} red={red:.4f} "
        f"coarse_unique={uniq} top5_share={top5:.3f}"
    )


for p in sys.argv[1:]:
    try:
        metrics(p)
    except FileNotFoundError:
        print(f"{p} MISSING")
