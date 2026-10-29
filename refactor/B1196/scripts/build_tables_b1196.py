#!/usr/bin/env python3
"""B1196 (#515): Rock 4D (lane 3) vs the ThinkPad surfaceless baseline, the 17
gate rows (tour-7 + b1130 r0-r7,r9,r10), same fable tip (8c2bfe7c2), same
plans/seed/sim-hz/camera (render_gate_rock4d.sh; plans verified byte-identical
to the baseline's).

Inputs (every number read from a file, nothing retyped):
  BASE = /mnt/scratch/gate-baseline/8c2bfe7c-sl
         tables/tour.tsv + tables/b1130.tsv   (the ThinkPad surfaceless values
                 "mine_*" and their band verdicts "v_*", oracle refs "oracle_*")
         logs/row_times.tsv                   (ThinkPad frames/render_s, CONC=3)
         measure/<tag>/run.log                (ThinkPad EGL/driver/surface facts)
  ROCK = /home/user/kb-evidence/kb-b1196-rock4d-render
         measure/<tag>/summary.tsv            (tour rows, harness_measure.py)
         measure/<tag>/b1130_rows.tsv         (b1130 rows, harness_measure_b1130.py)
         measure/<tag>/time.txt               (rock4d T0/T1/load + bash time user/sys)
         measure/<tag>/run.log                (rock4d EGL/driver/surface facts)
         logs/row_times.tsv                   (rock4d frames/render_s, CONC=3)

Band/verdict rules: copied VERBATIM from /mnt/scratch/gate-baseline/8c2bfe7c-sl/
tables/judge.py (itself verbatim from refactor/B1178/scripts/gate_after.py):
  cov +-3 % of oracle, p90 +-15 %, times +-0.10 s, other b1130 metrics +-0.15;
  tour roi_l/roi_s absolute windows (the baseline's band_roi_* columns).

"Within band" for a host-pair delta: |rock - thinkpad| <= the column's band
width at the oracle (the gate's own tolerance for that column). For the tour
roi columns (absolute windows, no band width) the hosts are within band when
both land on the same side (same verdict).

Outputs (out dir, default the repo's refactor/B1196/tables):
  delta_tour.tsv  delta_b1130.tsv  counts.tsv  throughput.tsv  egl.tsv  verdict.txt
"""
import os, re, sys

BASE = '/mnt/scratch/gate-baseline/8c2bfe7c-sl'
ROCK = os.path.expanduser('~/kb-evidence/kb-b1196-rock4d-render')
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'tables')
os.makedirs(OUT, exist_ok=True)

ROWS = ['tour_93', 'tour_38', 'tour_54', 'tour_656', 'tour_41', 'tour_253', 'tour_458',
        'b1130_r0', 'b1130_r1', 'b1130_r2', 'b1130_r3', 'b1130_r4', 'b1130_r5',
        'b1130_r6', 'b1130_r7', 'b1130_r9', 'b1130_r10']

# ---------------- band rules: verbatim from the baseline's judge.py
def band(metric, o):
    if metric in ('source_cov', 'landing_cov'): return 0.03 * o
    if metric in ('p90_src', 'p90_land'):       return 0.15 * o
    if metric in ('landing_end_s', 'source_onset_s'): return 0.10
    return 0.15

def verdict(m, o, h):
    if o is None or h is None: return 'n-a'
    b = band(m, o)
    if abs(h - o) <= b: return 'PASS'
    return 'short' if h < o - b else 'over'

def f(x):
    x = (x or '').strip()
    if x in ('', '-', 'n/a', 'n-a', 'nan'):
        return None
    try:
        return float(x)
    except ValueError:
        return None

def readtsv(p):
    with open(p) as fh:
        lines = [l.rstrip('\n') for l in fh if l.strip() and not l.lstrip().startswith('#')]
    hdr = lines[0].split('\t')
    return [dict(zip(hdr, l.split('\t'))) for l in lines[1:]]

def fmt(x, nd=3):
    if x is None or x == '':
        return ''
    return f'{x:.{nd}f}'.rstrip('0').rstrip('.') if nd else str(int(x))

# ---------------- baseline (ThinkPad surfaceless) tables
bt = {r['spell']: r for r in readtsv(os.path.join(BASE, 'tables/tour.tsv'))}
bb = {int(r['record']): r for r in readtsv(os.path.join(BASE, 'tables/b1130.tsv'))}

def rock_summary(tag):
    p = os.path.join(ROCK, 'measure', tag, 'summary.tsv')
    if not os.path.exists(p): return None
    rows = readtsv(p)
    return rows[0] if rows else None

def rock_b1130(tag):
    p = os.path.join(ROCK, 'measure', tag, 'b1130_rows.tsv')
    if not os.path.exists(p): return None
    rows = readtsv(p)
    return rows[0] if rows else None

def w(x):  # band width for a delta test
    return x

# ================= delta_tour.tsv
TOUR_COLS = [  # name, baseline mine-col, baseline v-col, rock summary key, metric (band key), kind
    ('land_cov', 'mine_land_cov', 'v_land_cov', 'land_cov', 'landing_cov', 'band'),
    ('end',      'mine_end',      'v_end',      'end',      'landing_end_s', 'band'),
    ('roi_l',    'mine_roi_l',    'v_roi_l',    'roi_l',    None, 'window'),
    ('roi_s',    'mine_roi_s',    'v_roi_s',    'roi_s',    None, 'window'),
    ('src_cov',  'mine_src_cov',  None,         'src_cov',  None, 'plain'),
]
lines = ['\t'.join(['spell', 'column', 'oracle', 'thinkpad', 'thinkpad_v', 'rock4d', 'rock4d_v',
                    'd_rock_minus_think', 'band_width', 'in_band'])]
tour_inband = {}
for tag in [t for t in ROWS if t.startswith('tour_')]:
    spell = tag.split('_')[1]
    br = bt[spell]
    rk = rock_summary(tag)
    for name, bmc, bvc, rkey, metric, kind in TOUR_COLS:
        if kind == 'window':
            o = br.get(f'band_{name}', '')
        else:
            o = f(br.get(f'oracle_{name}', ''))
        t = f(br.get(bmc))
        tv = br.get(bvc, '') if bvc else ''
        r = None if rk is None else f(rk.get(rkey))
        if kind == 'window':
            m = re.match(r'([\d.]+)-([\d.]+)', br.get(f'band_{name}', ''))
            rv = 'n-a'
            if m is not None and r is not None:
                lo, hi = float(m.group(1)), float(m.group(2))
                rv = 'PASS' if lo <= r <= hi else ('short' if r < lo else 'over')
            d = None if (t is None or r is None) else r - t
            ib = 'same-side' if (tv and rv and (tv == rv)) else ('n-a' if (t is None or r is None) else 'DIFFERENT')
            bw = ''
        elif kind == 'plain':
            rv = 'n-a'
            d = None if (t is None or r is None) else r - t
            bw = None
            ib = 'info'
        else:
            rv = verdict(metric, o, r) if metric else 'n-a'
            d = None if (t is None or r is None) else r - t
            bw = band(metric, o) if (metric and o is not None) else None
            ib = ('yes' if abs(d) <= bw else 'no') if (d is not None and bw is not None) else ('n-a' if (t is None or r is None) else 'no')
        if name in ('land_cov', 'end', 'roi_l', 'roi_s'):
            tour_inband.setdefault(name, []).append(ib)
        o_out = o if isinstance(o, str) else fmt(o)
        lines.append('\t'.join([spell, name, o_out, fmt(t), tv, fmt(r), rv,
                                f'{d:+.3f}' if d is not None else '', fmt(bw), ib]))
open(os.path.join(OUT, 'delta_tour.tsv'), 'w').write('\n'.join(lines) + '\n')

# ================= delta_b1130.tsv
METRICS = ['source_cov', 'landing_cov', 'landing_end_s', 'p90_src', 'p90_land',
           'source_onset_s', 'source_centroid', 'src_h_lo', 'src_h_hi']
lines = ['\t'.join(['record', 'kind', 'spell', 'column', 'oracle', 'thinkpad', 'thinkpad_v', 'rock4d',
                    'rock4d_v', 'd_rock_minus_think', 'band_width', 'in_band'])]
b1130_inband = {}
for tag in [t for t in ROWS if t.startswith('b1130_')]:
    rec = int(tag.split('_r')[1])
    br = bb[rec]
    rk = rock_b1130(tag)
    for mname in METRICS:
        o = f(br.get('oracle_' + mname))
        t = f(br.get('mine_' + mname))
        tv = br.get('v_' + mname, '')
        r = None if rk is None else f(rk.get(mname))
        rv = verdict(mname, o, r)
        d = None if (t is None or r is None) else r - t
        bw = band(mname, o)
        ib = ('yes' if abs(d) <= bw else 'no') if (d is not None and bw is not None) else ('n-a' if (t is None or r is None) else 'no')
        b1130_inband.setdefault(mname, []).append(ib)
        lines.append('\t'.join([str(rec), br.get('kind', ''), br.get('spell', ''), mname,
                                fmt(o), fmt(t), tv, fmt(r), rv,
                                f'{d:+.3f}' if d is not None else '', fmt(bw), ib]))
open(os.path.join(OUT, 'delta_b1130.tsv'), 'w').write('\n'.join(lines) + '\n')

# ================= counts.tsv
lines = ['table', 'column', 'rows', 'in_band_yes', 'in_band_no', 'in_band_na',
         'rock4d_PASS', 'rock4d_short', 'rock4d_over', 'rock4d_n-a']
def count_row(table, name, ibs):
    yes = ibs.count('yes'); no = ibs.count('no'); na = len(ibs) - yes - no
    # rock4d verdicts: re-read from the delta table's rock4d_v
    d = [l for l in open(os.path.join(OUT, f'delta_{table}.tsv')).read().splitlines()[1:]
         if l.split('\t')[1] == name]
    v = [l.split('\t')[6] for l in d]
    lines.append('\t'.join([table, name, str(len(ibs)), str(yes), str(no), str(na),
                            str(v.count('PASS')), str(v.count('short')), str(v.count('over')), str(v.count('n-a'))]))
for name in [c[0] for c in TOUR_COLS]:
    if name in tour_inband:
        count_row('tour', name, tour_inband[name])
for mname in METRICS:
    count_row('b1130', mname, b1130_inband[mname])
open(os.path.join(OUT, 'counts.tsv'), 'w').write('\n'.join(lines) + '\n')

# ================= throughput.tsv
def rowtimes(p):
    return {r['tag']: r for r in readtsv(p) if 'tag' in r}
tp_rt = rowtimes(os.path.join(BASE, 'logs/row_times.tsv'))
rk_rt = rowtimes(os.path.join(ROCK, 'logs/row_times.tsv'))

def cpus(t):
    # bash `time` output: user/sys as MmSs or Ss
    def sec(v):
        m = re.match(r'(\d+)m([\d.]+)s', v)
        if m: return int(m.group(1)) * 60 + float(m.group(2))
        m = re.match(r'([\d.]+)s', v)
        return float(m.group(1)) if m else None
    out = {}
    for ln in open(os.path.join(ROCK, 'measure', t, 'time.txt')):
        mm = re.match(r'\s*(real|user|sys)\s+([\dm\.:s]+)', ln)
        if mm: out[mm.group(1)] = sec(mm.group(2))
    return out

lines = ['tag', 'host', 'frames', 'render_s', 'fps', 'ms_per_frame_wall', 'cpu_s', 'ms_cpu_per_frame', 'load_before']
for tag in ROWS:
    for host, rt in (('thinkpad', tp_rt), ('rock4d', rk_rt)):
        r = rt.get(tag)
        if r is None:
            continue
        nfr = f(r.get('frames'))
        rend = f(r.get('render_s'))
        fps = None if (nfr is None or rend in (None, 0)) else nfr / rend
        mwf = None if (nfr is None or rend in (None, 0)) else 1000.0 * rend / nfr
        cpu = None; mcpf = None; load = ''
        if host == 'rock4d':
            c = cpus(tag)
            u, s = c.get('user'), c.get('sys')
            if u is not None and s is not None:
                cpu = u + s
                mcpf = None if nfr in (None, 0) else 1000.0 * cpu / nfr
            try:
                load = [l for l in open(os.path.join(ROCK, 'measure', tag, 'time.txt'))
                        if l.startswith('LOAD=')][0].split('LOAD=')[1].split()[0]
            except Exception:
                pass
        lines.append('\t'.join([tag, host, fmt(nfr, 0), fmt(rend, 1), fmt(fps, 2), fmt(mwf, 1),
                                fmt(cpu, 2), fmt(mcpf, 1), load]))
open(os.path.join(OUT, 'throughput.tsv'), 'w').write('\n'.join(lines) + '\n')

# ================= egl.tsv
def eglfacts_from(path):
    out = {}
    if not os.path.exists(path): return out
    for ln in open(path, errors='replace'):
        if 'EGL (surfaceless): Version' in ln:
            out['egl'] = ln.strip()
        elif 'EGL (surfaceless): Config' in ln:
            out['config'] = ln.strip()
        elif ln.startswith('GLES2 Driver:'):
            out['driver'] = ln.strip()
        elif '[fx-harness]' in ln and 'surface' in ln and 'for a' in ln:
            out['surface'] = ln.strip()
    return out
lines = ['tag', 'host', 'egl', 'config', 'driver', 'surface']
seen = {}
for tag in ROWS:
    for host, root in (('thinkpad', BASE), ('rock4d', ROCK)):
        fac = eglfacts_from(os.path.join(root, 'measure', tag, 'run.log'))
        key = (fac.get('egl', ''), fac.get('config', ''), fac.get('driver', ''), fac.get('surface', ''))
        dup = '' if key not in seen else f'(== {seen[key]})'
        seen[key] = tag
        lines.append('\t'.join([tag, host, fac.get('egl', ''), fac.get('config', ''),
                                fac.get('driver', ''), fac.get('surface', '') + dup]))
open(os.path.join(OUT, 'egl.tsv'), 'w').write('\n'.join(lines) + '\n')

# ================= verdict.txt
def all_ib(ibs):
    ok = ('yes', 'n-a', 'same-side')
    return ('yes' if all(i in ok for i in ibs) else 'NO',
            sum(i == 'yes' or i == 'same-side' for i in ibs), sum(i == 'no' or i == 'DIFFERENT' for i in ibs), sum(i == 'n-a' for i in ibs))
t = all_ib([i for v in tour_inband.values() for i in v])
b = all_ib([i for v in b1130_inband.values() for i in v])
comparable = (t[0] == 'yes' and b[0] == 'yes')
with open(os.path.join(OUT, 'verdict.txt'), 'w') as fh:
    fh.write('B1196 verdict: Rock 4D (lane 3) vs ThinkPad surfaceless, tip 8c2bfe7c2, 17 rows, CONC=3 both hosts\n')
    fh.write(f"tour   columns within band: {'yes' if comparable else 'NO'}  (in-band {t[1]}, out {t[2]}, n-a {t[3]})\n")
    fh.write(f"b1130  columns within band: {'yes' if comparable else 'NO'}  (in-band {b[1]}, out {b[2]}, n-a {b[3]})\n")
    fh.write(f"VERDICT: {'COMPARABLE -- every measured column within its B1178 band' if comparable else 'NOT comparable -- see counts.tsv / delta_*.tsv for the out-of-band cells'}\n")
    for tname in ('tour', 'b1130'):
        for name, ibs in (tour_inband if tname == 'tour' else b1130_inband).items():
            if 'no' in ibs:
                fh.write(f"  OUT {tname}/{name}: {ibs.count('no')} of {len(ibs)} rows\n")
print('wrote', os.path.abspath(OUT))
print(open(os.path.join(OUT, 'verdict.txt')).read())
