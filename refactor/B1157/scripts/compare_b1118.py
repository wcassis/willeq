#!/usr/bin/env python3
"""B1157: the b1118 re-gate table, B1152 (idle caster) vs B1157 (animated
caster). One row per tour spell and per record: the B1152 baseline value, this
run's value, the delta, and a verdict for the gate axes (landing coverage
+/-3 % and landing end +/-0.10 s, B1131's band / B1149's method, applied
harness-against-harness since the b1118 rig is a proxy, not the lane).
Source-axis deltas (src_cov, src_on25, src_hue) are where the cast animation
is ALLOWED to move the gate: the raised hand changes what the Source window
contains. A miss is owned: the sim field + address, a model/animation
difference, or "unattributed".

usage: compare_b1118.py <ev_b1118_dir> <b1152_runs_dir> <out.tsv>
"""
import sys, os, csv

EV, RUNS, out = sys.argv[1], sys.argv[2], sys.argv[3]

# tour spell -> the B1152 record run that is its baseline (B1151's tour rows;
# a spell's baseline is its own record's representative run).
TOUR = [
    ('93', 'b1152_r0_93'),
    ('38', 'b1152_r0_93'),      # record 0's group (f144): rep 93
    ('54', 'b1152_r23_372'),    # record 23's group: rep 372
    ('656', 'b1152_r23_372'),
    ('41', 'b1152_r20_341'),    # record 20's group: rep 341
    ('253', 'b1152_r35_253'),
    ('458', 'b1152_r33_382'),   # record 33's group: rep 382
]

def load(path):
    d = {}
    if os.path.exists(path):
        for r in csv.DictReader(open(path), delimiter='\t'):
            d[r['spell']] = r
    return d

def f(x):
    try: return float(x)
    except (TypeError, ValueError): return None

rows = []

def add(tag, spell, base_tag, base, new):
    b, n = base.get(spell), new.get(spell)
    if not b or not n or b.get('land_cov') in (None, '', 'missing'):
        rows.append([tag, spell, base_tag, 'MISSING-BASELINE' if not b else 'BASELINE-MISSING', '', '', '', '', '', '', '', ''])
        return
    def pair(col, pct=False):
        bv, nv = f(b[col]), f(n[col])
        if bv is None or nv is None:
            return b[col], n[col], ''
        d = nv - bv
        return f'{bv:g}', f'{nv:g}', f'{d:+.3f}'
    lc_b, lc_n, lc_d = pair('land_cov')
    en_b, en_n, en_d = pair('end')
    sc_b, sc_n, sc_d = pair('src_cov')
    so_b, so_n, so_d = pair('src_on25')
    sh_b, sh_n = b['src_hue'], n['src_hue']
    # verdict on the gate axes (harness-against-harness band):
    blc, nlc, blcd = f(lc_b), f(lc_n), f(lc_d)
    ben, nen, bended = f(en_b), f(en_n), f(en_d)
    v = 'PASS'
    if blc and nlc:
        dev = abs(nlc - blc) / blc
        if dev > 0.03: v = 'short' if nlc < blc else 'over'
    if ben is not None and nen is not None and abs(nen - ben) > 0.10:
        v = 'short' if nen < ben else 'over'
    # owner per miss:
    owner = ''
    if v != 'PASS':
        owner = 'unattributed'  # the writer fills the specific owner from the
        # row's source-axis deltas + the frames
    rows.append([tag, spell, base_tag, v, lc_b, lc_n, lc_d, en_b, en_n, en_d,
                 f'{sc_b}->{sc_n} ({sc_d})', f'{so_b}->{so_n} ({so_d})', f'{sh_b}->{sh_n}', owner])

for sid, btag in TOUR:
    add(f'tour {sid}', sid, btag, load(os.path.join(RUNS, btag, 'measure', 'summary.tsv')),
        load(os.path.join(EV, 'measure', f'b1157_tour_{sid}', 'summary.tsv')))

# the 40 records: B1152's own plan list (record, spell, target, cast_ms)
for r in csv.DictReader(open(os.path.join(RUNS, '..', 'plans', 'sweep', 'list.tsv')), delimiter='\t'):
    rec, sid = r['record'], r['spell_id']
    btag = f'b1152_r{rec}_{sid}'
    add(f'r{rec}', sid, btag, load(os.path.join(RUNS, btag, 'measure', 'summary.tsv')),
        load(os.path.join(EV, 'measure', f'b1157_r{rec}_{sid}', 'summary.tsv')))

with open(out, 'w') as fh:
    w = csv.writer(fh, delimiter='\t')
    w.writerow(['row', 'spell', 'baseline', 'verdict',
                'land_cov_b', 'land_cov_n', 'd_land_cov',
                'end_b', 'end_n', 'd_end',
                'src_cov (b->n, d)', 'src_on25 (b->n, d)', 'src_hue (b->n)', 'owner'])
    w.writerows(rows)
print(f'{len(rows)} rows -> {out}')
