#!/usr/bin/env python3
"""B1164 (#483) dev5: the B1157 gate re-run before/after the attach scale s, per row.

b1118 rows (tour-7 + 40 records): B1157's committed after column
(refactor/B1157/b1118_table.tsv land_cov_n / end_n / src_cov_n), this card's BEFORE (the
branch's tip without the change, 4275bc51's code) and AFTER, from
<gate>/<label>/measure/b1118_<tag>/summary.tsv (B1139's harness_measure.py).
b1130 rows (40 records): B1157's refactor/B1157/b1130_table.tsv b1157_* columns, BEFORE and
AFTER from <gate>/<label>/measure/b1130_<kind>_r<rec>/b1130_rows.tsv (B1155's instrument).
A row MOVED when |after - before| exceeds the gate bands (land_cov 3 % of before, end 0.10 s,
src_cov 3 %). Owner of a moved row: the s of the entity its moved stage lands on (the plan's
target: 'other' = the harness's race-1 size-6 target, s 1.2; 'self' = the race-6 size-0 caster,
s 1.0, which cannot move).
usage: compare_dev5.py <gate_dir> <repo> <out_b1118.tsv> <out_b1130.tsv>"""
import sys, os, csv, math

gate, repo, out18, out30 = sys.argv[1:5]


def num(v):
    try:
        f = float(str(v).replace('+', ''))
        return None if math.isnan(f) else f
    except (TypeError, ValueError):
        return None


def summ(path, sid):
    if not os.path.exists(path):
        return {}
    for r in csv.DictReader(open(path), delimiter='\t'):
        if r.get('spell') == str(sid):
            return r
    return {}


def moved(b, a, rel=None, absb=None):
    if b is None or a is None:
        return (b is None) != (a is None)
    tol = abs(b) * rel if rel is not None else absb
    return abs(a - b) > tol + 1e-9


def fmt(v, sign=False):
    return '' if v is None else (f'{v:+.3f}' if sign else f'{v:.3f}')


targets = {}
for line in open(os.path.join(repo, 'refactor/B1152/plans/sweep/list.tsv')):
    w = line.rstrip('\n').split('\t')
    if len(w) >= 4:
        targets['r' + w[0]] = w[2]
tour_tgt = {'93': 'other', '38': 'other', '54': 'other', '656': 'other', '41': 'other', '253': 'self', '458': 'other'}
rows18 = list(csv.DictReader(open(os.path.join(repo, 'refactor/B1157/b1118_table.tsv')), delimiter='\t'))
n_moved = 0
with open(out18, 'w') as o:
    o.write('row\tspell\ttarget\tland_b1157\tland_before\tland_after\td_land\tend_b1157\tend_before\tend_after\td_end\t'
            'src_b1157\tsrc_before\tsrc_after\td_src\tmoved\towner\n')
    for r in rows18:
        row, sid = r['row'], r['spell']
        tag = ('tour_' + sid) if row.startswith('tour') else f'{row}_{sid}'
        tgt = tour_tgt.get(sid, '') if row.startswith('tour') else targets.get(row, '')
        b = summ(f'{gate}/before/measure/b1118_{tag}/summary.tsv', sid)
        a = summ(f'{gate}/after/measure/b1118_{tag}/summary.tsv', sid)
        lb, la, eb, ea = num(b.get('land_cov')), num(a.get('land_cov')), num(b.get('end')), num(a.get('end'))
        sb, sa = num(b.get('src_cov')), num(a.get('src_cov'))
        mv = [k for k, m in (('land', moved(lb, la, rel=0.03)), ('end', moved(eb, ea, absb=0.10)),
                             ('src', moved(sb, sa, rel=0.03))) if m]
        if not b or not a:
            owner = 'NOT RUN (' + ('before ' if not b else '') + ('after' if not a else '') + ')'
        elif mv:
            n_moved += 1
            owner = ('B1164 s = 1.2: the stage on the race-1 size-6 target (spell_fx_attach_scale.h)'
                     if tgt == 'other' else 'UNATTRIBUTED (s = 1.0 on the self target)')
        else:
            owner = ''
        o.write('\t'.join([row, sid, tgt, fmt(num(r['land_cov_n'])), fmt(lb), fmt(la),
                           fmt(None if lb is None or la is None else la - lb, True),
                           fmt(num(r['end_n']), True), fmt(eb, True), fmt(ea, True),
                           fmt(None if eb is None or ea is None else ea - eb, True),
                           fmt(num(r['src_cov_n'])), fmt(sb), fmt(sa),
                           fmt(None if sb is None or sa is None else sa - sb, True), ','.join(mv), owner]) + '\n')
print(f'b1118: {len(rows18)} rows, {n_moved} moved -> {out18}')

rows30 = list(csv.DictReader(open(os.path.join(repo, 'refactor/B1157/b1130_table.tsv')), delimiter='\t'))
tt = {}
for r in csv.DictReader(open('/mnt/projects/fable/review/B1130/record_table.tsv'), delimiter='\t'):
    tt[r[list(r)[0]]] = r
n_moved = 0
with open(out30, 'w') as o:
    o.write('record\tkind\tland_b1157\tland_before\tland_after\td_land\tend_b1157\tend_before\tend_after\td_end\t'
            'src_b1157\tsrc_before\tsrc_after\td_src\tmoved\towner\n')
    for r in rows30:
        rec, kind = r['record'], r['kind']

        def one(label):
            p = f'{gate}/{label}/measure/b1130_{kind}_r{rec}/b1130_rows.tsv'
            if not os.path.exists(p):
                return {}
            rr = list(csv.DictReader(open(p), delimiter='\t'))
            return rr[0] if rr else {}
        b, a = one('before'), one('after')
        lb, la = num(b.get('landing_cov')), num(a.get('landing_cov'))
        eb, ea = num(b.get('landing_end_s')), num(a.get('landing_end_s'))
        sb, sa = num(b.get('source_cov')), num(a.get('source_cov'))
        mv = [k for k, m in (('land', moved(lb, la, rel=0.03)), ('end', moved(eb, ea, absb=0.10)),
                             ('src', moved(sb, sa, rel=0.03))) if m]
        if not b or not a:
            owner = 'NOT RUN'
        elif mv:
            n_moved += 1
            owner = ('B1164 s = 1.2: the stage on the race-1 size-6 target (spell_fx_attach_scale.h)'
                     if kind == 'fixed_npc' else 'UNATTRIBUTED (fixed_self: s = 1.0 on the caster)')
        else:
            owner = ''
        o.write('\t'.join([rec, kind, fmt(num(r['b1157_landing_cov'])), fmt(lb), fmt(la),
                           fmt(None if lb is None or la is None else la - lb, True),
                           fmt(num(r['b1157_landing_end_s'])), fmt(eb), fmt(ea),
                           fmt(None if eb is None or ea is None else ea - eb, True),
                           fmt(num(r['b1157_source_cov'])), fmt(sb), fmt(sa),
                           fmt(None if sb is None or sa is None else sa - sb, True), ','.join(mv), owner]) + '\n')
print(f'b1130: {len(rows30)} rows, {n_moved} moved -> {out30}')
