#!/usr/bin/env python3
"""B1152 (#471): the gate tables of refactor/B1152/README.md, measured here
(on the host that holds the collected runs), against the B1150 gate tables
(the shapes baseline), the retired bands and the two external baselines.

usage: gate_table.py [runs_dir] [b1150_runs_dir]
  <runs_dir>         default /home/user/src/willeq-wt/kb-b1152-missile/refactor/B1152/runs
                     (the worktree is NFS-shared with the ThinkPad, so the
                     runs collected by the ThinkPad's tour7.sh/run_sweep.sh
                     are already there)
  <b1150_runs_dir>   default /home/user/kb-evidence/kb-b1150-shapes/runs

Inputs:
  <runs_dir>/b1151_r<row>_<spell>/measure/summary.tsv   the 40-record sweep
  <runs_dir>/b1151_tour_<spell>/measure/summary.tsv     the tour-7
  <b1150_runs_dir>/b1150_r<row>_<spell>/measure/summary.tsv  the B1150 shapes baseline
  <b1150_runs_dir>/b1150_tour_<spell>/measure/summary.tsv
  /mnt/projects/fable/review/B1130/record_table.tsv     the oracle (fixed camera)
  /mnt/projects/fable/review/B1149/sweep_table.tsv      the B1149 harness sweep
  /mnt/projects/fable/review/B1142/gate/regression_table.tsv  the B1142 lane-5 tour (the 93 reference is 3.049, NOT 2.938)
  <worktree>/tests/spell_fx_shape_oracle.h              the retired bands (gen_shape_bands.py)
  <worktree>/refactor/B1150/edd_shapes.tsv              the 40 records' EDD shape words (edd_shapes.py)

The summary.tsv columns are b1123_summary.py's: land_cov = the landing peak
lit coverage (caster heights^2), abs_end = the gap-aware landing end,
src_cov/src_ext/src_cen = the Source PEAK (coverage / extent / centroid,
caster heights), roi_l/roi_s = the sprite p90 (Landing Act+1.2..1.8 / Source
BC+0.3..Act-0.05).

The B1151 spread rule (the card): no row may regress against the B1150 table
by more than the p25..p75 spread of the per-row deltas (B1151 minus B1150);
a row below p25 is flagged and must be explained by the record's motion
words (the README's per-row notes). d vs B1149 is reported for continuity
with the B1150 table.
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
    runs = sys.argv[1] if len(sys.argv) > 1 else '/home/user/src/willeq-wt/kb-b1152-missile/refactor/B1152/runs'
    b1150 = sys.argv[2] if len(sys.argv) > 2 else '/home/user/kb-evidence/kb-b1150-shapes/runs'
    wt = '/home/user/src/willeq-wt/kb-b1152-missile'

    # -- the retired bands (tests/spell_fx_shape_oracle.h) --
    hdr = open(os.path.join(wt, 'tests/spell_fx_shape_oracle.h')).read()
    def bands(sect):
        body = hdr.split(sect)[1].split('};')[0]
        out = {}
        for m in re.finditer(r'\{int\((\d+)\), (\d+), ([\d.eE+-]+), ([\d.eE+-]+), ([\d.eE+-]+), ([\d.eE+-]+)', body):
            out[int(m.group(1))] = dict(anchor=int(m.group(2)), lo=f(m.group(3)), hi=f(m.group(4)),
                                        hw=f(m.group(5)), cen=f(m.group(6)))
        return out
    ksrc = bands('kSource[] = {')

    # -- the 40 records' EDD shape words (B1150's tsv) --
    shapes = {}
    for r in csv.DictReader((l for l in open(os.path.join(wt, 'refactor/B1150/edd_shapes.tsv'))
                             if not l.startswith('#')), delimiter='\t'):
        shapes.setdefault(int(r['record']), {}).setdefault(r['stage'], []).append(r['shape'])
    def shp(rec, stage):
        return '/'.join(shapes.get(rec, {}).get(stage, [])) or '-'

    # -- the collected runs --
    def summary(runsdir, run):
        p = os.path.join(runsdir, run, 'measure', 'summary.tsv')
        if not os.path.exists(p):
            return None
        rows = [r for r in csv.DictReader(open(p), delimiter='\t')]
        return rows[0] if rows else None

    def collect(runsdir, tag):
        sweep, tour = {}, {}
        for d in sorted(glob.glob(os.path.join(runsdir, tag + '_*'))):
            n = os.path.basename(d)
            m = re.match(re.escape(tag) + r'_r(\d+)_(\d+)$', n)
            if m:
                r = summary(runsdir, n)
                if r:
                    sweep[(int(m.group(1)), int(m.group(2)))] = r
            elif n.startswith(tag + '_tour_'):
                r = summary(runsdir, n)
                if r:
                    tour[int(n.split('tour_')[1])] = r
        return sweep, tour

    sweep, tour = collect(runs, 'b1151')
    sweep50, tour50 = collect(b1150, 'b1150')

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

    # -- sweep: the Source stage --
    print('\n### Sweep: the Source stage -- the harness vs the retired band, vs B1150 and B1149 (per row)\n')
    print('`src_cov/src_ext/src_cen` are the B1151 harness PEAK (summary.tsv, b1123_summary.py);')
    print('the band is the retired row (tests/spell_fx_shape_oracle.h). `cen` verdict: in = src_cen within')
    print('[lo, hi], short/over = below/above. `d vs B1150` = the B1151 src_cov minus the B1150 sweep row;')
    print('`d vs B1149` = the B1151 src_cov minus review/B1149/sweep_table.tsv\'s.\n')
    print('| row | spell | band lo..hi / cen | src_cov | src_ext | src_cen | cen | d vs B1150 | d vs B1149 |')
    print('|---|---|---|---|---|---|---|---|---|')
    d50s, d49s = [], []
    d50s_rows, d49s_rows = [], []
    for (rec, sid), r in sorted(sweep.items()):
        b = ksrc.get(rec)
        s50 = sweep50.get((rec, sid))
        bs = b1149.get((str(rec), str(sid)))
        sc, se, scn = f(r['src_cov']), f(r['src_ext']), f(r['src_cen'])
        s50c = f(s50['src_cov']) if s50 else None
        b49c = f(bs['harness_src_cov']) if bs else None
        verdict = 'in' if b and b['lo'] <= scn <= b['hi'] else ('short' if b and scn < b['lo'] else ('over' if b else '-'))
        d50 = (sc - s50c) if (sc is not None and s50c is not None) else None
        d49 = (sc - b49c) if (sc is not None and b49c is not None) else None
        if d50 is not None:
            d50s.append(d50); d50s_rows.append((rec, d50))
        if d49 is not None:
            d49s.append(d49); d49s_rows.append((rec, d49))
        print(f"| {rec} | {sid} | {b['lo'] if b else '-'}..{b['hi'] if b else '-'} / {b['cen'] if b else '-'} | "
              f"{r['src_cov']} | {r['src_ext']} | {r['src_cen']} | {verdict} | "
              f'{"-" if d50 is None else f"{d50:+.3f}"} | {"-" if d49 is None else f"{d49:+.3f}"} |')

    # -- sweep: the landing --
    land = []
    for (rec, sid), r in sorted(sweep.items()):
        o = rt.get(str(rec))
        s50 = sweep50.get((rec, sid))
        bs = b1149.get((str(rec), str(sid)))
        hc, he = f(r['land_cov']), f(r['abs_end'])
        oc = f(o['landing_cov']) if o else None
        oe = f(o['landing_end_s']) if o else None
        bc = f(bs['harness_land_cov']) if bs else None
        h50c = f(s50['land_cov']) if s50 else None
        d50 = (hc - h50c) if (hc is not None and h50c is not None) else None
        d49 = (hc - bc) if (hc is not None and bc is not None) else None
        land.append(dict(rec=rec, sid=sid, o=o, r=r, hc=hc, he=he, oc=oc, oe=oe, bc=bc, d50=d50, d49=d49))
    d50v = [L['d50'] for L in land if L['d50'] is not None]
    p25, p75 = (st.quantiles(d50v, n=4)[0::2] if len(d50v) >= 4 else (None, None))

    print('\n### Sweep: the landing -- the B1151 harness vs the B1150 shapes baseline, the B1130 oracle and the B1149 sweep\n')
    print('`B1150` = the collected B1150 sweep row (refactor/B1150/README.md "The gate", the shapes baseline);')
    print('`d vs B1150` = the B1151 land_cov minus B1150\'s, the card\'s spread rule (the p25..p75 of those deltas);')
    print('`oracle` = review/B1130/record_table.tsv (fixed camera), reported as measured (the harness geometry')
    print('differs from the lane, B1149\'s instrument note); `d vs B1149` = the B1151 land_cov minus')
    print('review/B1149/sweep_table.tsv\'s (continuity with the B1150 table).\n')
    print('| row | spell | T shape | B1150 cov | B1151 cov | d vs B1150 | spread | oracle cov | B1151/oracle | oracle end | B1151 end | d end | d vs B1149 |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for L in land:
        rec, sid, o, r = L['rec'], L['sid'], L['o'], L['r']
        hc, he, oc, oe, bc, d50, d49 = L['hc'], L['he'], L['oc'], L['oe'], L['bc'], L['d50'], L['d49']
        ratio = f'{hc / oc:.2f}' if (hc is not None and oc) else ('n/a' if hc is not None else '-')
        dend = f'{he - oe:+.2f}' if (he is not None and oe is not None) else '-'
        s50 = sweep50.get((rec, sid))
        if d50 is None:
            dcol = spread = '-'
        else:
            dcol = f'{d50:+.3f}'
            spread = 'under' if d50 < p25 else ('above' if d50 > p75 else 'in')
        print(f"| {rec} | {sid} | {shp(rec, 'T')} | {s50['land_cov'] if s50 else '-'} | {r['land_cov']} | {dcol} | {spread} | "
              f"{o['landing_cov'] if o else '-'} | {ratio} | {o['landing_end_s'] if o else '-'} | "
              f"{r['abs_end']} | {dend} | {'-' if d49 is None else f'{d49:+.3f}'} |")

    if len(d50v) >= 4:
        print(f'\n# d vs B1150 (landing): median {st.median(d50v):+.3f}, p25..p75 {p25:+.3f}..{p75:+.3f} (n {len(d50v)}); '
              f'min {min(d50v):+.3f} (row {[L["rec"] for L in land if L["d50"] == min(d50v)][0]}), '
              f'max {max(d50v):+.3f} (row {[L["rec"] for L in land if L["d50"] == max(d50v)][0]})')
        under = [(L['rec'], f'{L["d50"]:+.3f}') for L in land if L['d50'] is not None and L['d50'] < p25]
        above = [(L['rec'], f'{L["d50"]:+.3f}') for L in land if L['d50'] is not None and L['d50'] > p75]
        print(f'# under-spread vs B1150 (regresses more than p25..p75): '
              f'{", ".join(f"row {a} ({b})" for a, b in under) if under else "none"}')
        print(f'# above-spread vs B1150 (improves more): '
              f'{", ".join(f"row {a} ({b})" for a, b in above) if above else "none"}')
    if len(d49s) >= 4:
        sp25, sp75 = st.quantiles(d49s, n=4)[0::2]
        under = [(rec, f'{d:+.3f}') for rec, d in d49s_rows if d < sp25]
        print(f'# d vs B1149 (source): median {st.median(d49s):+.3f}, p25..p75 {sp25:+.3f}..{sp75:+.3f} (n {len(d49s)}); '
              f'under-spread: {", ".join(f"row {a} ({b})" for a, b in under) if under else "none"}')
    d49v = [L['d49'] for L in land if L['d49'] is not None]
    if len(d49v) >= 4:
        q25, q75 = st.quantiles(d49v, n=4)[0::2]
        print(f'# d vs B1149 (landing): median {st.median(d49v):+.3f}, p25..p75 {q25:+.3f}..{q75:+.3f} (n {len(d49v)})')

    # -- the tour-7 --
    print('\n### Tour-7 -- the B1151 harness vs the B1142 lane-5 "new" rig and the B1150 tour\n')
    print('The B1142 `new_land_cov` for 93 is the 3.049 reference (NOT the old rig\'s 2.938).')
    print('`d vs B1150` = the B1151 land_cov minus the B1150 tour row.\n')
    print('| spell | B1142 land | B1150 land | B1151 land | d vs B1142 | d vs B1150 | B1142 end | B1151 end | roi_l (band) | roi_s (band) |')
    print('|---|---|---|---|---|---|---|---|---|---|')
    for sid in sorted(tour):
        r, b = tour[sid], b1142.get(str(sid))
        t50 = tour50.get(sid)
        hc, he = f(r['land_cov']), f(r['abs_end'])
        if b:
            bl, be = f(b['new_land_cov']), f(b['new_end'])
            d142 = f'{hc - bl:+.3f}' if (hc is not None and bl is not None) else '-'
            def v(v, band):
                x, m = f(v), re.match(r'([\d.]+)-([\d.]+)', band)
                lo = f(m.group(1)) if m else None
                if x is None or lo is None:
                    return f'{v} (-)'
                return f'{v} ({x - lo:+.3f} of {band})'
            roil = v(r['roi_l'], b['roi_l_band'])
            rois = v(r['roi_s'], b['roi_s_band'])
        else:
            d142 = be = '-'
            roil = rois = f'{r["roi_l"]} / {r["roi_s"]} (no B1142 row)'
        t50c = f(t50['land_cov']) if t50 else None
        d50 = f'{hc - t50c:+.3f}' if (hc is not None and t50c is not None) else '-'
        print(f'| {sid} | {b["new_land_cov"] if b else "-"} | {t50["land_cov"] if t50 else "-"} | '
              f'{r["land_cov"]} | {d142} | {d50} | {be if be != "-" else (b["new_end"] if b and b["new_end"] != "-" else "-")} | '
              f'{r["abs_end"]} | {roil} | {rois} |')

if __name__ == '__main__':
    main()
