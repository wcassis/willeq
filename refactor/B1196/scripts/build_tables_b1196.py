#!/usr/bin/env python3
"""B1196 (#515): Rock 4D (lane 3, arm64 build, the ci-arm64 publish for tip
8c2bfe7c2) vs the ThinkPad SURFACELESS baseline for the SAME tip, the 17 gate
rows (tour-7 + b1130 r0-r7,r9,r10), same plans (byte-identical, verified),
same seed/sim-hz/camera/fps.

The card names the baseline as /mnt/scratch/gate-baseline/<sha>-sl (the
surfaceless baseline; the README there: "every card from now on judges its
AFTER against a surfaceless baseline (<sha>-sl)"). 8c2bfe7c2-sl did not exist
when this card started (the 8c2bfe7c2 dir predates the 2026-10-29 12:45Z
render-path change -- its b1130 rows are on the old i915 3840x2160 path), so
the 17 baseline rows were rendered on the ThinkPad by this card with the
canonical runner (/mnt/projects/fable/review/GATE-BASELINE/render_gate.sh,
CONC=2, B1130_DRM=1 default) into /mnt/scratch/gate-baseline/8c2bfe7c2-sl/.

Inputs (every number read from a file, nothing retyped):
  ROCK = /home/user/kb-evidence/kb-b1196-rock4d-render
         measure/<tag>/summary.tsv      (tour rows, B1139 harness_measure.py)
         measure/<tag>/b1130_rows.tsv   (b1130 rows, B1155 harness_measure_b1130.py)
         measure/<tag>/time.txt         (rock4d T0/T1/load + bash time user/sys)
         measure/<tag>/run.log          (rock4d EGL/driver/surface facts)
         logs/row_times.tsv             (rock4d frames/render_s)
  BASE = /mnt/scratch/gate-baseline/8c2bfe7c2-sl
         measure/<tag>/summary.tsv + b1130_rows.tsv + run.log, logs/row_times.tsv
  refs = the worktree at 8c2bfe7c2 (this table's repo):
         tests/spell_fx_population_oracle.h  (kTour: oracle land/end + roi windows)
         refactor/B1153/tables/gate_c.tsv    (b1130 oracle_* reference columns)
         /mnt/projects/fable/review/B1130/record_table.tsv (record -> spell)

Band/verdict rules: copied VERBATIM from refactor/B1178/scripts/gate_after.py
(as carried in refactor/B1188/scripts/build_tables_b1188.py band()/verdict()):
  source_cov / landing_cov: +-3 % of the oracle reference
  p90_src / p90_land:       +-15 % of the oracle reference
  landing_end_s / source_onset_s: +-0.10 s
  source_centroid / src_h_lo / src_h_hi: +-0.15
  tour land_cov: +-3 % of kTour oracle_land; tour end: +-0.10 s;
  tour roi_l/roi_s: the absolute kTour windows.

"Within band" for a host-pair delta: |rock - thinkpad| <= the column's band
width at the oracle (the gate's own tolerance for that column). For the tour
roi columns (absolute windows, no band width) the hosts are within band when
both land on the same side (same verdict).

Outputs (out dir, default refactor/B1196/tables in this repo):
  delta_tour.tsv  delta_b1130.tsv  counts.tsv  throughput.tsv  egl.tsv  verdict.txt
"""
import math, os, re, sys

REPO = os.environ.get('B1196_REPO', '/home/user/src/willeq-wt/kb-b1196-rock4d-render')
ROCK = os.path.expanduser('~/kb-evidence/kb-b1196-rock4d-render')
BASE = '/mnt/scratch/gate-baseline/8c2bfe7c2-sl'
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, 'refactor/B1196/tables')
os.makedirs(OUT, exist_ok=True)

ROWS = ['tour_93', 'tour_38', 'tour_54', 'tour_656', 'tour_41', 'tour_253', 'tour_458',
        'b1130_r0', 'b1130_r1', 'b1130_r2', 'b1130_r3', 'b1130_r4', 'b1130_r5',
        'b1130_r6', 'b1130_r7', 'b1130_r9', 'b1130_r10']

# ---------------- band rules: verbatim from refactor/B1178/scripts/gate_after.py
def band(metric, o):
    if metric in ('source_cov', 'landing_cov'): return 0.03 * o
    if metric in ('p90_src', 'p90_land'):       return 0.15 * o
    if metric in ('landing_end_s', 'source_onset_s'): return 0.10
    return 0.15

def verdict(m, o, h):
    import math
    if o is None or h is None or (isinstance(h, float) and math.isnan(h)): return 'n-a'
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
    import math
    if x is None or x == '':
        return ''
    if isinstance(x, float) and math.isnan(x):
        return 'nan'
    if isinstance(x, str):
        return x
    return f'{x:.{nd}f}'.rstrip('0').rstrip('.') if nd else str(int(x))

# ---------------- references (committed at 8c2bfe7c2, this tree)
def parse_ktour():
    txt = open(os.path.join(REPO, 'tests/spell_fx_population_oracle.h')).read()
    m = re.search(r'kTour\[\]\s*=\s*\{(.*?)\};', txt, re.S)
    if m is None:
        raise SystemExit('kTour not found in tests/spell_fx_population_oracle.h')
    out = {}
    for r in re.finditer(
            r'\{\s*(\d+),\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,'
            r'\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f\s*\}', m.group(1)):
        g = [float(x) for x in r.groups()[1:]]
        out[int(r.group(1))] = dict(oracle_land=g[0], oracle_end=g[1],
                                    roi_l_lo=g[4], roi_l_hi=g[5], roi_s_lo=g[6], roi_s_hi=g[7])
    return out

kTour = parse_ktour()
gate_c = {int(r['record']): r for r in readtsv(os.path.join(REPO, 'refactor/B1153/tables/gate_c.tsv'))}
rec_table = {int(r['record']): r for r in readtsv('/mnt/projects/fable/review/B1130/record_table.tsv')}
spell_of_record = {k: v['spell_id'] for k, v in rec_table.items()}

# ---------------- measured values, both hosts
def host_summary(root, tag):
    p = os.path.join(root, 'measure', tag, 'summary.tsv')
    if not os.path.exists(p): return None
    rows = readtsv(p)
    return rows[0] if rows else None

def host_b1130(root, tag):
    p = os.path.join(root, 'measure', tag, 'b1130_rows.tsv')
    if not os.path.exists(p): return None
    rows = readtsv(p)
    return rows[0] if rows else None

def delta_inband(d, bw, tv, rv, kind):
    """host-pair within-band decision for one cell."""
    import math
    if d is None or (isinstance(d, float) and math.isnan(d)):
        return 'n-a'
    if kind == 'window':
        return 'same-side' if (tv and rv and tv == rv) else 'DIFFERENT'
    if bw is None:
        return 'info'
    return 'yes' if abs(d) <= bw else 'no'

# ================= delta_tour.tsv
TOUR_COLS = [  # name, summary key, oracle field, kind (band | window | plain)
    ('land_cov', 'land_cov', 'oracle_land', 'band'),
    ('end',      'end',      'oracle_end',  'band'),
    ('roi_l',    'roi_l',    None,          'window'),
    ('roi_s',    'roi_s',    None,          'window'),
    ('src_cov',  'src_cov',  None,          'plain'),
]
TOUR_METRIC = {'land_cov': 'landing_cov', 'end': 'landing_end_s'}
lines = ['\t'.join(['spell', 'column', 'oracle', 'thinkpad', 'thinkpad_v', 'rock4d', 'rock4d_v',
                    'd_rock_minus_think', 'band_width', 'in_band'])]
tour_inband = {}
for tag in [t for t in ROWS if t.startswith('tour_')]:
    spell = int(tag.split('_')[1])
    k = kTour[spell]
    t = host_summary(BASE, tag)
    r = host_summary(ROCK, tag)
    for name, skey, ofield, kind in TOUR_COLS:
        o = k.get(ofield)
        if kind == 'window':
            lo, hi = k[f'roi_{"l" if name == "roi_l" else "s"}_lo'], k[f'roi_{"l" if name == "roi_l" else "s"}_hi']
            o_out = f'{lo}-{hi}'
        else:
            o = k.get(ofield)
            o_out = fmt(o)
        tv_ = None if t is None else f(t.get(skey))
        rv_ = None if r is None else f(r.get(skey))
        if kind == 'window':
            def wv(x):
                return 'n-a' if x is None else ('PASS' if lo <= x <= hi else ('short' if x < lo else 'over'))
            tv, rv = wv(tv_), wv(rv_)
            bw = None
        elif kind == 'plain':
            tv = rv = 'n-a'
            bw = None
        else:
            m = TOUR_METRIC[name]
            tv = verdict(m, o, tv_)
            rv = verdict(m, o, rv_)
            bw = band(m, o) if o is not None else None
        d = None if (tv_ is None or rv_ is None) else rv_ - tv_
        ib = delta_inband(d, bw, tv, rv, kind)
        if name in ('land_cov', 'end', 'roi_l', 'roi_s'):
            tour_inband.setdefault(name, []).append(ib)
        d_out = '' if (d is None or math.isnan(d)) else f'{d:+.3f}'
        lines.append('\t'.join([str(spell), name, o_out, fmt(tv_), tv, fmt(rv_), rv,
                                d_out, fmt(bw), ib]))
open(os.path.join(OUT, 'delta_tour.tsv'), 'w').write('\n'.join(lines) + '\n')

# ================= delta_b1130.tsv
METRICS = ['source_cov', 'landing_cov', 'landing_end_s', 'p90_src', 'p90_land',
           'source_onset_s', 'source_centroid', 'src_h_lo', 'src_h_hi']
lines = ['\t'.join(['record', 'kind', 'spell', 'column', 'oracle', 'thinkpad', 'thinkpad_v', 'rock4d',
                    'rock4d_v', 'd_rock_minus_think', 'band_width', 'in_band'])]
b1130_inband = {}
for tag in [t for t in ROWS if t.startswith('b1130_')]:
    rec = int(tag.split('_r')[1])
    gc = gate_c[rec]
    t = host_b1130(BASE, tag)
    r = host_b1130(ROCK, tag)
    for mname in METRICS:
        o = f(gc.get('oracle_' + mname))
        tv_ = None if t is None else f(t.get(mname))
        rv_ = None if r is None else f(r.get(mname))
        tv = verdict(mname, o, tv_)
        rv = verdict(mname, o, rv_)
        bw = band(mname, o) if o is not None else None
        d = None if (tv_ is None or rv_ is None) else rv_ - tv_
        ib = delta_inband(d, bw, tv, rv, 'band')
        b1130_inband.setdefault(mname, []).append(ib)
        d_out = '' if (d is None or math.isnan(d)) else f'{d:+.3f}'
        lines.append('\t'.join([str(rec), gc.get('kind', ''), spell_of_record.get(rec, ''), mname,
                                fmt(o), fmt(tv_), tv, fmt(rv_), rv, d_out, fmt(bw), ib]))
open(os.path.join(OUT, 'delta_b1130.tsv'), 'w').write('\n'.join(lines) + '\n')

# ================= counts.tsv
lines = ['table', 'column', 'rows', 'in_band_yes', 'in_band_same_side', 'in_band_out',
         'in_band_na',
         'thinkpad_PASS', 'thinkpad_short', 'thinkpad_over', 'thinkpad_n-a',
         'rock4d_PASS', 'rock4d_short', 'rock4d_over', 'rock4d_n-a']
def count_row(table, name, ibs, tcol, rcol, delta_cols):
    d = [l.split('\t') for l in open(os.path.join(OUT, f'delta_{table}.tsv')).read().splitlines()[1:]
         if l.split('\t')[delta_cols[0]] == name]
    tv = [l[tcol] for l in d]
    rv = [l[rcol] for l in d]
    yes = ibs.count('yes'); ss = ibs.count('same-side'); na = ibs.count('n-a')
    out = len(ibs) - yes - ss - na
    lines.append('\t'.join([table, name, str(len(ibs)), str(yes), str(ss), str(out), str(na),
                            str(tv.count('PASS')), str(tv.count('short')), str(tv.count('over')), str(tv.count('n-a')),
                            str(rv.count('PASS')), str(rv.count('short')), str(rv.count('over')), str(rv.count('n-a'))]))
for name in [c[0] for c in TOUR_COLS]:
    if name in tour_inband:
        count_row('tour', name, tour_inband[name], 4, 6, (1,))
for mname in METRICS:
    count_row('b1130', mname, b1130_inband[mname], 6, 8, (3,))
open(os.path.join(OUT, 'counts.tsv'), 'w').write('\n'.join(lines) + '\n')

# ================= throughput.tsv
def rowtimes(p):
    if not os.path.exists(p): return {}
    return {r['tag']: r for r in readtsv(p) if 'tag' in r}
tp_rt = rowtimes(os.path.join(BASE, 'logs/row_times.tsv'))
rk_rt = rowtimes(os.path.join(ROCK, 'logs/row_times.tsv'))

def bash_time_secs(v):
    v = v.strip()
    m = re.match(r'(\d+)m([\d.]+)s', v)
    if m: return int(m.group(1)) * 60 + float(m.group(2))
    m = re.match(r'([\d.]+)s', v)
    return float(m.group(1)) if m else None

def rock_cpu(tag):
    p = os.path.join(ROCK, 'measure', tag, 'time.txt')
    if not os.path.exists(p): return None, ''
    cpu, load = None, ''
    for ln in open(p):
        mm = re.match(r'\s*(user|sys)\s+([\dm\.:s]+)', ln)
        if mm:
            s = bash_time_secs(mm.group(2))
            if s is not None: cpu = (cpu or 0.0) + s
        if ln.startswith('LOAD='):
            load = ln.split('LOAD=')[1].split()[0]
    return cpu, load

lines = ['tag', 'host', 'frames', 'render_s', 'fps', 'ms_per_frame_wall', 'cpu_s',
         'ms_cpu_per_frame', 'load_before', 'note']
for tag in ROWS:
    for host, rt in (('thinkpad', tp_rt), ('rock4d', rk_rt)):
        r = rt.get(tag)
        if r is None:
            continue
        nfr = f(r.get('frames'))
        rend = f(r.get('render_s'))
        fps = None if (nfr is None or rend in (None, 0)) else nfr / rend
        mwf = None if (nfr is None or rend in (None, 0)) else 1000.0 * rend / nfr
        cpu, mcpf, load, note = None, None, '', ''
        if host == 'rock4d':
            cpu, load = rock_cpu(tag)
            mcpf = None if (cpu is None or not nfr) else 1000.0 * cpu / nfr
        else:
            note = 'no per-row CPU captured by the baseline runner (render_s is the ssh call wall)'
        lines.append('\t'.join([tag, host, fmt(nfr, 0), fmt(rend, 1), fmt(fps, 2), fmt(mwf, 1),
                                fmt(cpu, 2), fmt(mcpf, 1), load, note]))
open(os.path.join(OUT, 'throughput.tsv'), 'w').write('\n'.join(lines) + '\n')

# ================= egl.tsv
def eglfacts_from(path):
    out = {}
    if not os.path.exists(path): return out
    for ln in open(path, errors='replace'):
        if re.search(r'EGL(?: \(surfaceless\))?: Version', ln):
            out['egl'] = ln.strip()
        elif re.search(r'EGL(?: \(surfaceless\))?: Config', ln):
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
            sum(i in ('yes', 'same-side') for i in ibs), sum(i in ('no', 'DIFFERENT') for i in ibs),
            sum(i == 'n-a' for i in ibs))
t = all_ib([i for v in tour_inband.values() for i in v])
b = all_ib([i for v in b1130_inband.values() for i in v])
comparable = (t[0] == 'yes' and b[0] == 'yes')
with open(os.path.join(OUT, 'verdict.txt'), 'w') as fh:
    fh.write('B1196 verdict: Rock 4D (lane 3, ci-arm64 build of 8c2bfe7c2, KMS 1024x768 Panfrost '
             'path -- the arm64 device has no surfaceless fallback, see README) vs ThinkPad '
             'surfaceless NVIDIA 1280x800, same tip 8c2bfe7c2, 17 rows, CONC=3 (rock4d) / CONC=2 (thinkpad baseline)\n')
    fh.write(f"tour   columns within band: {'yes' if t[0] == 'yes' else 'NO'}  (in-band {t[1]}, out {t[2]}, n-a {t[3]})\n")
    fh.write(f"b1130  columns within band: {'yes' if b[0] == 'yes' else 'NO'}  (in-band {b[1]}, out {b[2]}, n-a {b[3]})\n")
    fh.write(f"VERDICT: {'COMPARABLE -- every measured column within its B1178 band' if comparable else 'NOT comparable -- see counts.tsv / delta_*.tsv for the out-of-band cells'}\n")
    for tname in ('tour', 'b1130'):
        for name, ibs in (tour_inband if tname == 'tour' else b1130_inband).items():
            if 'no' in ibs or 'DIFFERENT' in ibs:
                fh.write(f"  OUT {tname}/{name}: {ibs.count('no') + ibs.count('DIFFERENT')} of {len(ibs)} rows\n")
print('wrote', os.path.abspath(OUT))
print(open(os.path.join(OUT, 'verdict.txt')).read())
