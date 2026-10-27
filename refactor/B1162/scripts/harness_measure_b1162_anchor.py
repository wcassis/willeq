#!/usr/bin/env python3
"""B1162 fix leg (MR !316 review note 7739, finding 1): re-measure an action-only FX-harness run
on the b1130 / b1130-self preset at the anchor the harness RENDERED, next to the preset anchor
that harness_measure_b1130_action.py used, with the SAME unchanged B1132 instruments
(fx_measure.py, landing_onset_abs.py, anchor.py; review/B1132/scripts/).

usage: harness_measure_b1162_anchor.py <frames_dir> <out_root> <fixed_npc|fixed_self> [--precast N]
                                        [--anchor x,y,px]   (override the derived rendered anchor)

Two anchors, both printed and both measured:
  PRESET   = harness_measure_b1130_action.py's GEOM (fixed_npc: crop 150,60,990,540 = y 60..600,
             tfeet/tpx = kB1130TargetAnchor, the ORACLE's measured target box, read from
             tools/model_viewer/fx_target_anchor.h since B1166 (#485); B1162 ran it at B1130's
             940,652 / 325, which FX_TFEET=940,652 FX_TMPX=325 reproduce; r131.json fixed_self:
             crop 150,60,900,540, tfeet = feet 640,598 / 209 px).
  RENDERED = the target's box as the harness actually drew it in the pre-cast frame N (default 10):
             every pixel that differs from the clear colour at (5,5), split at column 800 (caster
             left, NPC target right; fixed_self has only the caster). feet = (column centroid,
             lowest lit row), px = lowest - highest lit row + 1. The crop is extended to y 800
             (150,60,990,740 / 150,60,900,740) so every pixel at and below the feet is inside it.
The b1130 preset's target fit (run.log "rendered target feet row 652 at column 940, 50 px") is the
defect (B1166, #485): the fit is solved on the rest pose, the running frames draw the target at
rows ~467..782. This script does NOT fix the fit; it only measures where the target really is.

Per cast: fx_measure.py window BC-1.2 .. min(Act+12, vend-0.05) (as harness_measure_b1130_action.py,
BC = Act - 1 frame on the action-only path), landing_onset_abs.py d 209 (RISE onset/end/cov over the
whole crop; fx_timing.py first for its lit80.tsv), anchor.py d <tfeet> <tpx> LANDING --halfw <0.7|1.6> (RUN anatomy).
Output: <out_root>/{preset,rendered}/measure.txt (raw) and <out_root>/anchor_table.tsv
(anchor, crop, tfeet, tpx, onset, end, peak, RISE cov, run cov, cen_h, h_p5, h_p95, ground)."""
import sys, os, re, subprocess, glob
import numpy as np, cv2
PY = sys.executable
S = '/mnt/projects/fable/review/B1132/scripts'
# B1166 (#485): the fixed_npc TARGET anchor is the one the harness solves to,
# read from its single definition (tools/model_viewer/fx_target_anchor.h,
# kB1130TargetAnchor: the oracle r51 target's measured box, see refactor/B1166/README.md
# sec. 1), exactly as refactor/B1155/scripts/harness_measure_b1130.py reads it.
# FX_TFEET=940,652 FX_TMPX=325 reproduce the pre-B1166 measure (B1130's geom file,
# whose extent began at the name label).
_hdr = open(os.path.join(os.path.dirname(os.path.realpath(__file__)), '..', '..', '..',
                         'tools', 'model_viewer', 'fx_target_anchor.h')).read()
_a = re.search(r'kB1130TargetAnchor\{([\d.]+)f, ([\d.]+)f, ([\d.]+)f\}', _hdr)
TFEET = os.environ.get('FX_TFEET', f'{float(_a.group(1)):g},{float(_a.group(2)):g}')
TMPX = float(os.environ.get('FX_TMPX', _a.group(3)))
PRESET = {
    'fixed_npc':  dict(crop='150,60,990,540', tfeet=TFEET, tpx=int(TMPX), halfw=0.7),
    'fixed_self': dict(crop='150,60,900,540', tfeet='640,598', tpx=209, halfw=1.6),
}
EXT_CROP = {'fixed_npc': '150,60,990,740', 'fixed_self': '150,60,900,740'}
MPX = 209  # the caster's model px (review/B1130/geom/r51.json, r131.json)

a = sys.argv[1:]
fd, root, kind = os.path.realpath(a[0]), os.path.realpath(a[1]), a[2]
pre = int(a[a.index('--precast') + 1]) if '--precast' in a else 10
override = a[a.index('--anchor') + 1] if '--anchor' in a else None

im = cv2.imread(f'{fd}/frame_{pre:05d}.png').astype(int)
lit = np.abs(im - im[5, 5]).sum(2) > 0
lo = 800 if kind == 'fixed_npc' else 0
hi = 1280 if kind == 'fixed_npc' else 800
ys, xs = np.nonzero(lit[:, lo:hi]); xs = xs + lo
derived = (int(round(xs.mean())), int(ys.max()), int(ys.max() - ys.min() + 1))
print(f'pre-cast frame_{pre:05d}.png: {"target" if kind == "fixed_npc" else "caster"} lit rows {ys.min()}..{ys.max()} '
      f'columns {xs.min()}..{xs.max()} centroid {xs.mean():.1f} -> rendered feet {derived[0]},{derived[1]} / {derived[2]} px')
if override:
    ox, oy, op = map(int, override.split(','))
    derived = (ox, oy, op)
    print(f'--anchor override: feet {ox},{oy} / {op} px')
P = PRESET[kind]
GEOMS = [('preset', P['crop'], P['tfeet'], P['tpx']),
         ('rendered', EXT_CROP[kind], f'{derived[0]},{derived[1]}', derived[2])]
for name, crop, tfeet, tpx in GEOMS:
    print(f'anchor {name}: crop {crop} tfeet {tfeet} / {tpx} px halfw {P["halfw"]}')

rows = [l.rstrip('\n').split('\t') for l in open(fd + '/timing.tsv')][1:]
ts = [float(r[1]) for r in rows if r[2] == '-']
fps = round(1.0 / (ts[1] - ts[0]))
vend = (len(ts) - 1) / fps
casts, open_bc = [], {}
for r in rows:
    if r[2] == 'BeginCast': open_bc[int(r[3])] = float(r[7])
    elif r[2] == 'Action':
        s = int(r[3]); casts.append((s, open_bc.pop(s, float(r[7]) - 1.0 / fps), float(r[7])))
os.makedirs(root, exist_ok=True)
video = root + '/video.mkv'
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-framerate', str(fps), '-i', fd + '/frame_%05d.png',
                '-c:v', 'ffv1', video], check=True)

def sh(args):
    p = subprocess.run([PY, S + '/' + args[0]] + list(args[1:]), capture_output=True, text=True)
    if p.returncode: raise SystemExit(f'{args[0]} failed:\n{p.stdout}\n{p.stderr}')
    return p.stdout

out = ['anchor\tspell\tcrop\ttfeet\ttpx\tonset_act\tend_act\tpeak\trise_cov\trun_cov\tcen_h\th_p5\th_p95\tground']
for name, crop, tfeet, tpx in GEOMS:
    gd = f'{root}/{name}'; os.makedirs(gd, exist_ok=True)
    log = open(f'{gd}/measure.txt', 'w')
    log.write(f'# anchor {name}: crop {crop} tfeet {tfeet} / {tpx} px halfw {P["halfw"]}\n')
    for s, tbc, tact in casts:
        d = f'{gd}/fable_{s}'
        tend = min(tact + 12.0, vend - 0.05)
        log.write(f'## spell {s} {kind} BC {tbc:.3f} Act {tact:.3f} end {tend:.3f}\n'); log.flush()
        subprocess.run([PY, S + '/fx_measure.py', video, '0', str(tbc), str(tact), str(tend), crop, d,
                        '--label', f'fable{s}_{kind}_{name}'], check=True, stdout=log)
        try:  # writes lit80.tsv; its SOURCE line needs a non-empty BC..Act window (none on the action-only path)
            log.write(sh(['fx_timing.py', d, str(MPX)]))
        except SystemExit as e:
            log.write(f'fx_timing.py skipped (empty BC..Act window): {str(e).splitlines()[0]}\n')
        rise = sh(['landing_onset_abs.py', d, str(MPX)]); log.write(rise)
        land = sh(['anchor.py', d, tfeet, str(tpx), 'LANDING', '--halfw', str(P['halfw'])]); log.write(land); log.flush()
        r = re.search(r'RISE onset f(\d+) Act([+-][\d.]+)s end f(\d+) Act([+-][\d.]+)s peak f(\d+) Act([+-][\d.]+)s coverage ([\d.]+)', rise)
        u = re.search(r'RUN f\d+-f\d+ \(\d+ frames\): cov ([\d.]+) cen_h ([\d.-]+) .*?h_p5\.\.p95 ([\d.-]+)\.\.([\d.-]+) .*?ground=([\d.]+)', land)
        if r:
            on, en, pk, rc = f'+{r.group(2).lstrip("+")} (f{r.group(1)})', f'+{r.group(4).lstrip("+")} (f{r.group(3)})', f'f{r.group(5)} Act{r.group(6)}', r.group(7)
        else:
            on = en = pk = 'none'; rc = '-'
        rc_, ch, h5, h95, gr = u.groups() if u else ('-',) * 5
        out.append('\t'.join(map(str, [name, s, crop, tfeet, tpx, on, en, pk, rc, rc_, ch, h5, h95, gr])))
    log.close()
    if not os.environ.get('FX_KEEP'):
        for pth in glob.glob(f'{gd}/fable_*/f*.png'): os.remove(pth)
open(f'{root}/anchor_table.tsv', 'w').write('\n'.join(out) + '\n')
print('\n'.join(out))
