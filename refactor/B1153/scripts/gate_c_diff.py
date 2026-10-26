#!/usr/bin/env python3
"""B1153 gate (c) delta: the verdict cells that changed between
refactor/B1155/banded_table.tsv (the B1155 sweep, cut pre-B1152) and
refactor/B1153/tables/gate_c_banded.tsv (this sweep's clean copy).

Run from any checkout:

    python3 refactor/B1153/scripts/gate_c_diff.py

Prints the per-metric verdict-change counts (the 124-of-360 figure in
README sec. 3(c)) and the per-row list. Comparison is by column NAME
(`verdict_*`), so the two tables need not be index-aligned.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))


def load(path):
    rows, header = {}, []
    with open(path) as f:
        for line in f:
            if line.startswith("#"):
                continue
            c = line.rstrip("\n").split("\t")
            if c and c[0] == "oracle_source_cov":
                header = [h for h in c if h]
            elif c and c[0].strip():
                rows[c[0]] = c
    return rows, header


b_rows, b_header = load(os.path.join(REPO, "refactor/B1155/banded_table.tsv"))
g_rows, g_header = load(os.path.join(REPO, "refactor/B1153/tables/gate_c_banded.tsv"))

# data rows carry two leading cells (record id, kind) the header does not name
b_idx = {h: i + 2 for i, h in enumerate(b_header)}
g_idx = {h: i + 2 for i, h in enumerate(g_header)}
verdicts = [h for h in g_header if h.startswith("verdict_")]

counts = {h[len("verdict_"):]: 0 for h in verdicts}
pairs = []
for rec in sorted(g_rows, key=lambda x: int(re.sub(r"\D", "", x) or 0)):
    gr, br = g_rows[rec], b_rows.get(rec)
    if br is None:
        print(f"missing in b1155: {rec}")
        continue
    for h in verdicts:
        gv, bv = gr[g_idx[h]], br[b_idx[h]]
        if gv != bv:
            counts[h[len("verdict_"):]] += 1
            pairs.append(f"r{rec} {h[len('verdict_')]}: {bv} -> {gv}")

n = len(g_rows)
print(f"verdict changes per column (b1155 -> b1153), {n} records:")
for name in counts:
    print(f"  {name}: {counts[name]}/{n}")
print("total:", sum(counts.values()), "of", len(counts) * n, "verdict cells")
print()
print("\n".join(pairs))
