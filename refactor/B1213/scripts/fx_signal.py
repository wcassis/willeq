#!/usr/bin/env python3
"""B1213 -- worn-FX signal measurement on the ONE continuous observer video.

This is a SANITY instrument, not a verdict. Its contract (per the B1213 card):
  1. It MUST detect the positive control (row_index 0, Fiery Avenger) in the
     same video BEFORE any per-character number is reported. If it cannot, the
     run is an instrument defect and exits non-zero.
  2. It then reports a raw per-character signal (lit-pixel counts + colour) so
     Will has a quantitative trace to review alongside the video. It makes no
     glow/no-glow ruling: a character whose crop shows particles to the eye but
     whose signal is low is an instrument defect to note, not a "no glow".

The B1206 defect (its instrument said "no glow" on 12 legs Will saw glowing)
was a SELF-MEDIAN baseline: a continuous/animating glow is present in most
frames of the window, so the median absorbed it and the lit count read 0. The
fix here is a PER-PIXEL LOW-PERCENTILE baseline (P10) over the character's own
20 s window: the animating FX rises above it in most frames, while the constant
scene and the observer (who stands beside every target with its own sword) sit
at or below it and are subtracted. The target's on-screen position drifts along
the row (the fixed camera heading does not track the row), so the ROI is a wide
central band rather than a single pinned box; the per-pixel baseline, recomputed
per character, absorbs that drift and the observer.

usage:
  fx_signal.py --video <video.mp4> --t0 <video.mp4.t0>
               --timestamps <timestamps.tsv> --out <out_dir>
               [--th N] [--fps N] [--roi X1 Y1 X2 Y2]
outputs (in <out_dir>):
  fx_signal.tsv        row_index, char, items, frames, lit_max, lit_mean,
                       peak_rgb, detected
  crop_<char>.png      the peak frame cropped to the lit region (evidence crop)
"""
import argparse
import subprocess
import sys
from pathlib import Path

try:
    import cv2
    import numpy as np
except ImportError:
    sys.exit("needs cv2 + numpy")


def read_timestamps(path):
    rows = []
    for line in Path(path).read_text().splitlines():
        if not line or line.startswith("row_index"):
            continue
        p = line.split("\t")
        if len(p) < 5:
            continue
        rows.append({"row": p[0], "char": p[1], "items": p[2],
                     "hs": float(p[6]), "he": float(p[7])})
    return rows


def extract(video, t0, hs, he, out_dir, fps):
    out_dir.mkdir(parents=True, exist_ok=True)
    # hold a little past both ends so the window is fully covered
    start = max(0.0, hs - 1.0)
    dur = (he - hs) + 2.0
    cmd = ["ffmpeg", "-v", "error", "-ss", f"{start:.3f}", "-t", f"{dur:.3f}",
           "-i", str(video), "-vf", f"fps={fps}", str(out_dir / "f%04d.png")]
    subprocess.run(cmd, check=True)
    frames = sorted(out_dir.glob("f*.png"))
    return frames, start


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--video", required=True)
    ap.add_argument("--t0", required=True)
    ap.add_argument("--timestamps", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--th", type=int, default=30)
    ap.add_argument("--fps", type=int, default=2)
    ap.add_argument("--roi", type=int, nargs=4, default=[380, 180, 820, 760],
                    metavar=("X1", "Y1", "X2", "Y2"))
    ap.add_argument("--detect-th", type=int, default=40,
                    help="lit_max above which a character counts as detected")
    args = ap.parse_args()

    t0 = float(Path(args.t0).read_text().strip())
    video = Path(args.video)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    x1, y1, x2, y2 = args.roi

    rows = read_timestamps(args.timestamps)
    results = []
    for r in rows:
        fr, start = extract(video, t0, r["hs"], r["he"], out / "frames" / r["char"],
                            args.fps)
        if not fr:
            sys.exit(f"no frames for {r['char']}")
        ims = []
        for f in fr:
            im = cv2.imread(str(f))
            if im is None:
                sys.exit(f"frame read failed: {f}")
            ims.append(im.astype(np.int16))
        ims = [im[y1:y2, x1:x2] for im in ims]
        if any(im.shape[0] == 0 or im.shape[1] == 0 for im in ims):
            sys.exit(f"empty ROI for {r['char']}")
        # per-pixel P10 baseline over the window (the B1206-defect fix)
        base = np.percentile(np.stack(ims), 10, axis=0).astype(np.int16)
        series = []
        peak = None
        for k, im in enumerate(ims):
            rise = (im - base).max(axis=2)
            m = rise > args.th
            lit = int(m.sum())
            series.append(lit)
            if lit and (peak is None or lit > peak[0]):
                bgr = im[m].mean(axis=0)  # cv2 is BGR
                rgb = "%d,%d,%d" % (int(round(bgr[2])),
                                    int(round(bgr[1])),
                                    int(round(bgr[0])))
                peak = (lit, rgb, k)
        lit_max = max(series)
        lit_mean = float(np.mean(series))
        detected = lit_max > args.detect_th
        results.append((r, lit_max, lit_mean,
                        peak[1] if peak else "-", detected))
        # evidence crop: peak frame, cropped to the lit region + margin
        if peak:
            im = cv2.imread(str(fr[peak[2]]))
            sub = im[y1:y2, x1:x2]
            m = (sub.astype(np.int16) - base).max(axis=2) > args.th
            ys, xs = np.where(m)
            if len(xs):
                mx = 40
                cx1 = max(0, xs.min() - mx); cx2 = min(sub.shape[1], xs.max() + mx)
                cy1 = max(0, ys.min() - mx); cy2 = min(sub.shape[0], ys.max() + mx)
                crop = im[y1 + cy1:y1 + cy2, x1 + cx1:x1 + cx2]
                cv2.imwrite(str(out / f"crop_{r['char']}.png"), crop)

    # write the table, positive control first (it is row_index 0 already)
    with open(out / "fx_signal.tsv", "w") as f:
        f.write("row_index\tchar\titems\tframes\tlit_max\tlit_mean\tpeak_rgb\t"
                "detected\n")
        for r, lit_max, lit_mean, rgb, detected in results:
            f.write(f"{r['row']}\t{r['char']}\t{r['items']}\t{len(fr)}\t"
                    f"{lit_max}\t{lit_mean:.1f}\t{rgb}\t{int(detected)}\n")

    # the gate: positive control (row_index 0) must be detected
    pc = results[0]
    print("POSITIVE CONTROL (must detect): %s %s lit_max=%s rgb=%s detected=%s"
          % (pc[0]["char"], pc[0]["items"], pc[1], pc[3], int(pc[4])))
    if not pc[4]:
        print("INSTRUMENT DEFECT: positive control not detected; "
              "per-character numbers withheld", file=sys.stderr)
        sys.exit(2)
    n_det = sum(1 for x in results if x[4])
    print("detected %d/%d characters -> %s" % (n_det, len(results),
                                                out / "fx_signal.tsv"))
    for r, lit_max, lit_mean, rgb, detected in results:
        print("  %s %s lit_max=%s mean=%s rgb=%s det=%s"
              % (r["char"], r["items"], lit_max, f"{lit_mean:.0f}", rgb,
                 int(detected)))


if __name__ == "__main__":
    main()
