#!/usr/bin/env python3
"""B1170 (#489): the re-gate table for the runs whose frames differ before/after.

usage: python3 -P build_regate_table.py <regate_dir> <identity.tsv> <side_before> <side_after> <out_prefix>
- b1118 rows (tour + 40 records): B1139's harness_measure.py summary.tsv, the
  spell's row: land_cov, end, src_cov, src_on25, src_cen, before / after / delta.
- b1130 rows: B1155's band_table.py over <regate_dir>/measure/<side>/b1130_<kind>_r<rec>
  (a symlink farm per side), joined per record: source_cov, landing_cov,
  landing_end -- the B1130 oracle value, before, after and both verdicts.
Writes <out_prefix>_b1118.tsv and <out_prefix>_b1130.tsv and prints both.
Every row's owner: the removed B1152 record-driven missile (the oracle drew no
Middle on these casts, refactor/B1168/README.md).
"""
import os
import subprocess
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
BAND = os.path.join(REPO, 'refactor', 'B1155', 'scripts', 'band_table.py')
OWNER = 'B1152 record-driven Middle removed (B1170); the oracle drew no Middle here (B1168)'


def summary(path, spell):
    with open(path) as f:
        hdr = f.readline().rstrip('\n').split('\t')
        for line in f:
            c = line.rstrip('\n').split('\t')
            if c[0] == str(spell):
                return dict(zip(hdr, c))
    return {}


def num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def delta(a, b):
    a, b = num(a), num(b)
    return '' if a is None or b is None else '%+.3f' % (b - a)


def banded(ev_measure, side, recs):
    farm = tempfile.mkdtemp(prefix='b1170_farm_')
    os.makedirs(os.path.join(farm, 'measure'))
    for tag in recs:
        src = os.path.join(ev_measure, side, tag)
        os.symlink(src, os.path.join(farm, 'measure', tag[len('b1130_'):]))
    out = os.path.join(farm, 'banded.tsv')
    subprocess.run([sys.executable, BAND, farm, out], check=True, stdout=subprocess.DEVNULL)
    rows = {}
    with open(out) as f:
        hdr = f.readline().rstrip('\n').split('\t')
        for line in f:
            c = line.rstrip('\n').split('\t')
            if c and c[0].strip().isdigit():
                d = {'kind': c[1]}
                d.update(dict(zip(hdr, c[2:])))
                rows[int(c[0])] = d
    return hdr, rows


def main():
    rd, ident, sb, sa, prefix = sys.argv[1:6]
    tags = []
    for line in open(ident):
        c = line.rstrip('\n').split('\t')
        if c[0] != 'run' and c[3] != '0':
            tags.append(c[0])
    meas = os.path.join(rd, 'measure')
    # b1118
    cols = ['land_cov', 'end', 'src_cov', 'src_on25', 'src_cen']
    out = ['row\tspell\t' + '\t'.join('%s_b\t%s_a\td_%s' % (k, k, k) for k in cols) + '\towner']
    for tag in sorted(t for t in tags if t.startswith('b1118_')):
        spell = tag.rsplit('_', 1)[1]
        b = summary(os.path.join(meas, sb, tag, 'summary.tsv'), spell)
        a = summary(os.path.join(meas, sa, tag, 'summary.tsv'), spell)
        row = tag[len('b1118_'):].replace('_' + spell, '') if not tag.startswith('b1118_tour') else 'tour ' + spell
        out.append('%s\t%s\t%s\t%s' % (row, spell, '\t'.join('%s\t%s\t%s' % (b.get(k, ''), a.get(k, ''), delta(b.get(k), a.get(k))) for k in cols), OWNER))
    open(prefix + '_b1118.tsv', 'w').write('\n'.join(out) + '\n')
    print('\n'.join(out))
    # b1130
    recs = sorted(t for t in tags if t.startswith('b1130_'))
    if recs:
        hb, bb = banded(meas, sb, recs)
        _, ba = banded(meas, sa, recs)
        mets = ['source_cov', 'landing_cov', 'landing_end_s']
        out = ['record\tkind\t' + '\t'.join('%s_oracle\t%s_b\t%s_a\t%s_vb\t%s_va' % (m, m, m, m, m) for m in mets) + '\towner']
        wanted = {int(t.rsplit('_r', 1)[1]) for t in recs}
        for rec in sorted(r for r in bb if r in wanted):
            b, a = bb[rec], ba.get(rec, {})
            cells = []
            for m in mets:
                cells += [b.get('oracle_' + m, ''), b.get('harness_' + m, ''), a.get('harness_' + m, ''),
                          b.get('verdict_' + m, ''), a.get('verdict_' + m, '')]
            out.append('%d\t%s\t%s\t%s' % (rec, b['kind'], '\t'.join(cells), OWNER))
        open(prefix + '_b1130.tsv', 'w').write('\n'.join(out) + '\n')
        print('\n'.join(out))
        print('band_table header:', '\t'.join(hb))
    return 0


if __name__ == '__main__':
    sys.exit(main())
