#!/usr/bin/env python3
"""B952: summarise --impostor-pixel-test CSVs per (resolution, distance, size).

usage: pixel_summary.py <csv> [<csv> ...]

Prints one markdown table row per (res, dist, size): variants, the model's
on-screen height (median/min/max px, measured against the background), the
predicted height (median), max_abs (median/max), and pixels differing at all
and by more than 32 as a share of the full model's footprint (median/max);
the interior (pixels both images cover) max-channel difference median/p90/max
(median over variants); and the silhouette mismatch (pixels exactly one image
covers) as a share of the footprint.
Numbers only -- nothing here judges a picture.
"""
import csv
import statistics
import sys
from collections import defaultdict


def main(paths):
    rows = defaultdict(list)
    for p in paths:
        with open(p) as f:
            for r in csv.DictReader(f):
                rows[(r["res"], int(float(r["dist"])), int(r["size"]))].append(r)
    print("| res | dist | size | variants | model h px med (min-max) | pred h px med | "
          "max_abs med / max | diff px / footprint med (max) | diff>32 px / footprint med (max) | "
          "interior diff med / p90 / max (medians) | silhouette px / footprint med (max) |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    for key in sorted(rows, key=lambda k: (k[0], k[1], k[2])):
        rs = rows[key]
        h = [int(r["model_h_px"]) for r in rs]
        pred = [float(r["model_h_px_pred"]) for r in rs]
        mx = [int(r["max_abs"]) for r in rs]
        fp = [max(1, int(r["full_px"])) for r in rs]
        d = [int(r["diff_px"]) / f for r, f in zip(rs, fp)]
        d32 = [int(r["diff32_px"]) / f for r, f in zip(rs, fp)]
        im = [int(r["interior_med"]) for r in rs]
        ip = [int(r["interior_p90"]) for r in rs]
        ix = [int(r["interior_max"]) for r in rs]
        sil = [int(r["silhouette_px"]) / f for r, f in zip(rs, fp)]
        print(f"| {key[0]} | {key[1]} | {key[2]} | {len(rs)} | {statistics.median(h):.0f} "
              f"({min(h)}-{max(h)}) | {statistics.median(pred):.1f} | "
              f"{statistics.median(mx):.0f} / {max(mx)} | {100*statistics.median(d):.1f}% "
              f"({100*max(d):.1f}%) | {100*statistics.median(d32):.1f}% ({100*max(d32):.1f}%) | "
              f"{statistics.median(im):.0f} / {statistics.median(ip):.0f} / {statistics.median(ix):.0f} | "
              f"{100*statistics.median(sil):.1f}% ({100*max(sil):.1f}%) |")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    main(sys.argv[1:])
