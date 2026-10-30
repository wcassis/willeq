#!/usr/bin/env python3
"""B1198 (#517): the dirty-gate audit and the fbo delta, on a pair of run dirs.

The dirty gate (readback_candidates.h) skips a frame when its probe bytes
equal the previous frame's. Captured frames are the driver path's frames by
construction; the audit checks the SKIPPED ones offline against the driver
baseline run's pixels, the way tests/test_fx_harness.cpp does for its 12-frame
row: a skipped frame must be pixel-identical (whole PPM) to the baseline's
previous frame. A frame whose baseline pixels changed between two frames was
a false clean (something moved outside the probe) and the row says so.

The fbo half compares a baseline (driver) and fbo run frame by frame: the
number of differing pixels, the max channel delta, and where (bounding box)
the delta lives -- the "exactly which pixels and why" statement, with a frame.

Usage:
  dirty_audit.py --dirty <dir> --base <dir> [--fbo <dir>] [--out <dir>]

The dirs hold the run's frames (frame_%05d.ppm) + readback_cost.tsv. The
audit writes audit.tsv (one row per frame index) and, with --fbo,
fbo_delta.tsv (one row per frame) in --out (or the dirty dir).
"""
import argparse
import os
import re
import sys

FRAME = re.compile(r"frame_(\d{5})\.ppm$")


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:2] == b"P6", path
    _, rest = data.split(b"\n", 1)
    dims, rest = rest.split(b"\n", 1)
    w, h = map(int, dims.split())
    maxval, rest = rest.split(b"\n", 1)
    assert int(maxval) == 255
    px = rest
    assert len(px) == w * h * 3, (path, len(px), w * h * 3)
    return w, h, px


def frames_in(dir):
    out = {}
    for name in os.listdir(dir):
        m = FRAME.match(name)
        if m:
            out[int(m.group(1))] = os.path.join(dir, name)
    return out


def readback_cost(dir):
    out = {}
    with open(os.path.join(dir, "readback_cost.tsv")) as f:
        f.readline()  # header
        for line in f:
            c = line.rstrip("\n").split("\t")
            out[int(c[0])] = {"raw": float(c[1]), "copy": float(c[2]),
                              "blit": float(c[3]), "post": float(c[4]),
                              "skipped": c[5] == "1"}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dirty", required=True, help="the dirty run's frames dir")
    ap.add_argument("--base", required=True, help="the driver baseline run's frames dir")
    ap.add_argument("--fbo", default="", help="optional fbo run's frames dir")
    ap.add_argument("--out", default="", help="output dir (default: the dirty dir)")
    args = ap.parse_args()
    out_dir = args.out or args.dirty

    base = frames_in(args.base)
    dirty = frames_in(args.dirty)
    if not base:
        print(f"FAIL: no frame_*.ppm in {args.base}", file=sys.stderr)
        return 1
    cost = readback_cost(args.dirty)
    false_clean = 0
    rows = []
    for idx in sorted(base):
        if idx not in dirty:
            # skipped: the baseline must show the frame unchanged from the
            # previous frame (the probe said clean; the pixels must agree).
            prev = idx - 1
            if prev not in base:
                print(f"FAIL: frame 0 was skipped", file=sys.stderr)
                return 1
            w1, h1, p1 = read_ppm(base[prev])
            w2, h2, p2 = read_ppm(base[idx])
            ok = (w1, h1) == (w2, h2) and p1 == p2
            if not ok:
                false_clean += 1
            rows.append(f"{idx}\t1\t{cost.get(idx, {}).get('raw', float('nan')):.3f}"
                        f"\t{'clean' if ok else 'FALSE-CLEAN'}\n")
        else:
            w1, h1, p1 = read_ppm(base[idx])
            w2, h2, p2 = read_ppm(dirty[idx])
            ok = (w1, h1) == (w2, h2) and p1 == p2
            if not ok:
                print(f"FAIL: captured frame {idx} differs from the baseline", file=sys.stderr)
                return 1
            rows.append(f"{idx}\t0\t{cost.get(idx, {}).get('raw', float('nan')):.3f}\tcaptured-identical\n")
    with open(os.path.join(out_dir, "audit.tsv"), "w") as f:
        f.write("frame\tskipped\tprobe_ms\tverdict\n")
        f.writelines(rows)
    n_skip = sum(1 for r in rows if r.split("\t")[1] == "1")
    n_cap = len(rows) - n_skip
    clean_med = sorted(r.split("\t")[2] for r in rows if r.split("\t")[1] == "1")
    print(f"audit: {len(rows)} frames, {n_cap} captured, {n_skip} skipped, "
          f"{false_clean} false-clean; clean-frame probe median "
          f"{clean_med[len(clean_med)//2] if clean_med else '-'} ms")

    if args.fbo:
        fbo = frames_in(args.fbo)
        rows = []
        for idx in sorted(base):
            w1, h1, p1 = read_ppm(base[idx])
            w2, h2, p2 = read_ppm(fbo[idx])
            assert (w1, h1) == (w2, h2), idx
            n = len(p1) // 3
            diff = 0
            maxd = 0
            x0 = y0 = 1 << 30
            x1 = y1 = -1
            for i in range(n):
                o = i * 3
                if p1[o] != p2[o] or p1[o + 1] != p2[o + 1] or p1[o + 2] != p2[o + 2]:
                    diff += 1
                    x, y = i % w1, i // w1
                    x0, y0 = min(x0, x), min(y0, y)
                    x1, y1 = max(x1, x), max(y1, y)
                maxd = max(maxd, abs(p1[o] - p2[o]), abs(p1[o + 1] - p2[o + 1]),
                           abs(p1[o + 2] - p2[o + 2]))
            rows.append(f"{idx}\t{diff}\t{100.0 * diff / n:.2f}\t{maxd}"
                        f"\t{x0},{y0},{x1},{y1}\n")
        with open(os.path.join(out_dir, "fbo_delta.tsv"), "w") as f:
            f.write("frame\tdiff_px\tpct\tmax_channel_delta\tbbox\n")
            f.writelines(rows)
        first = rows[0].split("\t")
        print(f"fbo: frame 0 differs at {first[1]} px ({first[2]} %), max channel delta "
              f"{first[3]}, bbox {first[4]}")

    return 0 if false_clean == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
