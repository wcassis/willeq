#!/usr/bin/env python3
"""T310 Leg C -- score a fable frame against an official-client oracle frame.

WHY THIS IS NOT A PIXEL DIFF. fable and the official Titanium client are two
different renderers over the same assets: they disagree on texture filtering,
lighting, gamma and fog even when the camera pose is IDENTICAL. A raw pixel
difference therefore never reaches zero and its absolute value carries no
information. What DOES carry information is where the same world edges land on
screen, because that is a pure function of the camera pose.

So the primary metric is a GRADIENT metric:

    edge = 1 - NCC(|grad(oracle)|, |grad(candidate)|)

normalised cross-correlation of the Sobel gradient magnitude, on greyscale,
after a light blur. It is invariant to the two renderers' brightness/contrast
offsets and keys on the geometry. 0.0 == the edges land in the same places;
1.0 == no relationship. THE CURVE'S MINIMUM OVER A SWEPT PARAMETER IS THE
MEASUREMENT; the curve's width is the error bar. A single score is not a
result.

`mad` (mean absolute greyscale difference, 0..1) is reported alongside as a
sanity companion -- it is the metric that CANNOT be trusted across renderers,
and it is printed precisely so a reader can see the two disagree.

Usage:
  camera_match_score.py --oracle O.png --candidate C.png [--crop WxH+X+Y]
                        [--candidate-dir DIR]  (score every frame, report the best)
                        [--tsv]
"""
import argparse, glob, os, sys
import numpy as np
from PIL import Image


def load_gray(path, crop=None, size=None):
    im = Image.open(path).convert("L")
    if crop:
        w, h, x, y = crop
        im = im.crop((x, y, x + w, y + h))
    if size and im.size != size:
        im = im.resize(size, Image.BILINEAR)
    return np.asarray(im, dtype=np.float64) / 255.0


def blur3(a):
    k = np.array([1.0, 2.0, 1.0])
    k = k / k.sum()
    out = np.apply_along_axis(lambda m: np.convolve(m, k, mode="same"), 0, a)
    out = np.apply_along_axis(lambda m: np.convolve(m, k, mode="same"), 1, out)
    return out


def gradmag(a):
    a = blur3(a)
    gx = np.zeros_like(a); gy = np.zeros_like(a)
    gx[:, 1:-1] = a[:, 2:] - a[:, :-2]
    gy[1:-1, :] = a[2:, :] - a[:-2, :]
    return np.sqrt(gx * gx + gy * gy)


def ncc(a, b):
    a = a - a.mean(); b = b - b.mean()
    da = np.sqrt((a * a).sum()); db = np.sqrt((b * b).sum())
    if da == 0 or db == 0:
        return 0.0
    return float((a * b).sum() / (da * db))


def score(oracle_g, cand_path, crop):
    c = load_gray(cand_path, crop, size=(oracle_g.shape[1], oracle_g.shape[0]))
    edge = 1.0 - ncc(gradmag(oracle_g), gradmag(c))
    mad = float(np.abs(oracle_g - c).mean())
    return edge, mad


def parse_crop(s):
    if not s:
        return None
    # WxH+X+Y  (ImageMagick geometry)
    wh, _, rest = s.partition("+")
    x, _, y = rest.partition("+")
    w, _, h = wh.partition("x")
    return (int(w), int(h), int(x), int(y))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--oracle", required=True)
    p.add_argument("--candidate")
    p.add_argument("--candidate-dir")
    p.add_argument("--glob", default="frame_*.png")
    p.add_argument("--crop", default="")
    # The oracle is an x11grab of the official client's window, which Wine
    # places at root (-1, 30) with a 1024x768 geometry -- so a grab at
    # ":99+0,30" (note the COMMA; "+0+30" silently means y=0) yields 1023
    # columns starting at the window's column 1. fable's own dump is a full
    # 1024. --candidate-crop exists so the two are aligned by CROPPING rather
    # than by a resample that would quietly rescale the thing being measured.
    p.add_argument("--candidate-crop", default="")
    p.add_argument("--tsv", action="store_true")
    a = p.parse_args()

    crop = parse_crop(a.crop)
    ccrop = parse_crop(a.candidate_crop) or crop
    og = load_gray(a.oracle, crop)

    cands = []
    if a.candidate:
        cands = [a.candidate]
    if a.candidate_dir:
        cands += sorted(glob.glob(os.path.join(a.candidate_dir, a.glob)))
    if not cands:
        print("no candidate frames", file=sys.stderr)
        return 2

    rows = [(f,) + score(og, f, ccrop) for f in cands]
    if a.tsv:
        for f, e, m in rows:
            print("%s\t%.6f\t%.6f" % (os.path.basename(f), e, m))
    best = min(rows, key=lambda r: r[1])
    print("BEST\t%s\t%.6f\t%.6f" % (os.path.basename(best[0]), best[1], best[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
