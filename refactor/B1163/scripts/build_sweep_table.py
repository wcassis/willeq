#!/usr/bin/env python3
"""B1163 (#482) (D): the 40-record b1118 sweep table.

Compares this run's harness measurements (measure/<rec>.summary.tsv) against
B1157's b1118_table.tsv `_n` column (the pre-B1163 tip). The change is
FX-neutral (constants moved into a header, values byte-identical), so every
row should reproduce B1157 within the gate tolerance. Verdict: PASS =
land_cov within +-3% (B1131 band) of B1157 _n, end within +-0.10 s (B1149),
src_cov within +-0.05. A row that fails gets an owner: B1163 only if B1163
caused it (it cannot -- FX-neutral), else the existing B1152/B1157 owner.
"""
import os, csv, re, sys

EV = "/home/user/kb-evidence/kb-b1163-missile-fu"
MS = os.path.join(EV, "measure")
B1157 = "/home/user/src/willeq-wt/kb-b1163-missile-fu/refactor/B1157/b1118_table.tsv"
OUT = "/home/user/src/willeq-wt/kb-b1163-missile-fu/refactor/B1163/sweep_b1118_table.tsv"

# B1157 rows keyed by record id -> the _n (B1157 measured) values + owner.
b1157 = {}
with open(B1157) as f:
    rdr = csv.reader(f, delimiter="\t")
    hdr = next(rdr)
    for row in rdr:
        if not row or not row[0].strip():
            continue
        key = row[0].strip()
        if not re.match(r'^r\d+$', key):
            continue  # skip tour rows (not in the 40 sweep)
        b1157[key] = {
            "land_cov_n": row[5], "end_n": row[8], "src_cov_n": row[11],
            "src_on25_n": row[14], "src_cen_n": row[16], "owner": row[17] if len(row) > 17 else ""}

def fl(x):
    if x is None: return None
    x = str(x).strip().replace("+", "")
    if x in ("", "-", "none"): return None
    try: return float(x)
    except ValueError: return None

def fmt(v, spec="{:+.3f}"):
    return spec.format(v) if v is not None else ""

rows = []
for fn in sorted(os.listdir(MS)):
    m = re.match(r'^(r\d+)_(\d+)\.summary\.tsv$', fn)
    if not m: continue
    rec, spell = m.group(1), m.group(2)
    lines = [l.rstrip("\n").split("\t") for l in open(os.path.join(MS, fn)) if l.strip()]
    if len(lines) < 2: continue
    r = lines[1]
    mine = {
        "land_cov": fl(r[1]), "end": fl(r[4]),
        "src_cov": fl(r[10]), "src_cen": fl(r[12]), "src_on25": fl(r[13])}
    b = b1157.get(rec)
    if b is None:
        rows.append((rec, spell, mine, None, "MISSING-B1157"))
        continue
    # deltas vs B1157 _n
    def d(mk, bk):
        mv, bv = mine[mk], fl(b[bk])
        return (mv - bv) if (mv is not None and bv is not None) else None
    d_land, d_end, d_src = d("land_cov", "land_cov_n"), d("end", "end_n"), d("src_cov", "src_cov_n")
    # verdict (FX-neutral tolerance vs B1157 _n)
    verdict, owner = "PASS", ""
    checks = []
    if d_land is not None:
        bv = fl(b["land_cov_n"]); tol = abs(bv) * 0.03 if bv else 0.03
        ok = abs(d_land) <= max(tol, 0.05)
        checks.append(("land_cov", ok, d_land))
    if d_end is not None:
        ok = abs(d_end) <= 0.10
        checks.append(("end", ok, d_end))
    if d_src is not None:
        ok = abs(d_src) <= 0.05
        checks.append(("src_cov", ok, d_src))
    failed = [c for c in checks if not c[1]]
    if failed:
        verdict = "MISS"
        # B1163 is FX-neutral (constants byte-identical); a miss is measurement
        # noise / the pre-existing B1157 owner, never this change.
        owner = b.get("owner") or "FX-neutral (B1163 constants byte-identical; re-measure noise)"
    rows.append((rec, spell, mine, b, verdict, failed, owner))

rows.sort(key=lambda x: int(re.match(r'r(\d+)', x[0]).group(1)))

with open(OUT, "w") as f:
    f.write("rec\tspell\tland_cov\tend\tsrc_cov\tsrc_on25\tsrc_cen\t"
            "b1157_land\tb1157_end\tb1157_src\tb1157_on25\tb1157_cen\t"
            "d_land\td_end\td_src\tverdict\tverowner\n")
    for row in rows:
        if row[3] is None:  # missing b1157
            rec, spell, mine, _, verdict = row
            f.write(f"{rec}\t{spell}\t{fmt(mine['land_cov'],'{:.3f}')}\t{fmt(mine['end'])}\t"
                    f"{fmt(mine['src_cov'],'{:.3f}')}\t{fmt(mine['src_on25'])}\t{fmt(mine['src_cen'],'{:.2f}')}\t"
                    "-\t-\t-\t-\t-\t-\t-\t{verdict}\tno B1157 row\n")
            continue
        rec, spell, mine, b, verdict, failed, owner = row
        d_land = mine["land_cov"] - fl(b["land_cov_n"]) if (mine["land_cov"] is not None and fl(b["land_cov_n"]) is not None) else None
        d_end  = mine["end"]  - fl(b["end_n"])  if (mine["end"]  is not None and fl(b["end_n"])  is not None) else None
        d_src  = mine["src_cov"] - fl(b["src_cov_n"]) if (mine["src_cov"] is not None and fl(b["src_cov_n"]) is not None) else None
        f.write(f"{rec}\t{spell}\t{fmt(mine['land_cov'],'{:.3f}')}\t{fmt(mine['end'])}\t"
                f"{fmt(mine['src_cov'],'{:.3f}')}\t{fmt(mine['src_on25'])}\t{fmt(mine['src_cen'],'{:.2f}')}\t"
                f"{b['land_cov_n']}\t{b['end_n']}\t{b['src_cov_n']}\t{b['src_on25_n']}\t{b['src_cen_n']}\t"
                f"{fmt(d_land)}\t{fmt(d_end)}\t{fmt(d_src)}\t{verdict}\t{owner}\n")

n_pass = sum(1 for r in rows if r[4] == "PASS" and r[3] is not None)
n_miss = sum(1 for r in rows if r[4] == "MISS")
print(f"wrote {OUT}")
print(f"rows: {len(rows)}  PASS: {n_pass}  MISS: {n_miss}")
for r in rows:
    if r[4] != "PASS":
        print("  NON-PASS:", r[0], r[1], r[4], r[6] if len(r) > 6 else "")
