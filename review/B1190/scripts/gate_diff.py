#!/usr/bin/env python3
"""B1190: the 47-row gate byte-identity check.

Compares the measured columns of the 47 gate rows (40 b1130 `spellsnew`
records from review/B1130/record_table.tsv, record 51 excluded, + the 7 tour
spells) between two render_gate.sh evidence dirs (a base tree and a tip tree).
Each row's measured tsv (summary.tsv for tour/b1118 rows, b1130_rows.tsv for
b1130 rows) must be byte-identical.

usage: gate_diff.py <base-ev-dir> <tip-ev-dir> [record-table.tsv]
(record table default: the B1130 review copy on this host,
 /mnt/projects/fable/review/B1130/record_table.tsv -- not in the repo)
exit 0 iff all 47 rows present on both sides and byte-identical.
"""
import sys, pathlib

def usage():
    sys.exit("usage: gate_diff.py <base-ev-dir> <tip-ev-dir> [record-table.tsv]")

if len(sys.argv) < 3:
    usage()
base_ev, tip_ev = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
table = pathlib.Path(sys.argv[3] if len(sys.argv) > 3 else
                     "/mnt/projects/fable/review/B1130/record_table.tsv")

def row_keys(ev: pathlib.Path) -> dict:
    out = {}
    m = ev / "measure"
    if not m.exists():
        return out
    for d in sorted(m.iterdir()):
        if not d.is_dir():
            continue
        files = []
        for name in ("summary.tsv", "b1130_rows.tsv"):
            p = d / name
            if p.exists() and p.stat().st_size:
                files.append(p)
        if files:
            out[d.name] = files
    return out

base_rows = row_keys(base_ev)
tip_rows = row_keys(tip_ev)

tour = [f"tour_{s}" for s in ("93", "38", "54", "656", "41", "253", "458")]
b1130 = []
for line in table.read_text().splitlines()[1:]:
    parts = line.split("\t")
    if parts and parts[0] not in ("", "51"):
        b1130.append(f"b1130_r{parts[0]}")
rows = tour + b1130
print(f"expect {len(rows)} rows (tour {len(tour)} + b1130 {len(b1130)})")

missing_b = [t for t in rows if t not in base_rows]
missing_t = [t for t in rows if t not in tip_rows]
if missing_b:
    print(f"MISSING ON BASE: {missing_b}")
if missing_t:
    print(f"MISSING ON TIP: {missing_t}")

bad = ok = 0
for t in rows:
    if t not in base_rows or t not in tip_rows:
        bad += 1
        continue
    bf, tf = base_rows[t], tip_rows[t]
    if len(bf) != len(tf):
        print(f"{t}: FILE SET MISMATCH base={[f.name for f in bf]} tip={[f.name for f in tf]}")
        bad += 1
        continue
    row_ok = True
    for i, f in enumerate(bf):
        g = tf[i]
        hb, ht = f.read_bytes(), g.read_bytes()
        if hb != ht:
            row_ok = False
            lb, lt = hb.decode(errors="replace").splitlines(), ht.decode(errors="replace").splitlines()
            hdr_b = lb[0].split("\t") if lb else []
            for j, (x, y) in enumerate(zip(lb, lt)):
                if x != y:
                    if j == 0:
                        cols = {h: (a, b_) for h, a, b_ in zip(hdr_b, x.split("\t"), y.split("\t")) if a != b_}
                        print(f"{t} {f.name}: header col diffs {cols}")
                    else:
                        print(f"{t} {f.name} line {j}:\n  base={x[:160]}\n  tip ={y[:160]}")
            if len(lb) != len(lt):
                print(f"{t} {f.name}: line count base={len(lb)} tip={len(lt)}")
    if row_ok:
        ok += 1
    else:
        bad += 1

print(f"== {ok}/{len(rows)} rows byte-identical, {bad} not ==")
sys.exit(1 if bad else 0)
