#!/usr/bin/env python3
"""B1155: measure one model_viewer FX-harness run on the b1130 / b1130-self preset with
B1130's instruments, UNCHANGED (the lane-5 fable tools of review/B1132/scripts/ and the
B1130 stage instruments, run exactly as review/B1130/scripts/cast_measure.py runs them,
minus the pcap: the cast times come from the harness's timing.tsv, as
review/B1139/scripts/harness_measure.py does for b1118).

usage: harness_measure_b1130.py <frames_dir> <measure_root> <fixed_npc|fixed_self>

Geometry = B1130's fixed cast geometry, per the two geom files
(review/B1130/geom/r51.json fixed_npc: crop 150,60,990,540, feet 640,598 209 px,
halfw 0.7, with the target anchor of tools/model_viewer/fx_target_anchor.h -- B1166; review/B1130/geom/r131.json fixed_self: crop
150,60,900,540, tfeet = feet, halfw 1.6). The B1130 nameplate --exclude
rectangles do NOT apply: the harness draws no UI (the oracle's nameplates are
oracle-side chrome, like the Tip-of-the-Day window B1142 removed).

Per cast (fx_measure.py window BC-1.2 .. min(Act+12, vend-0.05) -- B1130's own
tend = Act+13 minus the 1 s the harness tail needs):
  fx_timing.py d 209                      -> lit80.tsv (the tail-baseline lit series)
  landing_onset_abs.py d 209              -> RISE onset/end/coverage (landing cov + end, Act+)
  anchor.py d 640,598 209 SOURCE --halfw  -> SOURCE onset, peak(cast) cov, CAST cen_h + h p5..p95
  anchor.py d <tfeet> <tmpx> LANDING      -> the landing's anatomy (record; same halfw)
  fx_sprite_roi.py d 209 SOURCE 0.3..Act  -> p90 source sprite (ROI = summarize.py's roi_c)
  fx_sprite_roi.py d 209 LANDING lon..le  -> p90 landing sprite  (ROI = summarize.py's roi_t)

ROI rectangles are review/B1130/scripts/summarize.py's, computed from the
preset's own numbers (its lines: roi_c = fx-1.5mpx..fx+1.5mpx, fy-1.6mpx..fy+0.1mpx;
roi_t = tfx-1.5tm..tfx+1.5tm, tfy-2.0tm..tfy+0.1tm, crop px, crop origin the offset).

Output: <measure_root>/b1130_rows.tsv, one row per cast:
  spell source_onset_s source_cov source_centroid src_h_lo src_h_hi
        landing_onset_s landing_end_s landing_cov p90_src p90_land
plus the raw instrument output in <measure_root>/measure_b1130.txt."""
import sys, os, re, subprocess, glob
PY = sys.executable   # the venv python (numpy/cv2/scipy); the B1132 instruments need it
S = '/mnt/projects/fable/review/B1132/scripts'
B1130 = '/mnt/projects/fable/review/B1130'
# B1166 (#485): the fixed_npc TARGET anchor is the one the harness solves to,
# read from its single definition (tools/model_viewer/fx_target_anchor.h,
# kB1130TargetAnchor: the oracle r51 target's measured box, feet 940,615 / 288 px)
# so the harness and this measure cannot disagree. B1130's geom file says
# tfeet 940,652 / tmpx 325, which began at the name label (refactor/B1166/README.md
# sec. 1). FX_TFEET=940,652 FX_TMPX=325 reproduce the pre-B1166 measure.
_hdr = open(os.path.join(os.path.dirname(os.path.realpath(__file__)), '..', '..', '..',
                         'tools', 'model_viewer', 'fx_target_anchor.h')).read()
_a = re.search(r'kB1130TargetAnchor\{([\d.]+)f, ([\d.]+)f, ([\d.]+)f\}', _hdr)
TFEET = os.environ.get('FX_TFEET', f'{float(_a.group(1)):g},{float(_a.group(2)):g}')
TMPX = float(os.environ.get('FX_TMPX', _a.group(3)))
GEOM = {
    # review/B1130/geom/r51.json (every fixed_npc cast; target anchor above), review/B1130/geom/r131.json (fixed_self).
    'fixed_npc':  dict(crop='150,60,990,540',  feet='640,598', mpx=209.0, tfeet=TFEET, tmpx=TMPX, halfw=0.7),
    'fixed_self': dict(crop='150,60,900,540',  feet='640,598', mpx=209.0, tfeet='640,598', tmpx=209.0, halfw=1.6),
}
fd, root, kind = os.path.realpath(sys.argv[1]), os.path.realpath(sys.argv[2]), sys.argv[3]
g = GEOM[kind]
os.makedirs(root, exist_ok=True)

rows = [l.rstrip('\n').split('\t') for l in open(fd + '/timing.tsv')][1:]
fps = None
ts = [float(r[1]) for r in rows if r[2] == '-']
if len(ts) > 1: fps = round(1.0 / (ts[1] - ts[0]))
video = root + '/video.mkv'
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-framerate', str(fps), '-i', fd + '/frame_%05d.png',
                '-c:v', 'ffv1', video], check=True)
vend = (len(ts) - 1) / fps

casts, open_bc = [], {}
for r in rows:
    if r[2] == 'BeginCast': open_bc[int(r[3])] = float(r[7])
    elif r[2] == 'Action':
        s = int(r[3]); casts.append((s, open_bc.pop(s), float(r[7])))
assert not open_bc, f'unmatched BeginCast {open_bc}'

cx, cy = int(g['crop'].split(',')[0]), int(g['crop'].split(',')[1])
fx, fy = map(float, g['feet'].split(','))
tfx, tfy = map(float, g['tfeet'].split(','))
mpx, tm = g['mpx'], g['tmpx']
# summarize.py's ROIs, in crop px (its exact formulae, the preset's own numbers):
roi_c = (int(fx - 1.5 * mpx - cx), int(fx + 1.5 * mpx - cx), int(fy - 1.6 * mpx - cy), int(fy + 0.1 * mpx - cy))
roi_t = (max(0, int(tfx - 1.5 * tm - cx)), int(tfx + 1.5 * tm - cx),
         max(0, int(tfy - 2.0 * tm - cy)), int(tfy + 0.1 * tm - cy))

def sh(args, env=None):
    e = dict(os.environ); e.update(env or {})
    cmd = [PY, S + '/' + args[0]] + list(args[1:]) if args[0].endswith('.py') else list(args)
    p = subprocess.run(cmd, capture_output=True, text=True, env=e)
    if p.returncode: raise SystemExit(f'{args[0]} failed:\n{p.stdout}\n{p.stderr}')
    return p.stdout

log = open(f'{root}/measure_b1130.txt', 'w')
out = []
# B1185 (#504): the Source stage's particle onset (the client's createCastGlow
# Source branch: sourceLatency + the slot's +0x8C start, spell_visual_fx.cpp:684),
# emitted by the harness to source_onset.tsv. This is the FIRST VISIBLE
# PARTICLE frame, not the cast-anim's first-lit frame (B1157's CastingAnim),
# which the anchor.py's pixel measure shadowed.
src_onset = {}
src_onset_file = fd + '/source_onset.tsv'
if os.path.exists(src_onset_file):
    for l in open(src_onset_file):
        parts = l.rstrip('\n').split('\t')
        if len(parts) >= 5 and parts[1] == 'Source':
            try:
                src_onset[int(parts[0])] = float(parts[4])
            except ValueError:
                pass
for s, tbc, tact in casts:
    d = f'{root}/fable_{s}'
    tend = min(tact + 12.0, vend - 0.05)  # B1130's Act+13 window minus the 1 s the RISE tail baseline needs
    log.write(f'## spell {s} {kind} BC {tbc:.3f} Act {tact:.3f} (cast {tact - tbc:.3f}s) end {tend:.3f}\n'); log.flush()
    subprocess.run([PY, S + '/fx_measure.py', video, '0', str(tbc), str(tact), str(tend),
                    g['crop'], d, '--label', f'fable{s}_{kind}'], check=True, stdout=log)
    log.write(sh(['fx_timing.py', d, str(int(mpx))])); log.flush()
    rise = sh(['landing_onset_abs.py', d, str(int(mpx))]); log.write(rise); log.flush()
    src = sh(['anchor.py', d, g['feet'], str(int(mpx)), 'SOURCE', '--halfw', str(g['halfw'])]); log.write(src); log.flush()
    land = sh(['anchor.py', d, g['tfeet'], str(int(tm)), 'LANDING', '--halfw', str(g['halfw'])]); log.write(land); log.flush()

    r = re.search(r'RISE onset f(\d+) Act([+-]\d+\.\d+)s end f(\d+) Act([+-]\d+\.\d+)s peak f(\d+) Act([+-]\d+\.\d+)s coverage ([\d.]+)', rise)
    lon, lend, lcov = (float(r.group(2)), float(r.group(4)), float(r.group(7))) if r else (float('nan'),) * 3
    m = re.search(r'SOURCE onset f(\d+) BC([+-]\d+\.\d+);', src)
    son_pixel = float(m.group(2)) if m else float('nan')
    # B1185 (#504): the Source stage's particle onset (the client's rule) is
    # the reported source_onset_s. The pixel measure (anchor.py's first-lit
    # frame) is kept as son_pixel for the log (diagnostic only).
    son = src_onset.get(s, son_pixel)
    m = re.search(r'peak\(cast\) f\d+ BC[+-]\d+\.\d+ cov ([\d.]+)', src)
    scov = float(m.group(1)) if m else float('nan')
    m = re.search(r'CAST f\d+-f\d+ \(\d+ frames\): cov [\d.]+ cen_h ([\d.]+) cen_dx [+-][\d.]+ dx_p5\.\.p95 [+-][\d.]+\.\.[+-][\d.]+ h_p5\.\.p95 ([\d.]+)\.\.([\d.]+)', src)
    scen, slo, shi = (float(m.group(1)), float(m.group(2)), float(m.group(3))) if m else (float('nan'),) * 3
    cast = tact - tbc
    p = re.search(r'p90 ([\d.]+)', sh(['fx_sprite_roi.py', d, str(int(mpx)), 'SOURCE', '0.3', str(cast)],
                                      {'ROI': ','.join(map(str, roi_c))}))
    p90s = float(p.group(1)) if p else float('nan')
    if r:  # a landing exists -> its p90 over the RISE run, the target's own ROI
        p = re.search(r'p90 ([\d.]+)', sh(['fx_sprite_roi.py', d, str(int(mpx)), 'LANDING', str(lon), str(lend)],
                                          {'ROI': ','.join(map(str, roi_t))}))
        p90l = float(p.group(1)) if p else float('nan')
    else:
        p90l = float('nan')
    log.flush()
    out.append(f'{s}\t{son:.3f}\t{scov:.3f}\t{scen:.3f}\t{slo:.3f}\t{shi:.3f}'
               f'\t{lon:+.3f}\t{lend:+.3f}\t{lcov:.3f}\t{p90s:.3f}\t{p90l:.3f}')

with open(f'{root}/b1130_rows.tsv', 'w') as f:
    f.write('spell\tsource_onset_s\tsource_cov\tsource_centroid\tsrc_h_lo\tsrc_h_hi\t'
            'landing_onset_s\tlanding_end_s\tlanding_cov\tp90_src\tp90_land\n')
    f.write('\n'.join(out) + '\n')
log.close()
if not os.environ.get('FX_KEEP'):
    for pth in glob.glob(f'{root}/fable_*/f*.png'): os.remove(pth)
print(open(f'{root}/b1130_rows.tsv').read(), end='')
