#!/usr/bin/env python3
"""B1157: the b1118 re-gate table, before/after, one row per tour spell and
per B1130 record.

before:
  tour-7   = B1151's committed tour table (refactor/B1151/README.md, the
             "Tour-7" section) -- B1152 did not re-run the tour, so B1151's
             rows are the last before numbers (B1152's own claim: 40/40
             records within 0.028 src_cov of B1151, i.e. B1152 == B1151 for
             the idle caster).
  40 recs  = B1152's runs (refactor/B1152/runs/b1152_r<rec>_<spell>/measure/
             summary.tsv) -- the card's "before/after against the B1152
             table".

after = this sweep's runs (<ev>/measure/b1157_{tour_,}r<...>/summary.tsv),
the same instrument (B1139's harness_measure.py + B1130's anchor.py).

Gate axes (harness-against-harness, the b1118 rig is a proxy, not the lane):
landing coverage +/-3 % (B1131's band), landing end +/-0.10 s (B1149's
method). The Source axis (src_cov, src_on25, src_cen) is where the cast
animation is ALLOWED to move the gate -- the raised hand changes what the
Source window contains; a row moving there toward the oracle is this card's
own effect, not a regression. A miss is owned: the writer assigns it (the
sim's word is out of this card's scope per Will's rule; a model/animation
difference; or "unattributed").

usage: compare_b1118.py <ev_b1118_dir> <b1152_runs_dir> <b1151_readme> <out.tsv>
"""
import sys, os, re, csv

EV, RUNS, B1151R, out = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

def f(x):
    try:
        return float(str(x).replace('+', ''))
    except (TypeError, ValueError):
        return None

def load_summary(path):
    if not os.path.exists(path):
        return {}
    return {r['spell']: r for r in csv.DictReader(open(path), delimiter='\t')}

def fmt(v, spec='g'):
    return f'{v:{spec}}' if v is not None else ''

# B1151's committed tour table rows look like:
# | 38 | 2.650 | 2.742 | 2.113 | -0.537 | -0.629 | 4.831 | +6.467 | 0.041 (...) | 0.067 (...) |
# (cols: spell, B1142 land, B1150 land, B1151 land, d, d, B1142 end, B1151
#  end, roi_l, roi_s). Scoped to the Tour-7 section so the 40-record table
# (same leading shape) cannot contaminate it.
tour_before = {}
in_tour = False
for line in open(B1151R):
    if line.startswith('### Tour-7'):
        in_tour = True
        continue
    if in_tour and line.startswith('#'):
        break
    if not in_tour:
        continue
    m = re.match(r'\|\s*(\d+)\s*\|\s*[\d.]+\s*\|\s*[\d.]+\s*\|\s*([\d.]+)\s*\|[^|]*\|[^|]*\|\s*[^\|]*\|\s*([+-]?[\d.]+)\s*\|', line)
    if m:
        tour_before[m.group(1)] = {'land': f(m.group(2)), 'end': f(m.group(3))}

rows = []

def add(tag, spell, base_tag, v, lc_b, lc_n, en_b, en_n, sc_b, sc_n, so_b, so_n, sc_c_b, sc_c_n, owner):
    rows.append([tag, spell, base_tag, v,
                 fmt(lc_b), fmt(lc_n), f'{lc_n - lc_b:+.3f}' if lc_b is not None and lc_n is not None else '',
                 fmt(en_b, '+.3f'), fmt(en_n, '+.3f'), f'{en_n - en_b:+.3f}' if en_b is not None and en_n is not None else '',
                 fmt(sc_b), fmt(sc_n), f'{sc_n - sc_b:+.3f}' if sc_b is not None and sc_n is not None else '',
                 fmt(so_b, '+.3f'), fmt(so_n, '+.3f'),
                 fmt(sc_c_b, '.2f'), fmt(sc_c_n, '.2f'),
                 owner])

# Tour-7: before from B1151's committed rows, after from this sweep.
# Owner per miss: the tour was NOT re-gated by B1152 (its 40-record sweep
# covered the same ground and claimed 40/40 within 0.028 src_cov of B1151),
# so a tour-row delta is the B1151->B1157 span, and where the spell IS one of
# the 40 records (93 = record 0) the B1152->B1157 half can be read off the
# record row: it is +0.000 land / +0.000 end, i.e. the tour-row delta is
# B1152's sim change, not B1157's animation. 458 has no B1152 baseline at
# all (not one of the 40), so its row is the honest 'unattributed'.
TOUR_MISS_OWNER = {
    '93': 'B1152 sim delta (tour not re-gated by B1152; B1152->B1157 for this spell = record r0: +0.000 land / +0.000 end)',
    '38': 'B1152 sim delta (tour not re-gated by B1152; 38 renders identically to 93 in both gates: 2.113/+6.467 in B1151, 1.993/+6.633 here; the B1152->B1157 half is record r0: +0.000 land / +0.000 end)',
    '458': 'unattributed (no B1152 baseline: 458 is not one of the 40 records; the row spans B1151->B1157 incl. B1152\'s sim change)',
}
for sid in ('93', '38', '54', '656', '41', '253', '458'):
    n = load_summary(os.path.join(EV, 'measure', f'b1157_tour_{sid}', 'summary.tsv')).get(sid)
    if not n:
        add(f'tour {sid}', sid, 'b1151_tour_' + sid, 'MISSING-AFTER', None, None, None, None, None, None, None, None, None, None, '')
        continue
    b = tour_before.get(sid, {})
    lc_b, lc_n = b.get('land'), f(n['land_cov'])
    en_b, en_n = b.get('end'), f(n['end'])
    v = 'PASS'
    if lc_b is not None and lc_n is not None and lc_b > 0 and abs(lc_n - lc_b) / lc_b > 0.03:
        v = 'short' if lc_n < lc_b else 'over'
    if en_b is not None and en_n is not None and abs(en_n - en_b) > 0.10:
        v = 'short' if en_n < en_b else 'over'
    add(f'tour {sid}', sid, 'b1151_tour_' + sid, v,
        lc_b, lc_n, en_b, en_n, None, f(n['src_cov']), None, f(n['src_on25']), None, f(n['src_cen']),
        TOUR_MISS_OWNER.get(sid, 'unattributed') if v != 'PASS' else '')

# The 40 records: before from B1152's runs, after from this sweep. B1152's
# plan list has no header: record, spell_id, target, cast_ms.
# Owner per miss: the sim is the SAME tree (B1157 touches only the animation
# path) and the plans/seed/fps are B1152's own, so every gate delta on a
# record row is the caster's animation: the raised hand (the spell's own
# CastingAnim track, not o01) in the instrument's whole-frame lit counts,
# the hand-anchored held Source (released Act+0.33, alive into the landing
# window's first 0.53 s) and the stail-coupled landing threshold
# (review/B1132/scripts/fx_timing.py 45-46). r8 is self-target: the landing
# stage is ON the caster, so the pose is in the landing window outright.
with open(os.path.join(RUNS, '..', 'plans', 'sweep', 'list.tsv')) as pl:
    plan = [r for r in csv.DictReader(pl, fieldnames=['record', 'spell_id', 'target', 'cast_ms'], delimiter='\t')]
for r in plan:
    rec, sid = r['record'], r['spell_id']
    btag = f'b1152_r{rec}_{sid}'
    b = load_summary(os.path.join(RUNS, btag, 'measure', 'summary.tsv')).get(sid)
    n = load_summary(os.path.join(EV, 'measure', f'b1157_r{rec}_{sid}', 'summary.tsv')).get(sid)
    if not b or not n:
        add(f'r{rec}', sid, btag, 'MISSING-' + ('BASELINE' if not b else 'AFTER'),
            None, None, None, None, None, None, None, None, None, None, '')
        continue
    lc_b, lc_n = f(b['land_cov']), f(n['land_cov'])
    en_b, en_n = f(b['end']), f(n['end'])
    v = 'PASS'
    if lc_b and lc_n and lc_b > 0 and abs(lc_n - lc_b) / lc_b > 0.03:
        v = 'short' if lc_n < lc_b else 'over'
    if en_b is not None and en_n is not None and abs(en_n - en_b) > 0.10:
        v = 'short' if en_n < en_b else 'over'
    add(f'r{rec}', sid, btag, v,
        lc_b, lc_n, en_b, en_n,
        f(b['src_cov']), f(n['src_cov']), f(b['src_on25']), f(n['src_on25']),
        f(b['src_cen']), f(n['src_cen']),
        ('model/animation (B1157 cast pose: the raised hand -- the only delta vs the B1152 baseline; '
         + ('self-target landing ON the caster, ' if r['target'] == 'self' else '')
         + 'see the README for the mechanism)') if v != 'PASS' else '')

with open(out, 'w') as fh:
    w = csv.writer(fh, delimiter='\t')
    w.writerow(['row', 'spell', 'baseline', 'verdict',
                'land_cov_b', 'land_cov_n', 'd_land_cov',
                'end_b', 'end_n', 'd_end',
                'src_cov_b', 'src_cov_n', 'd_src_cov',
                'src_on25_b', 'src_on25_n', 'src_cen_b', 'src_cen_n', 'owner'])
    w.writerows(rows)
print(f'{len(rows)} rows -> {out}')
