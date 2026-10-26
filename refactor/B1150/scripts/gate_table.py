#!/usr/bin/env python3
"""B1150 (#470): the gate tables of refactor/B1150/README.md, measured here
(on the host that holds the collected runs), against the retired bands and
the two baselines.

usage: gate_table.py <runs_dir> [worktree]
  <runs_dir>  the runs collected from the ThinkPad
              (default /home/user/kb-evidence/kb-b1150-shapes/runs;
              collected by: rsync -a thinkpad:/mnt/projects/fable/refactor/B1150/runs/ <runs_dir>/)
  <worktree>  default /home/user/src/willeq-wt/kb-b1150-shapes

Inputs:
  <runs_dir>/b1150_r<row>_<spell>/measure/summary.tsv   the 40-record sweep
  <runs_dir>/b1150_tour_<spell>/measure/summary.tsv     the tour-7
  /mnt/projects/fable/review/B1130/record_table.tsv     the oracle (fixed camera)
  /mnt/projects/fable/review/B1149/sweep_table.tsv      the B1149 harness sweep
  /mnt/projects/fable/review/B1142/gate/regression_table.tsv  the B1142 lane-5 tour ('new' rig; the 93 reference is 3.049, NOT 2.938)
  <worktree>/tests/spell_fx_shape_oracle.h              the retired bands (gen_shape_bands.py)
  <worktree>/refactor/B1150/edd_shapes.tsv              the 40 records' EDD shape words (edd_shapes.py)

The summary.tsv columns are b1123_summary.py's: land_cov = the landing peak
lit coverage (caster heights^2), abs_end = the gap-aware landing end,
src_cov/src_ext/src_cen = the Source PEAK (coverage / extent / centroid,
caster heights), roi_l/roi_s = the sprite p90 (Landing Act+1.2..1.8 / Source
BC+0.3..Act-0.05).

The B1149 spread rule: no row may regress against review/B1149/sweep_table.tsv
by more than the p25..p75 spread of the per-row deltas; a row whose
delta is below p25 is flagged and must be explained by the shape case it now
uses (the README's per-row notes).
"""
import csv
import glob
import os
import re
import statistics as st
import sys

def f(x):
    try:
        return float(str(x).replace('+', ''))
    except (ValueError, TypeError):
        return None

def main():
    runs = sys.argv[1] if len(sys.argv) > 1 else '/home/user/kb-evidence/kb-b1150-shapes/runs'
    wt = sys.argv[2] if len(sys.argv) > 2 else '/home/user/src/willeq-wt/kb-b1150-shapes'

    # -- the retired bands (tests/spell_fx_shape_oracle.h) --
    hdr = open(os.path.join(wt, 'tests/spell_fx_shape_oracle.h')).read()
    def bands(sect):
        body = hdr.split(sect)[1].split('};')[0]
        out = {}
        for m in re.finditer(r'\{int\((\d+)\), (\d+), ([\d.eE+-]+), ([\d.eE+-]+), ([\d.eE+-]+), ([\d.eE+-]+)', body):
            out[int(m.group(1))] = dict(anchor=int(m.group(2)), lo=f(m.group(3)), hi=f(m.group(4)),
                                        hw=f(m.group(5)), cen=f(m.group(6)))
        return out
    ksrc, ktgt = bands('kSource[] = {'), bands('kTarget[] = {')

    # -- the 40 records' EDD shape words --
    shapes = {}
    for r in csv.DictReader((l for l in open(os.path.join(wt, 'refactor/B1150/edd_shapes.tsv'))
                             if not l.startswith('#')), delimiter='\t'):
        shapes.setdefault(int(r['record']), {}).setdefault(r['stage'], []).append(r['shape'])
    def shp(rec, stage):
        return '/'.join(shapes.get(rec, {}).get(stage, [])) or '-'

    # -- the collected runs --
    def summary(run):
        p = os.path.join(runs, run, 'measure', 'summary.tsv')
        if not os.path.exists(p):
            return None
        rows = [r for r in csv.DictReader(open(p), delimiter='\t')]
        return rows[0] if rows else None

    sweep, tour = {}, {}
    for d in sorted(glob.glob(os.path.join(runs, 'b1150_*'))):
        n = os.path.basename(d)
        m = re.match(r'b1150_r(\d+)_(\d+)$', n)
        if m:
            r = summary(n)
            if r: sweep[(int(m.group(1)), int(m.group(2)))] = r
        elif n.startswith('b1150_tour_'):
            r = summary(n)
            if r: tour[int(n.split('tour_')[1])] = r

    # -- the baselines --
    rt = {r['record']: r for r in csv.DictReader(
        (l for l in open('/mnt/projects/fable/review/B1130/record_table.tsv') if not l.startswith('#')),
        delimiter='\t')}
    b1149 = {(r['row'], r['spell']): r for r in csv.DictReader(
        (l for l in open('/mnt/projects/fable/review/B1149/sweep_table.tsv') if not l.startswith('#')),
        delimiter='\t')}
    b1142 = {r['spell']: r for r in csv.DictReader(
        (l for l in open('/mnt/projects/fable/review/B1142/gate/regression_table.tsv') if not l.startswith('#')),
        delimiter='\t')}

    missing = [(a, b) for a, b in sorted(sweep) if (str(a), str(b)) not in b1149]
    print(f'# runs: sweep {len(sweep)} (want 40), tour {sorted(tour)} (want 7 tour spells)')
    if missing:
        print('# ! sweep rows without a B1149 baseline:', missing)

    # -- sweep: the Source stage vs the retired band --
    print('\n### Sweep: the Source stage -- the harness vs the retired band (per row)\n')
    print('`src_cov/src_ext/src_cen` are the harness PEAK (summary.tsv, b1123_summary.py);')
    print('the band is the retired row (tests/spell_fx_shape_oracle.h): `lo..hi` in caster heights,')
    print('`cen` the measured centroid. `cen` verdict: in = src_cen within [lo, hi], short/over = below/above.')
    print('`ext x` = src_ext / (hi - lo). `d vs B1149` = the harness src_cov minus B1149\'s.\n')
    print('| row | spell | band lo..hi / cen | src_cov | src_ext | src_cen | cen | ext x | d vs B1149 |')
    print('|---|---|---|---|---|---|---|---|---|')
    dsrc = []
    dsrc_rows = []
    for (rec, sid), r in sorted(sweep.items()):
        b = ksrc.get(rec)
        bs = b1149.get((str(rec), str(sid)))
        sc, se, scn = f(r['src_cov']), f(r['src_ext']), f(r['src_cen'])
        if b:
            verdict = 'in' if b['lo'] <= scn <= b['hi'] else ('short' if scn < b['lo'] else 'over')
            extx = f'{se / (b["hi"] - b["lo"]):.2f}' if (se is not None and b['hi'] > b['lo']) else '-'
        else:
            verdict, extx = '-', '-'
        d = (sc - f(bs['harness_src_cov'])) if (bs and sc is not None) else None
        if d is not None:
            dsrc.append(d)
            dsrc_rows.append((rec, d))
        print(f"| {rec} | {sid} | {b['lo'] if b else '-'}..{b['hi'] if b else '-'} / {b['cen'] if b else '-'} | "
              f"{r['src_cov']} | {r['src_ext']} | {r['src_cen']} | {verdict} | {extx} | "
              f'{f"{d:+.3f}" if d is not None else "-"} |')

    # -- sweep: the landing vs the B1130 oracle and B1149 --
    # pass 1: collect the deltas so the spread column can be judged
    land = []
    for (rec, sid), r in sorted(sweep.items()):
        o = rt.get(str(rec))
        bs = b1149.get((str(rec), str(sid)))
        hc, he = f(r['land_cov']), f(r['abs_end'])
        oc = f(o['landing_cov']) if o else None
        oe = f(o['landing_end_s']) if o else None
        bc = f(bs['harness_land_cov']) if bs else None
        d = (hc - bc) if (hc is not None and bc is not None) else None
        land.append(dict(rec=rec, sid=sid, o=o, r=r, hc=hc, he=he, oc=oc, oe=oe, bc=bc, d=d))
    dvals = [L['d'] for L in land if L['d'] is not None]
    p25, p75 = (st.quantiles(dvals, n=4)[0::2] if len(dvals) >= 4 else (None, None))

    print('\n### Sweep: the landing -- the harness vs the B1130 oracle, and vs the B1149 sweep\n')
    print('`oracle` = review/B1130/record_table.tsv (fixed camera); `ratio` is reported as measured,')
    print('not a pass/fail band (B1149\'s instrument note: the harness geometry differs from the lane).')
    print('`d vs B1149` = the harness land_cov minus review/B1149/sweep_table.tsv\'s; the spread rule is')
    print('the p25..p75 of those deltas (below p25 = `under`, explained per row in the notes;')
    print('above p75 = `above`, an improvement).\n')
    print('| row | spell | T shape | oracle cov | harness cov | ratio | oracle end | harness end | d end | B1149 cov | d vs B1149 | spread |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|')
    for L in land:
        rec, sid, o, r = L['rec'], L['sid'], L['o'], L['r']
        hc, he, oc, oe, bc, d = L['hc'], L['he'], L['oc'], L['oe'], L['bc'], L['d']
        ratio = f'{hc / oc:.2f}' if (hc is not None and oc) else ('n/a' if hc is not None else '-')
        dend = f'{he - oe:+.2f}' if (he is not None and oe is not None) else '-'
        if d is None:
            dcol = spread = '-'
        else:
            dcol = f'{d:+.3f}'
            spread = 'under' if d < p25 else ('above' if d > p75 else 'in')
        print(f"| {rec} | {sid} | {shp(rec, 'T')} | {o['landing_cov'] if o else '-'} | {r['land_cov']} | {ratio} | "
              f"{o['landing_end_s'] if o else '-'} | {r['abs_end']} | {dend} | {bc if bc is not None else '-'} | {dcol} | {spread} |")

    if len(dvals) >= 4:
        print(f'\n# d vs B1149 (landing): median {st.median(dvals):+.3f}, '
              f'p25..p75 {p25:+.3f}..{p75:+.3f} (n {len(dvals)}); min {min(dvals):+.3f} '
              f'(row {[L["rec"] for L in land if L["d"] == min(dvals)][0]}), '
              f'max {max(dvals):+.3f} (row {[L["rec"] for L in land if L["d"] == max(dvals)][0]})')
        under = [(L['rec'], f'{L["d"]:+.3f}') for L in land if L['d'] is not None and L['d'] < p25]
        above = [(L['rec'], f'{L["d"]:+.3f}') for L in land if L['d'] is not None and L['d'] > p75]
        print(f'# under-spread (regresses more than p25..p75): '
              f'{", ".join(f"row {a} ({b})" for a, b in under) if under else "none"}')
        print(f'# above-spread (improves more): '
              f'{", ".join(f"row {a} ({b})" for a, b in above) if above else "none"}')
    if len(dsrc) >= 4:
        sp25, sp75 = st.quantiles(dsrc, n=4)[0::2]
        under = [(rec, f'{d:+.3f}') for rec, d in dsrc_rows if d < sp25]
        print(f'# d vs B1149 (source): median {st.median(dsrc):+.3f}, '
              f'p25..p75 {sp25:+.3f}..{sp75:+.3f} (n {len(dsrc)}); '
              f'under-spread: {", ".join(f"row {a} ({b})" for a, b in under) if under else "none"}')

    # -- the tour-7 vs the B1142 'new' rig --
    print('\n### Tour-7 -- the harness vs the B1142 lane-5 "new" rig (review/B1142/gate/regression_table.tsv)\n')
    print('The B1142 `new_land_cov` for 93 is the 3.049 reference (NOT the old rig\'s 2.938).')
    print('roi_l/roi_s verdicts are against the B1142 bands (roi_l_band / roi_s_band columns).\n')
    print('| spell | B1142 land | B1150 land | d | B1142 end | B1150 end | roi_l (band) | roi_s (band) |')
    print('|---|---|---|---|---|---|---|---|')
    for sid in sorted(tour):
        r, b = tour[sid], b1142.get(str(sid))
        hc, he = f(r['land_cov']), f(r['abs_end'])
        if b:
            bl, be = f(b['new_land_cov']), f(b['new_end'])
            d = f'{hc - bl:+.3f}' if (hc is not None and bl is not None) else '-'
            def v(v, band):
                x, m = f(v), re.match(r'([\d.]+)-([\d.]+)', band)
                if x is None or not m:
                    return f'{v} (-)'
                return f'{v} ({x - f(m.group(1)):+.3f} of {band})'
            roil = v(r['roi_l'], b['roi_l_band'])
            rois = v(r['roi_s'], b['roi_s_band'])
        else:
            d = be = '-'
            roil = rois = f'{r["roi_l"]} / {r["roi_s"]} (no B1142 row)'
        print(f'| {sid} | {b["new_land_cov"] if b else "-"} | {r["land_cov"]} | {d} | '
              f'{b["new_end"] if b and b["new_end"] != "-" else "-"} | {r["abs_end"]} | {roil} | {rois} |')

if __name__ == '__main__':
    main()
