#!/usr/bin/env python3
"""B1152 sweep vs B1151 sweep: per-row src_cov / land_cov / src_cen + deltas."""
import os, re, sys

WT = "/home/user/src/willeq-wt/kb-b1152-missile"

def run_metric(prefix, rec, sid, col):
    f = os.path.join(WT, "refactor", prefix, "runs", f"{prefix.lower()}_r{rec}_{sid}", "measure", "summary.tsv")
    if not os.path.exists(f):
        return None
    with open(f) as fh:
        lines = fh.read().splitlines()
    hdr = lines[0].split("\t")
    for line in lines[1:]:
        parts = line.split("\t")
        if len(parts) > col and parts[0] == str(sid):
            return parts[col]
    return None

def src_land_cen(prefix, rec, sid):
    # header: spell(0) land_cov(1) land_t(2) fxt_cov(3) end(4) abs_on(5) abs_end(6) rowtop(7) roi_l(8) roi_s(9) src_cov(10) src_ext(11) src_cen(12) ...
    return run_metric(prefix, rec, sid, 10), run_metric(prefix, rec, sid, 1), run_metric(prefix, rec, sid, 12), run_metric(prefix, rec, sid, 11)  # src_cov, land_cov, src_cen, src_ext

# list of (rec, sid) from B1152 runs
runs = []
d = os.path.join(WT, "refactor", "B1152", "runs")
for name in sorted(os.listdir(d)):
    m = re.match(r"^b1152_r(\d+)_(\d+)$", name)
    if m:
        runs.append((int(m.group(1)), int(m.group(2))))
runs.sort()

rows = []
for rec, sid in runs:
    s2, l2, c2, e2 = src_land_cen("B1152", rec, sid)
    s1, l1, c1, e1 = src_land_cen("B1151", rec, sid)
    rows.append((rec, sid, s1, s2, l1, l2, c1, c2, e1, e2))

def f(x):
    try: return float(x)
    except: return None

def d(v1, v2):
    a, b = f(v1), f(v2)
    return (b-a) if (a is not None and b is not None) else None

print("| row | spell | B1151 src_cov | B1152 src_cov | d | B1151 land_cov | B1152 land_cov | d | B1151 src_cen | B1152 src_cen |")
print("|---|---|---|---|---|---|---|---|---|---|")
ds_d = [x for x in (d(s1, s2) for _,_,s1,s2,_,_,_,_,_,_ in rows) if x is not None]
dl_d = [x for x in (d(l1, l2) for _,_,_,_,l1,l2,_,_,_,_ in rows) if x is not None]
for rec, sid, s1, s2, l1, l2, c1, c2, e1, e2 in rows:
    print(f"| {rec} | {sid} | {s1} | {s2} | {d(s1,s2):+.3f} | {l1} | {l2} | {d(l1,l2):+.3f} | {c1} | {c2} |".replace("nan", ""))
print(f"\nrows={len(rows)}; src_cov |d|>0.05: {sum(1 for x in ds_d if abs(x)>0.05)}/{len(ds_d)} (max {max(abs(x) for x in ds_d):.3f}); land_cov |d|>0.05: {sum(1 for x in dl_d if abs(x)>0.05)}/{len(dl_d)} (max {max(abs(x) for x in dl_d):.3f})")
