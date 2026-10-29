#!/usr/bin/env python3
"""B1195: per-lane table + lane-to-lane band judgment for the b1195_rig93_253 leg.

Input: two leg measure roots, each the obs_measure.py output for one lane (the <cap>_<spell>_<rep>.txt
logs plus the fx_timing.py lit80.tsv in each cast's frame dir). Per cast the rows are:
  TIMING  source_onset_s (BC+), source_peak_s, source_end_s, landing_onset_s (Act+), landing_end_s,
          landing_dur_s, landing_peak_s
  PIXEL   source_cov (fx_timing peak lit/model^2), landing_cov (same), p90_src / p90_land
          (90th percentile of the lit80 series in the stage window, /model^2),
          roi_source (anchor.py SOURCE 'CAST' summary cov), roi_landing (anchor.py LANDING 'RUN' cov)

Band rule = refactor/B1178/scripts/gate_after.py band() applied lane-to-lane (the named metrics get
their own band; the ROI fractions are lit-fraction coverage and take the cov class):
  source_cov / landing_cov / roi_source / roi_landing : +-3 % relative
  p90_src / p90_land                                  : +-15 % relative
  source_onset_s / landing_onset_s / landing_end_s / landing_peak_s / source_peak_s :
      source_onset_s +-0.10 s; landing_end_s +-0.10 s; the other onsets/peaks +-0.15 (absolute, gate_after's 'rest')
  landing_dur_s, source_end_s                         : +-0.15 s absolute (gate_after's 'rest')
usage: lane_table.py <rootA> <labelA> <rootB> <labelB> <out.tsv>
"""
import sys, os, re, json
import numpy as np

def fnum(x):
    try:
        return float(x)
    except (ValueError, TypeError):
        return None

def parse_txt(path, mpx_hint):
    """One cast log (obs_measure.py .txt) -> row dict."""
    txt = open(path).read()
    head = txt.splitlines()[0]
    m = re.search(r'SOURCE onset f\d+ BC([+-]?\d+\.\d+)s end f\d+ BC([+-]?\d+\.\d+)s peak f\d+ BC([+-]?\d+\.\d+)s coverage (\d+\.\d+) model\^2', txt)
    m2 = re.search(r'LANDING onset f\d+ Act([+-]?\d+\.\d+)s end f\d+ Act([+-]?\d+\.\d+)s dur (\d+\.\d+)s peak f\d+ Act([+-]?\d+\.\d+)s coverage (\d+\.\d+) model\^2', txt)
    mp = re.search(r'model (\d+)px', txt)
    mpx = float(mp.group(1)) if mp else mpx_hint
    r = dict(mpx=mpx, head=head)
    if m:
        r['source_onset_s'], r['source_end_s'], r['source_peak_s'], r['source_cov'] = map(fnum, m.groups())
    lm = re.search(r'LANDING active run f\d+ Act[+-]?\d+\.\d+ \.\. f\d+ Act[+-]?\d+\.\d+; peak f\d+ Act[+-]?\d+\.\d+ cov (\d+\.\d+)', txt)
    if lm:
        r['landing_peak_cov_anchor'] = fnum(lm.group(1))
    for tag, key in (('CAST', 'roi_source'), ('RUN', 'roi_landing')):
        mm = re.search(rf'^{tag} f\d+-f\d+ \(\d+ frames\): cov (\d+\.\d+)', txt, re.M)
        if mm:
            r[key] = fnum(mm.group(1))
    if m2:
        r['landing_onset_s'], r['landing_end_s'], r['landing_dur_s'], r['landing_peak_s'], r['landing_cov'] = map(fnum, m2.groups())
    return r

def p90s(frame_dir, mpx):
    p = os.path.join(frame_dir, 'lit80.tsv')
    if not os.path.exists(p):
        return None, None
    rows = [l.split('\t') for l in open(p)][1:]
    tb = np.array([float(x[1]) for x in rows]); ta = np.array([float(x[2]) for x in rows])
    lit = np.array([float(x[3]) for x in rows])
    sw = (tb >= 0) & (ta < 0); lw = (ta >= 0)
    ps = float(np.percentile(lit[sw], 90)) / mpx**2 if sw.any() else None
    pl = float(np.percentile(lit[lw], 90)) / mpx**2 if lw.any() else None
    return ps, pl

def leg(root, mpx_hint):
    """root: the obs_measure.py out root for one lane (both spell runs share it)."""
    out = {}
    for fn in sorted(os.listdir(root)):
        if not re.match(r'.*_\d+_\d+\.txt$', fn):
            continue
        d = os.path.join(root, fn[:-4])
        r = parse_txt(d + '.txt', mpx_hint)
        ps, pl = p90s(d, r['mpx'])
        r['p90_src'], r['p90_land'] = ps, pl
        mm = re.match(r'(.+?)_(\d+)_(\d+)\.txt$', fn)
        out[(mm.group(2), int(mm.group(3)))] = r
    return out

def band(metric, ref):
    if metric in ('source_cov', 'landing_cov', 'roi_source', 'roi_landing'):
        return 0.03 * abs(ref)
    if metric in ('p90_src', 'p90_land'):
        return 0.15 * abs(ref)
    if metric in ('source_onset_s', 'landing_end_s'):
        return 0.10
    return 0.15

METRICS = ['source_onset_s', 'source_peak_s', 'source_end_s', 'source_cov', 'p90_src', 'roi_source',
           'landing_onset_s', 'landing_peak_s', 'landing_end_s', 'landing_dur_s', 'landing_cov',
           'p90_land', 'roi_landing']

a, la, b, lb, outp = sys.argv[1:6]
A, B = leg(a, 0), leg(b, 0)
lines = []
hdr = ['lane', 'spell', 'rep'] + [f'{la}_{m}' for m in METRICS] + [f'{lb}_{m}' for m in METRICS] + [f'delta_{m}' for m in METRICS] + [f'verdict_{m}' for m in METRICS]
lines.append('\t'.join(hdr))
for key in sorted(set(A) | set(B)):
    spell, rep = key
    ra, rb = A.get(key), B.get(key)
    row = [f'{la} vs {lb}', str(spell), str(rep)]
    for m in METRICS:
        va = ra.get(m) if ra else None
        row.append(f'{va:.4f}' if va is not None else '-')
    for m in METRICS:
        vb = rb.get(m) if rb else None
        row.append(f'{vb:.4f}' if vb is not None else '-')
    for m in METRICS:
        va = ra.get(m) if ra else None
        vb = rb.get(m) if rb else None
        row.append(f'{(vb - va):+.4f}' if (va is not None and vb is not None) else '-')
    for m in METRICS:
        va = ra.get(m) if ra else None
        vb = rb.get(m) if rb else None
        if va is None or vb is None:
            row.append('n-a')
        else:
            # judge B vs A with A as the reference row (lane 5 first in the command line)
            row.append('in' if abs(vb - va) <= band(m, va) else ('short' if vb < va - band(m, va) else 'over'))
    lines.append('\t'.join(row))
open(outp, 'w').write('\n'.join(lines) + '\n')
print(f'wrote {outp}: {len(lines) - 1} cast rows x {len(METRICS)} metrics')
