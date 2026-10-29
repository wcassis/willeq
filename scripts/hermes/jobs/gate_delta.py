#!/usr/bin/env python3
"""B1203 (#522): gate_delta.py <base tables dir> <after tables dir> <out dir>

The per-row AFTER-vs-BASE delta of two judged gates (each a dir written by
refactor/B1188/scripts/build_tables_b1188.py: b1130.tsv, b1118.tsv, tour.tsv,
counts.tsv). Generalises refactor/B1188/scripts/delta_b1188_base.py from two fixed
paths to any two judged dirs. python3 stdlib only.

Writes <out>/delta.tsv (table, key, column, base, after, delta, for every `mine_*`
column where both sides are numbers) and <out>/flips.tsv (table, key, column,
v_base, v_after for every `v_*` verdict that changed), and prints one line per table x
column: PASS count base -> after.
"""
import os
import sys

KEYS = {"b1130.tsv": "record", "b1118.tsv": "row", "tour.tsv": "spell"}


def readtsv(p):
    if not os.path.exists(p):
        return []
    with open(p) as fh:
        lines = [l.rstrip("\n") for l in fh if l.strip() and not l.lstrip().startswith("#")]
    if not lines:
        return []
    hdr = lines[0].split("\t")
    return [dict(zip(hdr, l.split("\t"))) for l in lines[1:]]


def num(x):
    try:
        return float((x or "").strip())
    except ValueError:
        return None


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    base_dir, after_dir, out = argv[1:4]
    os.makedirs(out, exist_ok=True)
    delta = ["table\tkey\tcolumn\tbase\tafter\tdelta"]
    flips = ["table\tkey\tcolumn\tv_base\tv_after"]
    summary = []
    for name, key in KEYS.items():
        b = {r.get(key): r for r in readtsv(os.path.join(base_dir, name))}
        a = {r.get(key): r for r in readtsv(os.path.join(after_dir, name))}
        common = [k for k in a if k in b]
        cols = sorted({c for r in a.values() for c in r})
        for c in cols:
            if c.startswith("mine_"):
                for k in common:
                    x, y = num(b[k].get(c)), num(a[k].get(c))
                    if x is not None and y is not None:
                        delta.append("%s\t%s\t%s\t%g\t%g\t%g" % (name[:-4], k, c[5:], x, y, y - x))
            if c.startswith("v_"):
                nb = sum(b[k].get(c) == "PASS" for k in common)
                na = sum(a[k].get(c) == "PASS" for k in common)
                summary.append("%-6s %-18s PASS %3d -> %3d  (rows %d)" % (name[:-4], c[2:], nb, na, len(common)))
                for k in common:
                    if b[k].get(c) != a[k].get(c):
                        flips.append("%s\t%s\t%s\t%s\t%s" % (name[:-4], k, c[2:], b[k].get(c), a[k].get(c)))
    open(os.path.join(out, "delta.tsv"), "w").write("\n".join(delta) + "\n")
    open(os.path.join(out, "flips.tsv"), "w").write("\n".join(flips) + "\n")
    open(os.path.join(out, "pass_counts.txt"), "w").write("\n".join(summary) + "\n")
    print("\n".join(summary))
    print("GATE_DELTA rows=%d flips=%d -> %s" % (len(delta) - 1, len(flips) - 1, out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
