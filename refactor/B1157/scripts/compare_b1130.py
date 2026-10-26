#!/usr/bin/env python3
"""B1157: the b1130 40-record re-gate, before/after against B1155's banded table.

usage: compare_b1130.py <this_ev_dir> <b1155_banded.tsv> <out.tsv>

This sweep's runs are banded by B1155's own instrument (band_table.py, same
B1130 oracle table, same bands) and joined to B1155's committed
banded_table.tsv (the pre-B1157 idle caster) record by record: for each of
the three gate metrics the before value, the after value, the delta
(after - before) and both verdicts; a post-B1157 miss carries its owner:
"cast-anim (B1157), residual: <axes>" when the miss moved toward the oracle,
"model/animation (cast pose owns the Source window)" for a Source axis,
"unattributed" otherwise. The record's sim word is out of this card's scope
(Will's rule: the sim, the reader and spell_visual_fx are not this card's),
so no row is blamed on a sim field here.
"""
import sys, os, subprocess, math

ev, b1155, out = sys.argv[1], sys.argv[2], sys.argv[3]
repo = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
band = os.path.join(repo, 'B1155', 'scripts', 'band_table.py')
after = out + '.after.tsv'
# band_table.py reads <EV>/measure/<kind>_r<rec>/; this sweep's dirs carry a
# b1157_ prefix, so hand it an EV dir whose measure/ is a symlink farm. The
# farm is absolute-path symlinks into <ev> -- ephemeral, never committed.
import tempfile
farm = tempfile.mkdtemp(prefix='b1157_farm_')
os.makedirs(os.path.join(farm, 'measure'))
for d in sorted(os.listdir(os.path.join(ev, 'measure'))):
    if d.startswith('b1157_') and os.path.exists(os.path.join(ev, 'measure', d, 'b1130_rows.tsv')):
        os.symlink(os.path.join(ev, 'measure', d), os.path.join(farm, 'measure', d[6:]))
subprocess.run([sys.executable, band, farm, after], check=True)

def f(x):
    try:
        v = float(x)
        return None if math.isnan(v) else v
    except (ValueError, TypeError):
        return None

def read_banded(path):
    # band_table.py's format: the header covers the metric columns only;
    # each row starts with record, kind.
    with open(path) as fh:
        hdr = fh.readline().rstrip('\n').split('\t')
        rows = {}
        for line in fh:
            c = line.rstrip('\n').split('\t')
            if not c or not c[0].strip().isdigit():
                continue
            d = {'kind': c[1]}
            d.update(dict(zip(hdr, c[2:])))
            rows[int(c[0])] = d
    return rows

before = read_banded(b1155)
aftert = read_banded(after)

metrics = ['source_cov', 'landing_cov', 'landing_end_s']
outcols = ['record', 'kind']
for m in metrics:
    outcols += [f'b1155_{m}', f'b1157_{m}', f'delta_{m}', f'v55_{m}', f'v57_{m}']
outcols.append('owner')

def owner(rec, row):
    notes, moved = [], []
    for m in metrics:
        if row[f'v57_{m}'] in ('short', 'over'):
            notes.append(m)
        o = f(before[rec].get(f'oracle_{m}'))
        hb = f(before[rec].get(f'harness_{m}'))
        ha = f(row.get(f'b1157_{m}'))
        if o is not None and hb is not None and ha is not None:
            moved.append(abs(o - hb) - abs(o - ha))
    if not notes:
        return 'PASS'
    if moved and all(d >= 0 for d in moved):
        return 'cast-anim (B1157), residual: ' + '/'.join(notes)
    if 'source_cov' in notes:
        return 'model/animation (cast pose owns the Source window)'
    return 'unattributed'

n = 0
with open(out, 'w') as fh:
    fh.write('\t'.join(outcols) + '\n')
    for rec in sorted(set(before) & set(aftert)):
        b, a = before[rec], aftert[rec]
        row = [str(rec), b['kind']]
        for m in metrics:
            bv, av = b.get(f'harness_{m}', ''), a.get(f'harness_{m}', '')
            fb, fa = f(bv), f(av)
            d = f'{fa - fb:+.3f}' if fb is not None and fa is not None else ''
            row += [bv, av, d, b.get(f'verdict_{m}', ''), a.get(f'verdict_{m}', '')]
        row.append(owner(rec, {f'b1157_{m}': a.get(f'harness_{m}', '') for m in metrics} |
                             {f'v57_{m}': a.get(f'verdict_{m}', '') for m in metrics}))
        fh.write('\t'.join(row) + '\n')
        n += 1
print(f'wrote {out} ({n} records)')
