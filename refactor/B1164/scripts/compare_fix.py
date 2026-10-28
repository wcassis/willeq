#!/usr/bin/env python3
"""B1164 (#483) fix leg (review note 7941 finding 3): the B1157 gate re-run on the merged tree.

Per row and sweep, the builds side by side (every number from that run's own measure file):
  b1157    refactor/B1157/b1118_table.tsv / b1130_table.tsv (B1157's tip; its b1130 sweep ran on
           the ThinkPad's i915 DRM path, depth 16; its b1118 sweep on NVIDIA surfaceless, depth 24)
  base     dev5's before, 4275bc51's code, NVIDIA surfaceless (<dev5_gate>/before)
  base_i915 (b1130 only) 4275bc51's code on the i915 path, B1157's renderer (<attrib>/E3_base_i915)
  before   gitlab/fable f61fafb4 (B1169 determinism + B1166 + B1165 + B1168 + B1173), NVIDIA
  after    this branch's tip, NVIDIA (<fix_gate>/before, <fix_gate>/after)
A pair MOVES when (an NA cell -- a number B1157's table does not give -- is not compared) land_cov changes by > 3 %, the end by > 0.10 s or src_cov by > 3 %.
Columns mv_after = after vs before (this card), mv_tip = before vs base (the merged cards),
mv_gpu = base_i915 vs base (the renderer), mv_code = base_i915 vs b1157 (code B1157 -> 4275bc51).
usage: compare_fix.py <fix_gate> <dev5_gate> <attrib> <repo> <out_b1118.tsv> <out_b1130.tsv>"""
import csv
import math
import os
import sys

fix, dev5, attrib, repo, out18, out30 = sys.argv[1:7]


def num(v):
    try:
        f = float(str(v).replace('+', ''))
        return None if math.isnan(f) else f
    except (TypeError, ValueError):
        return None


NA = 'NA'  # a column B1157's table leaves empty (it did not report that number)


def ref(v):
    return NA if str(v).strip() == '' else num(v)


def fmt(v):
    return v if v == NA else ('nan' if v is None else f'{v:.3f}')


def moves(x, y):
    out = []
    for k, rel, ab in (('land', 0.03, None), ('end', None, 0.10), ('src', 0.03, None)):
        a, b = x.get(k), y.get(k)
        if NA in (a, b):
            continue
        if a is None or b is None:
            if (a is None) != (b is None):
                out.append(k)
            continue
        if abs(b - a) > (abs(a) * rel if rel else ab) + 1e-9:
            out.append(k)
    return ','.join(out)


def s18(root, tag, sid):
    p = f'{root}/measure/b1118_{tag}/summary.tsv'
    if not os.path.exists(p):
        return None
    for r in csv.DictReader(open(p), delimiter='\t'):
        if r.get('spell') == str(sid):
            return {'land': num(r.get('land_cov')), 'end': num(r.get('end')), 'src': num(r.get('src_cov'))}
    return None


def s30(root, tag):
    p = f'{root}/measure/{tag}/b1130_rows.tsv'
    if not os.path.exists(p):
        return None
    rr = list(csv.DictReader(open(p), delimiter='\t'))
    if not rr:
        return None
    r = rr[0]
    return {'land': num(r.get('landing_cov')), 'end': num(r.get('landing_end_s')), 'src': num(r.get('source_cov'))}


def cells(d):
    return [fmt(d['land']), fmt(d['end']), fmt(d['src'])] if d else ['-', '-', '-']


targets = {}
for line in open(os.path.join(repo, 'refactor/B1152/plans/sweep/list.tsv')):
    w = line.rstrip('\n').split('\t')
    if len(w) >= 4:
        targets['r' + w[0]] = w[2]
tour_tgt = {'93': 'other', '38': 'other', '54': 'other', '656': 'other', '41': 'other', '253': 'self', '458': 'other'}

hdr = []
for lab in ('b1157', 'base', 'before', 'after'):
    hdr += [f'land_{lab}', f'end_{lab}', f'src_{lab}']
with open(out18, 'w') as o:
    o.write('\t'.join(['row', 'spell', 'target'] + hdr + ['mv_after', 'mv_tip', 'mv_b1157_base']) + '\n')
    n = [0, 0, 0]
    for r in csv.DictReader(open(os.path.join(repo, 'refactor/B1157/b1118_table.tsv')), delimiter='\t'):
        row, sid = r['row'], r['spell']
        tag = ('tour_' + sid) if row.startswith('tour') else f'{row}_{sid}'
        tgt = tour_tgt.get(sid, '') if row.startswith('tour') else targets.get(row, '')
        k = {'land': ref(r['land_cov_n']), 'end': ref(r['end_n']), 'src': ref(r['src_cov_n'])}
        base, bef, aft = s18(f'{dev5}/before', tag, sid), s18(f'{fix}/before', tag, sid), s18(f'{fix}/after', tag, sid)
        m = [moves(bef, aft) if bef and aft else 'NOT RUN', moves(base, bef) if base and bef else 'NOT RUN',
             moves(k, base) if base else 'NOT RUN']
        for i in range(3):
            n[i] += bool(m[i]) and m[i] != 'NOT RUN'
        o.write('\t'.join([row, sid, tgt] + cells(k) + cells(base) + cells(bef) + cells(aft) + m) + '\n')
print(f'b1118: moved after-vs-before {n[0]}, before-vs-base {n[1]}, base-vs-b1157 {n[2]} -> {out18}')

hdr = []
for lab in ('b1157', 'base_i915', 'base', 'before', 'after'):
    hdr += [f'land_{lab}', f'end_{lab}', f'src_{lab}']
with open(out30, 'w') as o:
    o.write('\t'.join(['record', 'kind'] + hdr + ['mv_after', 'mv_tip', 'mv_gpu', 'mv_code']) + '\n')
    n = [0, 0, 0, 0]
    for r in csv.DictReader(open(os.path.join(repo, 'refactor/B1157/b1130_table.tsv')), delimiter='\t'):
        rec, kind = r['record'], r['kind']
        tag = f'b1130_{kind}_r{rec}'
        k = {'land': ref(r['b1157_landing_cov']), 'end': ref(r['b1157_landing_end_s']),
             'src': ref(r['b1157_source_cov'])}
        gi = s30(f'{attrib}/E3_base_i915', tag)
        base, bef, aft = s30(f'{dev5}/before', tag), s30(f'{fix}/before', tag), s30(f'{fix}/after', tag)
        m = [moves(bef, aft) if bef and aft else 'NOT RUN', moves(base, bef) if base and bef else 'NOT RUN',
             moves(gi, base) if gi and base else 'NOT RUN', moves(k, gi) if gi else 'NOT RUN']
        for i in range(4):
            n[i] += bool(m[i]) and m[i] != 'NOT RUN'
        o.write('\t'.join([rec, kind] + cells(k) + cells(gi) + cells(base) + cells(bef) + cells(aft) + m) + '\n')
print(f'b1130: moved after-vs-before {n[0]}, before-vs-base {n[1]}, renderer {n[2]}, code b1157->base {n[3]} -> {out30}')
