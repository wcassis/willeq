#!/usr/bin/env python3
"""B1178 (#497): re-measure every B1130 fixed_npc capture's target anchor (tmpx/tfeet)
by the B1166 method, then regenerate record_table.tsv's oracle anatomy columns at the
new anchor. One script; every number is a measured value from a named instrument.

Inputs (evidence on disk):
  <b1130>/stills/<cap>/*_tgt_npc_settle.png  -- with-target settle still (B1166 difference)
  <b1130>/stills/16_empty_r51cam.png         -- target-free still at the same fixed camera
  <b1130>/geom/<cap>.json                    -- the committed geom (crop/feet/mpx/excludes)
  <b1130_rex>/<cap>_fixed_npc/               -- frame PNGs + frames.tsv + lit80.tsv, re-extracted
                                               from the capture video by fx_regen.py's recipe
                                               (fx_measure.py + fx_timing.py); byte-identical to
                                               the committed frames.tsv (validated 28/28)
  record_table.tsv                            -- the committed table (old columns copied through)

Stage 1 -- the anchor (B1166 method, refactor101166/scripts/oracle_target_box.py):
  difference of the with-target still against the target-free still, the green selection
  ring removed by colour, the connected component through the torso seed (940,450), a row
  is a figure row when it holds >= 3 component pixels (B1164 minpx 3), feet = the last
  figure row. Threshold sweep 40/30/25/20/15/10; THR=20 adopted (the lowest threshold whose
  component does not leak into the open ground). The feet COLUMN = round-half-up of the mean
  of the centroid column over thresholds 20/30/40 (B1166: r51 reads 938.9-940.3; B1130's 940
  is kept -> 940). All fixed_npc captures share one fixed camera + one target position
  (identical crop/feet/tfeet in every geom), so the one empty still is the reference for all.

Stage 2 -- the anatomy (B1130/B1122 instruments, unchanged, on the re-extracted frames):
  anchor.py LANDING at the NEW anchor (target heights) -> target_centroid
  landing_onset_abs.py (caster model^2) -> landing_cov + lon/lend (anchor-free; identical to committed)
  fx_sprite_roi.py LANDING, ROI rebuilt from the NEW anchor (summarize.py's roi_t) -> sprite_p90_land
  overlap.py (caster model^2) -> overlap_cov (anchor-free)
  demand_landing = round(landing_cov / (pi/4 * p90_land^2)); demand_overlap = round(overlap_cov / same)
  (summarize.py's exact formulas, lines 81-89)

usage: remeasure_b1130.py <b1130_dir> <b1130_rex_dir> <record_table.tsv> <out_dir>
Outputs (in <out_dir>): b1130_anchor_remeasure.tsv, b1130_anatomy_old_new.tsv,
  record_table_remeasured.tsv, remeasure_log.txt (every instrument call + stdout).
Only the anchor-dependent oracle anatomy columns move; every other column is copied through.
No golden/manifest/baseline/preset file is touched.
"""
import sys, os, json, re, subprocess, math

B1130, REX, RTAB, OUT = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
WT = os.environ.get('WILLEQ_WT', '/home/user/src/willeq-wt/kb-b1178-oracle-refs')
S1166 = f'{WT}/refactor/B1166/scripts'
S1130 = '/mnt/projects/fable/review/B1130/scripts'
S1122 = '/mnt/projects/fable/review/B1122/scripts'
EMPTY = f'{B1130}/stills/16_empty_r51cam.png'
THR_SWOOP = [40, 30, 25, 20, 15, 10]
ADOPT_THR = 20
os.makedirs(OUT, exist_ok=True)
LOG = open(f'{OUT}/remeasure_log.txt', 'w')


def log(*a):
    print(*a); print(*a, file=LOG); LOG.flush()


def run(cmd, env=None):
    e = dict(os.environ)
    if env: e.update(env)
    r = subprocess.run([str(x) for x in cmd], capture_output=True, text=True, env=e)
    out = r.stdout.strip()
    log('$ ' + ' '.join(str(x) for x in cmd) + ('  [env ' + str(env) + ']' if env else ''))
    if r.returncode: log('  rc=' + str(r.returncode) + ' stderr=' + r.stderr.strip()[:400])
    for l in out.splitlines(): log('  ' + l)
    return out


def grab(t, pat):
    m = re.search(pat, t)
    if not m: return None
    return float(m.group(1)) if len(m.groups()) == 1 else tuple(float(g) for g in m.groups())


def rhu(x):
    return int(math.floor(x + 0.5))


def fixed_npc_captures():
    out = []
    for f in sorted(os.listdir(f'{B1130}/geom')):
        if not f.endswith('.json'): continue
        g = json.load(open(f'{B1130}/geom/' + f))
        for step, v in g.items():
            if v.get('kind') == 'fixed_npc':
                out.append((f[:-5], step, v))
    return out


def measure_anchor(cap, with_still):
    sweep = []
    for thr in THR_SWOOP:
        mask = f'{OUT}/mask_{cap}_thr{thr}.png'
        o = run(['python3', f'{S1166}/oracle_target_box.py', with_still, EMPTY, mask, 940, 450],
                env={'THR': str(thr)})
        rows = grab(o, r'minpx 3: rows (\d+)\.\.(\d+) height (\d+) px; cols (\d+)\.\.(\d+); centroid column ([\d.]+)')
        feetc = grab(o, r'feet rows \d+\.\.\d+: column centroid ([\d.]+)')
        comp = grab(o, r'component at seed \(\d+,\d+\): (\d+) px')
        sweep.append((thr, rows, feetc, comp))
    row = None
    for thr, rows, feetc, comp in sweep:
        if thr == ADOPT_THR and rows:
            row = (rows, feetc, comp); break
    if not row:
        for thr, rows, feetc, comp in sweep:
            if rows:
                row = (rows, feetc, comp); log(f'  !! {cap}: THR=20 empty, adopting THR={thr}'); break
    if not row:
        log(f'  !! {cap}: no component at any threshold'); return None, sweep
    (top, bot, height, c0, c1, centroid), feetc, comp = row
    cols = [s[1][5] for s in sweep if s[0] in (20, 30, 40) and s[1]]
    acol = rhu(sum(cols) / len(cols)) if cols else rhu(centroid)
    return {'top': int(top), 'bot': int(bot), 'tmpx': int(height), 'col': int(c0), 'colmax': int(c1),
            'centroid': centroid, 'feet_col': feetc, 'comp_px': int(comp), 'anchor_col': acol}, sweep


def roi_from_anchor(gv, tmpx, tfeet):
    cx, cy = map(int, gv['crop'].split(',')[:2])
    tfx, tfy = map(float, tfeet.split(',')); tm = float(tmpx)
    return f"{max(0, int(tfx - 1.5 * tm - cx))},{int(tfx + 1.5 * tm - cx)},{max(0, int(tfy - 2.0 * tm - cy))},{int(tfy + 0.1 * tm - cy)}"


def area(d):
    return math.pi * (d / 2) ** 2 if d else None


def anatomy(cap, gv, tmpx, tfeet):
    md = f'{REX}/{cap}_fixed_npc'
    if not (os.path.exists(md + '/frames.tsv') and os.path.exists(md + '/lit80.tsv')):
        log(f'  {cap}: re-extracted dir incomplete -- anatomy skipped'); return None
    mpx = float(gv['mpx'])
    exc = ['--exclude', '600,40,170,75', '--exclude', '0,0,200,800'] + \
          [x for e in gv.get('exclude', []) for x in ('--exclude', e)]
    hw = ['--halfw', str(gv.get('thalfw', gv.get('halfw')))]
    # 1. anchor.py LANDING at the NEW anchor -> target_centroid
    o = run(['python3', f'{S1130}/anchor.py', md, tfeet, str(int(tmpx)), 'LANDING'] + exc + hw)
    body = o.split('LANDING feet')[-1] if 'LANDING feet' in o else o
    lrun = re.search(r'^RUN .*$', body, re.M)
    if lrun:
        lr = lrun.group(0)
        cen = re.search(r'cen_h (\d+\.?\d*)', lr)
        p595 = re.search(r'h_p5\.\.p95 (\d+\.?\d*)\.\.(\d+\.?\d*)', lr)
        tcen = (f"cen_h {cen.group(1)} h {p595.group(1)}..{p595.group(2)} (target heights)"
                if (cen and p595) else '-')
    else:
        tcen = '-'
    # 2. landing_onset_abs.py -> lon/lend/lcov (anchor-free)
    o = run(['python3', f'{S1122}/landing_onset_abs.py', md, str(mpx)])
    rise = re.search(r'RISE onset f\d+ Act([+-][\d.]+)s end f\d+ Act([+-][\d.]+)s peak f\d+ Act[+-][\d.]+s coverage ([\d.]+)', o)
    lon, lend, lcov = (float(rise.group(1)), float(rise.group(2)), float(rise.group(3))) if rise else (None, None, None)
    # 3. fx_sprite_roi.py LANDING, ROI from the NEW anchor -> sprite_p90_land
    pl = None
    if lon is not None:
        roi_t = roi_from_anchor(gv, tmpx, tfeet)
        o = run(['python3', f'{S1122}/fx_sprite_roi.py', md, str(mpx), 'LANDING', str(lon), str(lend)], env={'ROI': roi_t})
        pl = grab(o, r'p90 ([\d.]+)')
    # 4. overlap.py -> overlap_cov (anchor-free)
    o = run(['python3', f'{S1130}/overlap.py', md, str(mpx)])
    ovc = grab(o, r'overlap_cov ([\d.]+)')
    dl = round(lcov / area(pl)) if lcov and pl else None
    ov = round(ovc / area(pl)) if ovc and pl else None
    return {'tcen': tcen, 'lcov': lcov, 'lon': lon, 'lend': lend, 'p90': pl, 'dl': dl, 'ovc': ovc, 'ov': ov, 'roi_t': roi_t if lon is not None else '-'}


def main():
    rt = [l.rstrip('\n').split('\t') for l in open(RTAB)]
    h = rt[0]; idx = {c: i for i, c in enumerate(h)}
    caps = fixed_npc_captures()
    log(f'fixed_npc captures: {len(caps)}')
    anchor_rows, anat_rows, newcols = [], [], {}
    for cap, step, gv in caps:
        log(f'=== {cap} ({step}) old tmpx={gv.get("tmpx")} tfeet={gv.get("tfeet")}')
        tgt = sorted(f for f in os.listdir(f'{B1130}/stills/{cap}') if f.endswith('_tgt_npc_settle.png'))
        if not tgt:
            log(f'  !! no tgt_npc_settle still for {cap}'); continue
        anch, sweep = measure_anchor(cap, f'{B1130}/stills/{cap}/{tgt[0]}')
        if not anch: continue
        flag = '' if (850 <= anch['col'] <= 1000 and 300 <= anch['top'] <= 345 and 600 <= anch['bot'] <= 650) else '  << CHECK (camera differs?)'
        log(f'  NEW anchor: tmpx={anch["tmpx"]} tfeet=({anch["anchor_col"]},{anch["bot"]}){flag}')
        for thr, rows, feetc, comp in sweep:
            if rows:
                log(f'    sweep thr{thr}: rows {int(rows[0])}..{int(rows[1])} h{int(rows[2])} cols {int(rows[3])}..{int(rows[4])} centroid {rows[5]} feetcol {feetc} comp {int(comp)}px')
            else:
                log(f'    sweep thr{thr}: no component')
        tfeet_new = f'{anch["anchor_col"]},{anch["bot"]}'
        a = anatomy(cap, gv, anch['tmpx'], tfeet_new)
        anchor_rows.append([cap, step, gv.get('tmpx'), gv.get('tfeet'), anch['tmpx'], tfeet_new,
                            ' | '.join(f'thr{t}:{(f"rows {int(r[0])}..{int(r[1])} h{int(r[2])}" if r else "none")}' for t, r, f, c in sweep)])
        if a:
            row = next((r for r in rt[1:] if r[idx['capture']] == cap), None)
            if row is None:
                log(f'  !! {cap}: not a primary capture in record_table'); continue
            old = {c: row[idx[c]] for c in ('target_centroid', 'sprite_p90_land', 'demand_landing', 'demand_overlap', 'landing_cov')}
            new = {'target_centroid': a['tcen'],
                   'sprite_p90_land': (f"{a['p90']:.3f}" if a['p90'] is not None else '-'),
                   'demand_landing': (str(a['dl']) if a['dl'] is not None else '-'),
                   'demand_overlap': (f"{a['ov']} (cov {a['ovc']:.3f})" if a['ov'] is not None else '-'),
                   'landing_cov': (f"{a['lcov']:.3f}" if a['lcov'] is not None else '-')}
            newcols[id(row)] = new
            anat_rows.append([cap, row[idx['record']], row[idx['spell_id']],
                              old['target_centroid'], new['target_centroid'],
                              old['sprite_p90_land'], new['sprite_p90_land'],
                              old['demand_landing'], new['demand_landing'],
                              old['demand_overlap'], new['demand_overlap']])
            log(f'  record {row[idx["record"]]}: tcen {old["target_centroid"]} -> {new["target_centroid"]}; p90 {old["sprite_p90_land"]} -> {new["sprite_p90_land"]}; dl {old["demand_landing"]} -> {new["demand_landing"]}')
    with open(f'{OUT}/b1130_anchor_remeasure.tsv', 'w') as f:
        f.write('capture\tstep\told_tmpx\told_tfeet\tnew_tmpx\tnew_tfeet\tthreshold_sweep\n')
        for r in anchor_rows: f.write('\t'.join(map(str, r)) + '\n')
    with open(f'{OUT}/b1130_anatomy_old_new.tsv', 'w') as f:
        f.write('capture\trecord\tspell\told_target_centroid\tnew_target_centroid\told_p90_land\tnew_p90_land\told_demand_landing\tnew_demand_landing\told_demand_overlap\tnew_demand_overlap\n')
        for r in anat_rows: f.write('\t'.join(map(str, r)) + '\n')
    with open(f'{OUT}/record_table_remeasured.tsv', 'w') as f:
        f.write('\t'.join(h) + '\n')
        for r in rt[1:]:
            if id(r) in newcols:
                for c, v in newcols[id(r)].items(): r[idx[c]] = v
            f.write('\t'.join(map(str, r)) + '\n')
    log(f'wrote {len(anchor_rows)} anchor rows, {len(anat_rows)} anatomy rows, {len(newcols)} record_table rows updated')


main()
