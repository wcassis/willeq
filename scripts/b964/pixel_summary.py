#!/usr/bin/env python3
"""B964: medians over the race/gender variants of impostor_pixel_compare's
pixel.csv, per distance and billboard variant (0 = the shipped program,
1 = the built-in alpha test: no un-premultiply, no fog, B952's billboard,
2 = the shipped program with its fog off). Shares are of the full model's
footprint. dark_edge = impostor pixels near black where the model is not
(the #274 fringe), summed over the variants.

usage: pixel_summary.py pixel.csv [pixel.csv...]
"""
import csv
import statistics as st
import sys
from collections import defaultdict

print("| file | dist | variant | n | model px | impostor px | height px | silhouette mismatch | diff > 32 | interior diff med / p90 | max diff | dark-edge px (sum) |")
print("|---|---|---|---|---|---|---|---|---|---|---|---|")
for path in sys.argv[1:]:
    rows = list(csv.DictReader(open(path)))
    g = defaultdict(list)
    for r in rows:
        g[(int(r["dist"]), int(r["variant"]))].append(r)
    for (dist, var), rs in sorted(g.items()):
        med = lambda c: st.median(float(r[c]) for r in rs)
        share = lambda c: st.median(100.0 * float(r[c]) / max(1.0, float(r["full_px"])) for r in rs)
        print("| %s | %d | %d | %d | %.0f | %.0f | %.0f | %.1f %% | %.1f %% | %.0f / %.0f | %.0f | %d |" % (
            path.split("/review/")[-1], dist, var, len(rs), med("full_px"), med("imp_px"), med("model_height_px"),
            share("sil_mismatch_px"), share("diff32_px"), med("interior_med"), med("interior_p90"),
            med("max_diff"), sum(int(r["dark_edge_px"]) for r in rs)))
