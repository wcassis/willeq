#!/usr/bin/env python3
"""B1186 (#505): re-derive the gate's landing_cov at the UNIFIED window.

THE GATE compares willeq's landing coverage (lit px inside a fixed crop,
threshold 80) against the B1178 v3 oracle references.  Coverage = lit px /
window, where the window is a captured figure's height, squared.  Before B1186
the two sides of that comparison used DIFFERENT windows:

  TOUR (b1118 rig):
    willeq side: the b1118 rig's OWN rendered-caster height, 235 px --
      refactor/B1155/scripts/b1123_summary.py:15 (`MPX = 235.0`), the rig
      solving the camera until its rendered caster silhouette is exactly that
      (tools/model_viewer/model_viewer_fx_harness.cpp:820-824, "the B1118
      tour's own numbers are row 605 / 235 px"); crop 170,0,1060,650
      (review/B1139/scripts/harness_measure.py:42).
    oracle side (B1178 v3): the ORACLE capture's own measured-caster height,
      214 px, measured off the oracle's own stills at the pinned camera
      (review/B1178/worker/measure_leg.sh: `MODEL=214` default, comment "model
      214 px: head row 388 ... feet 602 ... vision cross-check 385/600"; crop
      150,60,900,540; every per-leg timing file prints "model 214px").
  B1130: BOTH sides already share the capture's own window, 209 px (the
      oracle capture's caster at the fixed rig, review/B1130/geom/r51.json
      fixed_npc / r131.json fixed_self; willeq side
      refactor/B1155/scripts/harness_measure_b1130.py:53-55 GEOM dict, oracle
      side review/B1130/scripts/cast_measure.py reading the same files).

WILL'S RULE (the issue): like with like -- the window on both sides must be
the capture's own, measured, never a fitted proxy.  The reference is
BLESS-ONLY, so the side that moves is the willeq one: it is re-derived at the
oracle capture's own per-row window (the 214 the oracle LANDING lines were
normalized by), from the SAME measured frames -- no re-render, no tuned
harness value, no reference changed.  On b1130 the unification is a verified
no-op (window_before == window_after == 209 px) and the table records the
per-row geom provenance.

The re-derivation mirrors the committed instrument EXACTLY and changes only
the divisor:
  tour: peak of the same lit80.tsv series b123 uses (t_rel >= +1.0 s;
    refactor/B1155/scripts/b1123_summary.py:28-29) -- / 235^2 before,
    / <oracle leg's model px>^2 after (parsed from that leg's v3 timing file).
  b1130: the committed landing_cov column (RISE coverage at mpx^2;
    refactor/B1155/scripts/harness_measure_b1130.py:94-104) -- identical at
    both windows; the row records mpx from the kind's geom file.

Oracle values are READ as committed, never re-measured: the LANDING-line
"coverage" field of the v3 timing files (tour) and
refactor/B1153/tables/gate_c.tsv `oracle_landing_cov` (b1130).

Band: landing_cov is +/-3 % relative (refactor/B1155/scripts/band_table.py
bands; the same rule B1184 judged with).

usage: unify_window.py <measure_root> <out_dir>
         [--b1178 DIR]  (default /mnt/projects/fable/review/B1178/worker/measure-v3-r3)
         [--b1130 DIR]  (default /mnt/projects/fable/review/B1130)
         [--wt DIR]     (default: this repo, resolved from the script path)
Writes <out_dir>/unified_landing.tsv (one row per gate row) and prints it.
"""
import argparse, json, os, re, sys
import numpy as np

# the BEFORE tour window: the constant the committed instrument divides by
# (refactor/B1155/scripts/b1123_summary.py:15).  Cited, not tuned.
WIN_BEFORE_TOUR = 235.0
# the B1178 v3 leg each tour spell was captured in (measure-v3-r3 dir names).
TOUR_LEGS = {93: 'dru5', 38: 'wiz5', 54: 'wiz5', 656: 'wiz5',
             41: 'ench5', 253: 'dru5', 458: 'wiz5'}
BAND = 0.03   # landing_cov +/-3 % relative (band_table.py)

def verdict(cov, oracle):
    if cov is None or oracle in (None, ''):
        return 'n-a'
    r = cov / oracle
    if abs(r - 1.0) <= BAND:
        return 'PASS'
    return 'short' if r < 1.0 else 'over'

def load_lit80(d):
    # b123's exact read: refactor/B1155/scripts/b1123_summary.py:28-29
    a = np.loadtxt(f'{d}/lit80.tsv', skiprows=1)
    return a[:, 2], a[:, 3]

def tour_rows(measure, b1178):
    out = []
    for s in sorted(TOUR_LEGS):
        leg = TOUR_LEGS[s]
        d = f'{measure}/tour_{s}/fable_{s}'
        tim = f'{b1178}/b1118ref_{leg}/spell{s}.timing.txt'
        row = {'sweep': 'tour', 'row': s, 'kind': TOUR_LEGS[s], 'spell': s,
               'win_source_t': f'review/B1178/worker/measure-v3-r3/b1118ref_{leg}/spell{s}.timing.txt'}
        try:
            ta, lit = load_lit80(d)
            w = np.where(ta >= 1.0)[0]
            i = w[np.argmax(lit[w])]
            litpx, t = int(lit[i]), float(ta[i])
        except FileNotFoundError:
            row.update(win_b=WIN_BEFORE_TOUR, win_a='', litpx='', t='', cov_b='', cov_a='',
                       delta='', oracle='', ratio_b='', ratio_a='', v_b='n-a', v_a='n-a',
                       note=f'missing {d}/lit80.tsv')
            out.append(row); continue
        txt = open(tim).read()
        m = re.search(r'model (\d+(?:\.\d+)?)px', txt)
        if not m:
            row.update(win_b=WIN_BEFORE_TOUR, win_a='', litpx=litpx, t=t, cov_b='', cov_a='',
                       delta='', oracle='', ratio_b='', ratio_a='', v_b='n-a', v_a='n-a',
                       note=f'no model line in {tim}')
            out.append(row); continue
        mpx = float(m.group(1))          # the oracle capture's own height, per row
        cov_b = litpx / WIN_BEFORE_TOUR ** 2
        cov_a = litpx / mpx ** 2
        # the v3 LANDING line, both shapes:
        #   "LANDING onset f132 Act+0.394s ... peak f189 Act+2.294s coverage 4.479 model^2"
        #   "LANDING none above thr (max 219405px = 4.7909 model^2)"   (spell 253, the
        #     raw-max fallback reading: the landing never exceeded the active threshold)
        mo = re.search(r'LANDING .*peak f(\d+) Act([+-][\d.]+)s coverage ([\d.]+) model\^2', txt)
        note = 'unified: /oracle-capture height^2'
        if mo:
            oracle = float(mo.group(3))
        else:
            mo = re.search(r'LANDING none above thr \(max (\d+)px = ([\d.]+) model\^2\)', txt)
            oracle = float(mo.group(2)) if mo else None
            if mo:
                note += '; oracle reading is the raw-max fallback (none above thr)'
        row.update(win_b=WIN_BEFORE_TOUR, win_a=mpx, litpx=litpx, t=round(t, 3),
                   cov_b=round(cov_b, 3), cov_a=round(cov_a, 3),
                   delta=round(cov_a - cov_b, 3), oracle=oracle,
                   ratio_b=round(cov_b / oracle, 3) if oracle else '',
                   ratio_a=round(cov_a / oracle, 3) if oracle else '',
                   v_b=verdict(cov_b, oracle), v_a=verdict(cov_a, oracle),
                   note='unified: /oracle-capture height^2')
        out.append(row)
    return out

def b1130_rows(measure, b1130, wt):
    # the reference table (committed; B1178 regenerated) + the per-kind geom
    # provenance (review/B1130/geom/r51.json fixed_npc, r131.json fixed_self,
    # as harness_measure_b1130.py:53-55 names them).
    tab = [l.rstrip('\n').split('\t') for l in open(f'{wt}/refactor/B1153/tables/gate_c.tsv') if l.strip()]
    hdr = tab[0]
    gi, oi, li, ei, ki = (hdr.index(c) for c in
                          ('record', 'oracle_landing_cov', 'harness_landing_cov',
                           'harness_landing_end_s', 'kind'))
    # record -> (spell, kind) from the canonical sweep table
    rec_info = {}
    rt = [l.rstrip('\n').split('\t') for l in open(f'{b1130}/record_table.tsv') if l.strip()]
    for r in rt[1:]:
        if len(r) > 11:
            cap = r[11]                      # the captures column ($12, as run_b1186.sh reads it)
            rec_info[int(r[0])] = (r[5], 'fixed_npc' if 'fixed_npc' in cap else 'fixed_self')
    kind_geom = {'fixed_npc': f'{b1130}/geom/r51.json', 'fixed_self': f'{b1130}/geom/r131.json'}
    kind_key = {'fixed_npc': 'cast_fixed_npc', 'fixed_self': 'cast_fixed_self'}
    out = []
    for r in tab[1:]:
        rec = int(r[gi])
        kind = r[ki] if len(r) > ki else (rec_info.get(rec, (None, 'fixed_npc'))[1] or 'fixed_npc')
        spell = rec_info.get(rec, (None,))[0]
        p = f'{measure}/b1130_r{rec}/b1130_rows.tsv'
        mpx = None
        try:
            mpx = float(json.load(open(kind_geom[kind]))[kind_key[kind]]['mpx'])
        except (FileNotFoundError, KeyError, ValueError, TypeError):
            pass
        row = {'sweep': 'b1130', 'row': rec, 'kind': kind, 'spell': spell or '',
               'win_source_t': f'review/B1130/geom/{os.path.basename(kind_geom[kind])} (mpx {mpx})'}
        try:
            lines = [l.rstrip('\n').split('\t') for l in open(p) if l.strip()]
            hdr = lines[0]
            rows = lines[1:]
            cov = float(rows[0][hdr.index('landing_cov')])
            lend = rows[0][hdr.index('landing_end_s')]
        except FileNotFoundError:
            row.update(win_b='', win_a='', litpx='', t='', cov_b='', cov_a='', delta='',
                       oracle=r[oi] if len(r) > oi else '', ratio_b='', ratio_a='',
                       v_b='n-a', v_a='n-a', note=f'missing {p}')
            out.append(row); continue
        oracle = float(r[oi]) if len(r) > oi and r[oi] else None
        # UNIFIED = already the capture's own window on both sides: a verified
        # no-op (same divisor), provenance per row.
        row.update(win_b=mpx, win_a=mpx, litpx='', t='', cov_b=cov, cov_a=cov, delta=0.0,
                   oracle=oracle, ratio_b=round(cov / oracle, 3) if oracle else '',
                   ratio_a=round(cov / oracle, 3) if oracle else '',
                   v_b=verdict(cov, oracle), v_a=verdict(cov, oracle),
                   note='already unified (no-op); landing_end_s ' + lend)
        out.append(row)
    return out

COLS = ['sweep', 'row', 'kind', 'spell', 'win_b', 'win_a', 'litpx', 't',
        'cov_b', 'cov_a', 'delta', 'oracle', 'ratio_b', 'ratio_a', 'v_b', 'v_a',
        'win_source_t', 'note']

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('measure')
    ap.add_argument('out')
    ap.add_argument('--b1178', default='/mnt/projects/fable/review/B1178/worker/measure-v3-r3')
    ap.add_argument('--b1130', default='/mnt/projects/fable/review/B1130')
    ap.add_argument('--wt', default=os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))
    a = ap.parse_args()
    rows = tour_rows(a.measure, a.b1178) + b1130_rows(a.measure, a.b1130, a.wt)
    os.makedirs(a.out, exist_ok=True)
    outp = f'{a.out}/unified_landing.tsv'
    with open(outp, 'w') as f:
        f.write('\t'.join(COLS) + '\n')
        for r in rows:
            f.write('\t'.join(str(r.get(c, '')) for c in COLS) + '\n')
    print(f'wrote {outp} ({len(rows)} rows)')
    for r in rows:
        print('\t'.join(str(r.get(c, '')) for c in COLS))

if __name__ == '__main__':
    main()
