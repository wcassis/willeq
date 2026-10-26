#!/usr/bin/env python3
"""B1151 (#471): the motion signatures (the issue's gate) -- per record, the
per-frame centroid height and lit extent over time, harness vs oracle, from
the same instrument on both sides (review/B1130/scripts/anchor.py per-frame
rows: cov, centroid h/dx, h_p5..p95, extent bands, per active frame).

usage: motion_table.py [runs_dir] [oracle_dir]
  <runs_dir>   default /home/user/src/willeq-wt/kb-b1151-frames/refactor/B1151/runs
               rows: b1151_r<row>_<spell>/anchor_{SOURCE,LANDING}.tsv
               (taken by refactor/B1151/scripts/run_sweep.sh from the FX_KEEP
               frames, b1118 rig: feet 640,605, model 235)
  <oracle_dir> default /home/user/src/willeq-wt/kb-b1151-frames/refactor/B1151/oracle_anchor
               rows: <dir>_{SOURCE,LANDING}.tsv (review/B1130 measure dirs;
               rig per record in rig.tsv: fixed rigs in caster units for
               SOURCE, fixed_npc LANDING in target units 940,652/325px;
               oracle_anchor.sh)

Per (record, stage, side): ONSET = first active frame, PEAK = max-cov active
frame, END = last active frame; drift = centroid height at PEAK minus at
ONSET (and at END minus at ONSET), in that side's model heights; growth =
cov(PEAK)/cov(ONSET) and h_p95(PEAK) - h_p95(ONSET); duration = t(END) -
t(ONSET). Verdict vs the oracle: drift dir (rising > +0.1 H, falling < -0.1
H, flat between) and growth dir (growing > 1.2x, shrinking < 0.8x, steady
between) must agree; a disagreeing row is short/over per column and gets an
owner in the README (B1152 missile / B1153 retire / unattributed).

Heights are in the SIDE'S OWN model units (the oracle fixed_npc LANDING is
in target heights, 325px; the harness landing is in caster heights, 235px)
-- the table prints the units, and the comparison is on drift/growth
direction and relative magnitude, not on absolute px.
"""
import csv
import os
import re
import sys

def f(x):
    try:
        return float(str(x).replace('+', ''))
    except (ValueError, TypeError):
        return None

def parse(path):
    """anchor.py per-frame rows -> list of dicts (active frames only)."""
    if not os.path.exists(path):
        return None
    out = []
    hdr = None
    for line in open(path):
        line = line.rstrip('\n')
        if line.startswith('#') or not line.strip():
            continue
        if line.startswith('frame\t'):
            hdr = line.split('\t')
            continue
        if hdr is None:
            continue
        p = line.split('\t')
        if len(p) < 12:
            continue
        fr = f(p[0])
        if fr is None or not p[0].strip().isdigit():
            break  # summary lines (SOURCE onset ... / PEAK ...) follow
        active = p[-1].strip() == '*'
        if not active:
            continue
        out.append(dict(frame=int(fr), tb=f(p[1]), ta=f(p[2]), cov=f(p[3]),
                        cen_h=f(p[4]), cen_dx=f(p[5]), hp5=f(p[8]), hp95=f(p[9])))
    return out

def pstage(path, tcol):
    """parse + rebase 'tb' onto the stage's own time axis (SOURCE: t_bc,
    LANDING: t_act), so sig()/cell() (which use 'tb') are stage-correct."""
    rows = parse(path)
    if rows:
        for r in rows:
            r['tb'] = r[tcol]
    return rows


def sig(rows):
    """per-frame rows -> (tag, s): ('missing', None) / ('none', None) / ('ok', s)."""
    if rows is None:
        return 'missing', None
    if not rows:
        return 'none', None
    on = rows[0]
    pk = max(rows, key=lambda r: r['cov'])
    en = rows[-1]
    return 'ok', dict(on=on, pk=pk, en=en,
                drift_cen=pk['cen_h'] - on['cen_h'],
                drift_end=en['cen_h'] - on['cen_h'],
                growth_cov=(pk['cov'] / on['cov']) if on['cov'] else None,
                growth_h=pk['hp95'] - on['hp95'],
                dur=en['tb'] - on['tb'] if None not in (en['tb'], on['tb']) else None)

def dirdrift(d):
    if d is None:
        return '-'
    return 'rising' if d > 0.1 else ('falling' if d < -0.1 else 'flat')

def dirgrowth(g):
    if g is None:
        return '-'
    return 'growing' if g > 1.2 else ('shrinking' if g < 0.8 else 'steady')

def cell(tag, s, key):
    if tag != 'ok' or s is None:
        return '-'
    if key == 'onset':
        o = s['on']
        return f"f{o['frame']} {o['tb']:+.2f}"
    if key == 'peak':
        p = s['pk']
        return f"f{p['frame']} {p['tb']:+.2f} cov {p['cov']:.3f}"
    if key == 'drift':
        return f"{s['drift_cen']:+.2f} ({dirdrift(s['drift_cen'])})"
    if key == 'end drift':
        return f"{s['drift_end']:+.2f}"
    if key == 'growth':
        gc = s['growth_cov']
        gcs = f'{gc:.2f}x' if gc is not None else '-'
        return f"{gcs} ({dirgrowth(gc)}) h95 {s['growth_h']:+.2f}"
    if key == 'extent':
        return f"h95 {s['pk']['hp95']:.2f}"
    if key == 'dur':
        return f"{s['dur']:+.2f}s" if s['dur'] is not None else '-'
    return '-'

def main():
    runs = sys.argv[1] if len(sys.argv) > 1 else '/home/user/src/willeq-wt/kb-b1151-frames/refactor/B1151/runs'
    ora = sys.argv[2] if len(sys.argv) > 2 else '/home/user/src/willeq-wt/kb-b1151-frames/refactor/B1151/oracle_anchor'

    # record -> oracle dir (oracle_anchor.sh's rig.tsv)
    rig = {}
    for r in csv.DictReader(open(os.path.join(ora, 'rig.tsv')), delimiter='\t'):
        rig[int(r['record'])] = r['dir']

    # the 40 records' sweep runs (record, spell)
    import glob
    sweep = []
    for d in sorted(glob.glob(os.path.join(runs, 'b1151_r*'))):
        m = re.match(r'b1151_r(\d+)_(\d+)$', os.path.basename(d))
        if m:
            sweep.append((int(m.group(1)), int(m.group(2)), d))

    print('# motion signatures: per record, centroid height + lit extent over time (anchor.py per-frame rows).')
    print('# ONSET/PEAK/END are frame index + time (t_bc-relative for SOURCE, t_act-relative for LANDING).')
    print('# drift = centroid height PEAK - ONSET; end drift = END - ONSET; growth = cov(PEAK)/cov(ONSET) and')
    print('# h_p95(PEAK) - h_p95(ONSET); all in the SIDE\'S own model heights (oracle SOURCE/fixed_self in 209px')
    print('# caster units, oracle fixed_npc LANDING in 325px target units; harness in 235px caster units, b1118).')
    print('# Verdict: drift direction and growth direction vs the oracle; short/over per column when they differ.')
    print()
    for stage, tcol in (('SOURCE', 'tb'), ('LANDING', 'ta')):
        print(f'### {stage} -- the B1151 harness vs the B1130 oracle (per record)\n')
        print(f'| rec | oracle onset | oracle peak | oracle drift | oracle end drift | oracle growth | harness onset | harness peak | harness drift | harness end drift | harness growth | extent o/h | verdict |')
        print('|---|---|---|---|---|---|---|---|---|---|---|---|---|')
        for rec, sid, d in sweep:
            htag, hs = sig(pstage(os.path.join(d, f'anchor_{stage}.tsv'), tcol))
            od = rig.get(rec)
            otag, os_ = sig(pstage(os.path.join(ora, f'{od}_{stage}.tsv'), tcol)) if od else ('missing', None)
            if htag != 'ok' and otag != 'ok':
                v = 'no rows'
            elif htag != 'ok' or otag != 'ok':
                v = 'MISSING' + (f" (harness {htag}, oracle {otag})" if htag == 'ok' or otag == 'ok' else '')
            elif htag == 'none' or otag == 'none':
                v = 'none' if (htag == 'none' and otag == 'none') else ('harness-none' if htag == 'none' else 'oracle-none')
            else:
                assert hs is not None and os_ is not None  # htag/otag == 'ok'
                probs = []
                if dirdrift(hs['drift_cen']) != dirdrift(os_['drift_cen']):
                    probs.append('drift ' + ('short' if abs(hs['drift_cen']) < abs(os_['drift_cen']) else 'over'))
                g1, g2 = dirgrowth(hs['growth_cov']), dirgrowth(os_['growth_cov'])
                if g1 != g2:
                    probs.append('growth ' + ('short' if (hs['growth_cov'] or 0) < (os_['growth_cov'] or 0) else 'over'))
                if hs['pk']['hp95'] < os_['pk']['hp95'] * 0.7:
                    probs.append('extent short')
                elif hs['pk']['hp95'] > os_['pk']['hp95'] / 0.7:
                    probs.append('extent over')
                v = 'PASS' if not probs else ' / '.join(probs)
            print(f"| {rec} | {cell(otag, os_, 'onset')} | {cell(otag, os_, 'peak')} | {cell(otag, os_, 'drift')} | {cell(otag, os_, 'end drift')} | "
                  f"{cell(otag, os_, 'growth')} | {cell(htag, hs, 'onset')} | {cell(htag, hs, 'peak')} | {cell(htag, hs, 'drift')} | "
                  f"{cell(htag, hs, 'end drift')} | {cell(htag, hs, 'growth')} | {cell(otag, os_, 'extent')} / {cell(htag, hs, 'extent')} | {v} |")
        print()

if __name__ == '__main__':
    main()
