#!/usr/bin/env python3
"""B952: the marginal memory cost of ONE impostor texture, per platform/size.

usage: marginal.py <summary.csv>   (the --csv output of summarize.py)

A creation batch's deltas carry one-time costs (the first FBO use compiles the
shader variants, the scratch target is allocated). Those are the same whether
the batch creates 1 texture or 200, so the cost of one more texture is
    (delta(N=200 run) - delta(N=1 run)) / (textures(200) - textures(1))
taken between runs that differ ONLY in N (same zone, size, mode, scene, fbo).
Rows print per (zone, size, mode, scene, fbo) pair that has both ends.
"""
import csv
import re
import sys

FIELDS = ["gpu_driver_delta", "rss_anon_delta", "sys_shmem_delta_kb", "textures_driver_delta",
          "gpu_texture_bytes_delta"]


def key(label):
    m = re.match(r"(\w+?)_n(\d+)_s(\d+)_(unique|shared)_(far|mixed)(.*)", label)
    if not m:
        return None, None
    zone, n, size, mode, scene, extra = m.groups()
    return (zone, int(size), mode, scene, extra), int(n)


def main(path):
    rows = list(csv.DictReader(open(path)))
    by = {}
    for r in rows:
        k, n = key(r["label"])
        if k is None or not r.get("textures"):
            continue
        by.setdefault(k, {})[n] = r
    print("| zone | size | mode | scene | extra | textures 1 -> 200 | logical B/tex | "
          "gpu_driver B/tex | RssAnon B/tex | sys Shmem B/tex | textures_driver B/tex |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    for k in sorted(by):
        ends = by[k]
        lo = ends.get(1)
        hi = ends.get(200)
        if not lo or not hi:
            continue
        dt = int(hi["textures"]) - int(lo["textures"])
        if dt <= 0:
            continue
        per = {}
        for f in FIELDS:
            a, b = float(lo[f]), float(hi[f])
            scale = 1024 if f == "sys_shmem_delta_kb" else 1
            per[f] = (b - a) * scale / dt
        size = k[1]
        logical = size * size * 4 + (size * size * 2 if "fboper" in k[4] else 0)
        print(f"| {k[0]} | {size} | {k[2]} | {k[3]} | {k[4] or '-'} | {lo['textures']} -> "
              f"{hi['textures']} | {logical} | {per['gpu_driver_delta']:.0f} | "
              f"{per['rss_anon_delta']:.0f} | {per['sys_shmem_delta_kb']:.0f} | "
              f"{per['textures_driver_delta']:.0f} |")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    main(sys.argv[1])
