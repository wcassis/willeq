#!/usr/bin/env python3
"""B1198 (#517): the README's per-candidate readback table.

Reads each run's frame_cost.tsv (B1193's per-phase split: frame, render_ms,
readback_ms, ...) and readback_cost.tsv (the candidate's sub-phases, one row
per frame for every mode but driver), and prints:

  Table A -- per run, the render-thread per-frame cost.
    driver/glread/pbo/fbo: readback_ms per frame (the candidate's whole
      phase on the render thread; the sub-phases are in readback_cost.tsv).
    dirty: the merged per-frame cost -- a captured frame is frame_cost's
      readback_ms (the probe PLUS the full readback), a skipped frame is
      readback_cost's raw_ms (the probe alone) -- plus the skip census.
    amortized_ms = the mean over ALL frames (the number the fps pays);
    fps = 1000 / (render mean + amortized_ms): render and readback are
    serial on the render thread, so that is its per-frame ceiling.

Usage:
  build_tables.py <EV dir>     # the b1198_driver.sh $EV (runs + summary)

The output is markdown, ready for refactor/B1198/README.md.
"""
import os
import sys


def frame_cost(d):
    """frame_cost.tsv -> {frame: (render_ms, readback_ms)}."""
    out = {}
    with open(os.path.join(d, "frame_cost.tsv")) as f:
        f.readline()
        for line in f:
            c = line.rstrip("\n").split("\t")
            out[int(c[0])] = (float(c[1]), float(c[2]))
    return out


def rb_cost(d):
    """readback_cost.tsv -> {frame: (raw_ms, skipped)}. Missing for driver."""
    out = {}
    p = os.path.join(d, "readback_cost.tsv")
    if not os.path.exists(p):
        return out
    with open(p) as f:
        f.readline()
        for line in f:
            c = line.rstrip("\n").split("\t")
            out[int(c[0])] = (float(c[1]), c[5] == "1")
    return out


def mean(xs):
    return sum(xs) / len(xs) if xs else float("nan")


def med(xs):
    if not xs:
        return float("nan")
    xs = sorted(xs)
    return xs[len(xs) // 2]


def summary(d):
    """c1/summary.tsv is data-only (one tab-separated row per run, no
    header): tag readback conc frames wall_s render_ms_median
    readback_ms_median load_at_start."""
    wall = load = float("nan")
    p = os.path.join(d, "summary.tsv")
    if os.path.exists(p):
        with open(p) as f:
            for line in f:
                c = line.rstrip("\n").split("\t")
                if len(c) > 7:
                    try:
                        wall, load = float(c[4]), float(c[7])
                    except ValueError:
                        pass
    return wall, load


def main(ev):
    byrow = {}
    for name in sorted(os.listdir(ev)):
        d = os.path.join(ev, name)
        if not os.path.isdir(d) or not os.path.exists(os.path.join(d, "c1", "frame_cost.tsv")):
            continue
        # <row>_<mode>_c<conc>_s<secs> with row in {tour_93, b1130_r2} and
        # mode in {driver, glread, pbo, fbo, dirty}: split from the right.
        head, tail = name.rsplit("_c", 1)
        row, mode = head.rsplit("_", 1)
        byrow.setdefault(row, {})[mode] = os.path.join(d, "c1")

    print("| row | candidate | frames | captured | skipped | render ms/f | readback ms/f (amortized) | readback median (captured) | clean-frame probe ms | fps ceiling | wall s | load at start |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|")
    for row in sorted(byrow):
        for mode in ["driver", "glread", "pbo", "fbo", "dirty"]:
            d = byrow[row].get(mode)
            if not d:
                continue
            fc = frame_cost(d)
            rc = rb_cost(d)
            render = [v[0] for v in fc.values()]
            readback = [v[1] for v in fc.values()]
            if mode == "dirty":
                # every frame in rc: captured (probe + full, from fc) or
                # skipped (probe only, from rc).
                amortized, clean_probe = [], []
                for i in sorted(rc):
                    raw, skipped_flag = rc[i]
                    amortized.append(fc[i][1] if i in fc and not skipped_flag else raw)
                    if skipped_flag:
                        clean_probe.append(raw)
                n_frames = len(rc)
                n_captured = sum(1 for i in rc if not rc[i][1])
                n_skipped = n_frames - n_captured
            else:
                amortized, clean_probe = readback, []
                n_frames = n_captured = len(fc)
                n_skipped = 0
            wall, load = summary(d)
            fps = 1000.0 / (mean(render) + mean(amortized))
            clean = f"{med(clean_probe):.2f}" if clean_probe else "-"
            print(f"| {row} | {mode} | {n_frames} | {n_captured} | {n_skipped} | "
                  f"{mean(render):.2f} | {mean(amortized):.2f} | {med(readback):.2f} | "
                  f"{clean} | {fps:.1f} | {wall:.2f} | {load:.2f} |")


if __name__ == "__main__":
    main(sys.argv[1])
