#!/usr/bin/env python3
"""One-command set comparison: SUMMARY.md per-expansion tables vs sweep_results.tsv."""
import sys, re, os
BASE = os.path.dirname(os.path.abspath(__file__))
tsv = open(f"{BASE}/logs/sweep_results.tsv").read().splitlines()
tsv_zones = {}
for ln in tsv[1:]:
    if ln.strip():
        z, e = ln.split("\t")[0], ln.split("\t")[1]
        tsv_zones.setdefault(e, set()).add(z)
text = open(f"{BASE}/SUMMARY.md").read()
def grab(marker, stop):
    i = text.index(marker)
    j = text.index(stop, i)
    # Plain '|' is a literal in Python re (escaping it is harmless). The
    # separator row ('||------|...') is filtered out below, as is the header.
    rows = [re.match(r"^\|\|\s*(\S+)", l) for l in text[i:j].splitlines()]
    return [m.group(1) for m in rows
            if m and m.group(1) != "Zone" and not m.group(1).startswith("---")]
k = set(grab("### Kunark (28 zones)", "### Velious (18 zones)"))
v = set(grab("### Velious (18 zones)", "## Notes on failures"))
print("TSV kunark:", len(tsv_zones['kunark']), " velious:", len(tsv_zones['velious']))
print("SUMMARY kunark rows:", len(k), " velious rows:", len(v))
print("kunark diff (SUMMARY-TSV / TSV-SUMMARY):", sorted(k - tsv_zones['kunark']), sorted(tsv_zones['kunark'] - k))
print("velious diff (SUMMARY-TSV / TSV-SUMMARY):", sorted(v - tsv_zones['velious']), sorted(tsv_zones['velious'] - v))
print("overlap between tables:", sorted(k & v))
ok = (k == tsv_zones['kunark'] and v == tsv_zones['velious'] and not (k & v)
      and len(k) + len(v) == 46)
print("RESULT:", "MATCH — 28+18=46, every zone exactly once under its true expansion" if ok else "MISMATCH")
sys.exit(0 if ok else 1)
