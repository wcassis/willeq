#!/usr/bin/env python3
"""Full cell-by-cell comparison of SUMMARY.md tables vs sweep_results.tsv.

Stronger than verify_tables_vs_tsv.py: checks s3d_bytes, wall and peak-RSS
cells for every row, not just the zone-name sets.
"""
import re, sys, os

BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "refactor", "B854")
tsv = open(f"{BASE}/logs/sweep_results.tsv").read().splitlines()
rows = {}
for ln in tsv[1:]:
    if ln.strip():
        c = ln.split("\t")
        rows[c[0]] = {"expansion": c[1], "s3d": int(c[2]), "wall": c[5],
                      "peak": int(c[18]) or 0}

text = open(f"{BASE}/SUMMARY.md").read()

# Split each table row on '|' into cells: ['||', ' zone ', ' s3d ', ...].
# (Splitting on the literal pipe character sidesteps any regex subtlety.)
def grab(marker, stop):
    i = text.index(marker)
    j = text.index(stop, i)
    out = {}
    for l in text[i:j].splitlines():
        if not l.startswith("||"):
            continue
        cells = [c.strip() for c in l.split("|")]
        # cells: ['', '', zone, s3d, status, wall, peak, '']
        if len(cells) < 8 or cells[2] in ("Zone", "------", ""):
            continue
        out[cells[2]] = cells
    return out

def mb(b):
    return f"{b / (1024 * 1024):.1f} MB" if b else "-"

errors = []
checked = 0
for exp, marker, stop in (("kunark", "### Kunark (28 zones)", "### Velious (18 zones)"),
                          ("velious", "### Velious (18 zones)", "## Notes on failures")):
    tbl = grab(marker, stop)
    exp_zones = {z for z, r in rows.items() if r["expansion"] == exp}
    if set(tbl) != exp_zones:
        errors.append(f"{exp}: zone set diff SUMMARY-TSV={sorted(set(tbl) - exp_zones)} TSV-SUMMARY={sorted(exp_zones - set(tbl))}")
    for z, cells in tbl.items():
        r = rows[z]
        if cells[3] != str(r["s3d"]):
            errors.append(f"{z}: s3d {cells[3]} != TSV {r['s3d']}")
        wall = f"{r['wall']}s" if r["wall"] not in ("", "-") else "-"
        if cells[5] != wall:
            errors.append(f"{z}: wall {cells[5]} != TSV {wall}")
        if cells[6] != mb(r["peak"]):
            errors.append(f"{z}: peak {cells[6]} != TSV {mb(r['peak'])}")
        checked += 1

print(f"cells checked: {checked} rows x (s3d, wall, peak)")
if checked != 46:
    errors.append(f"expected 46 checked rows, got {checked}")
if errors:
    print("ERRORS:")
    for e in errors:
        print("  -", e)
    sys.exit(1)
print("RESULT: MATCH - all 46 rows, every s3d/wall/peak cell agrees with the TSV")
sys.exit(0)
