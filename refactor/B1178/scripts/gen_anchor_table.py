#!/usr/bin/env python3
"""
regenerate the B1178 README's "representative anchor rows" table FROM the
committed remeasure TSVs -- never retype it (card finding 3).

The table's cells are read from two byte-committed evidence files:
  * b1130_anchor_remeasure.tsv  (capture, step, old_tmpx, old_tfeet,
                                 new_tmpx, new_tfeet, threshold_sweep)
      the *_tfeet columns are "col,feet".
  * b1130_anatomy_old_new.tsv   (capture, record, spell, old/new_target_centroid,
                                 old/new_p90_land, old/new demand columns)

The representative captures are the six the README calls out in prose
(r51, e13, e3, e15, e16, e24). This script prints the exact markdown
table body (header + separator + rows) so the README can paste it --
re-run after any TSV change and the table cannot drift from its source.

Usage:
  python3 refactor/B1178/scripts/gen_anchor_table.py \
      <b1130_anchor_remeasure.tsv> <b1130_anatomy_old_new.tsv>
"""

import sys

REPRESENTATIVE = ['r51', 'e13', 'e3', 'e15', 'e16', 'e24']
ARROW = '\u2192'  # the README's table uses the real arrow, not "->"


def read_tsv(path):
    with open(path, encoding='utf-8') as fh:
        lines = [ln.rstrip('\n') for ln in fh]
    header = lines[0].split('\t')
    rows = [ln.split('\t') for ln in lines[1:] if ln.strip()]
    return header, {r[0]: dict(zip(header, r)) for r in rows}


def col_feet(field):
    """the *_tfeet columns are 'col,feet' -> (col, feet)."""
    col, feet = field.split(',')
    return col, feet


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    _, anchor = read_tsv(sys.argv[1])
    _, anat = read_tsv(sys.argv[2])

    out = []
    out.append('| capture | spell | old tmpx/feet/col | new tmpx/feet/col | target_centroid old -> new |')
    out.append('|---|---|---|---|---|')
    for cap in REPRESENTATIVE:
        a = anchor[cap]
        n = anat[cap]
        old_col, old_feet = col_feet(a['old_tfeet'])
        new_col, new_feet = col_feet(a['new_tfeet'])
        old_cen = n['old_target_centroid']
        new_cen = n['new_target_centroid']
        out.append(f"| {cap:4s} | {n['spell']:>3s} | "
                   f"{a['old_tmpx']} / {old_feet} / {old_col} | "
                   f"**{a['new_tmpx']} / {new_feet} / {new_col}** | "
                   f"{old_cen} {ARROW} {new_cen} |")
    print('\n'.join(out))


if __name__ == '__main__':
    main()
