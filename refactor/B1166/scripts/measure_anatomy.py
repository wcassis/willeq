#!/usr/bin/env python3
"""B1166 (#485) ask 4: one landing's anatomy at a GIVEN target anchor, with B1130's own
unchanged instruments (review/B1132/scripts/fx_measure.py + landing_onset_abs.py,
review/B1130/scripts/anchor.py LANDING), for an oracle capture or a harness run alike.

usage:
  measure_anatomy.py oracle <video.mp4> <pts_offset> <t_bc> <t_act> <t_end> <out> <crop> <tfeet> <tpx> [exclude ...]
  measure_anatomy.py harness <frames_dir> <out> <crop> <tfeet> <tpx> [exclude ...]
The harness form takes BC/Act from timing.tsv (an action-only cast: BC = Act - 1 frame, B1162's
rule) and t_end = min(Act + 12, video end - 0.05) (B1155's). Excludes are crop-relative
x,y,w,h (anchor.py's --exclude); the oracle form ALSO applies cast_measure.py's two UI
excludes (600,40,170,75 and 0,0,200,800). halfw 0.7 (r51.json thalfw). Prints one TSV row:
label crop tfeet tpx onset end peak rise_cov run_cov cen_h h_p5 h_p95 ground."""
import sys, os, re, subprocess
PY = sys.executable
S22 = '/mnt/projects/fable/review/B1132/scripts'
S30 = '/mnt/projects/fable/review/B1130/scripts'
a = sys.argv[1:]
if a[0] == 'oracle':
    video, off, tbc, tact, tend, out, crop, tfeet, tpx = a[1:10]; exc = a[10:]
    exc = ['600,40,170,75', '0,0,200,800'] + exc
else:
    fd, out, crop, tfeet, tpx = a[1:6]; exc = a[6:]
    rows = [l.rstrip('\n').split('\t') for l in open(fd + '/timing.tsv')][1:]
    ts = [float(r[1]) for r in rows if r[2] == '-']
    fps = round(1.0 / (ts[1] - ts[0]))
    act = [r for r in rows if r[2] == 'Action'][0]
    bcs = [r for r in rows if r[2] == 'BeginCast']
    tact = float(act[7]); tbc = float(bcs[0][7]) if bcs else tact - 1.0 / fps
    tend = min(tact + 12.0, (len(ts) - 1) / fps - 0.05)
    os.makedirs(out, exist_ok=True)
    video, off = out + '/video.mkv', '0'
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-framerate', str(fps), '-i', fd + '/frame_%05d.png',
                    '-c:v', 'ffv1', video], check=True)
    tbc, tact, tend = f'{tbc:.6f}', f'{tact:.6f}', f'{tend:.6f}'
os.makedirs(out, exist_ok=True)
d = out + '/m'
log = open(out + '/measure.txt', 'w')
subprocess.run([PY, S22 + '/fx_measure.py', video, off, tbc, tact, tend, crop, d, '--label', os.path.basename(out)],
               check=True, stdout=log)
def sh(c):
    p = subprocess.run([PY] + c, capture_output=True, text=True); log.write(p.stdout + p.stderr); return p.stdout
sh([S22 + '/fx_timing.py', d, '209'])
rise = sh([S22 + '/landing_onset_abs.py', d, '209'])
xa = sum([['--exclude', e] for e in exc], [])
land = sh([S30 + '/anchor.py', d, tfeet, tpx, 'LANDING', '--halfw', '0.7'] + xa)
r = re.search(r'RISE onset f(\d+) Act([+-][\d.]+)s end f(\d+) Act([+-][\d.]+)s peak f(\d+) Act([+-][\d.]+)s coverage ([\d.]+)', rise)
u = re.search(r'RUN f\d+-f\d+ \(\d+ frames\): cov ([\d.]+) cen_h ([\d.-]+) .*?h_p5\.\.p95 ([\d.-]+)\.\.([\d.-]+) .*?ground=([\d.]+)', land)
on, en, pk, rc = (f'{r.group(2)} (f{r.group(1)})', f'{r.group(4)} (f{r.group(3)})', f'f{r.group(5)} Act{r.group(6)}', r.group(7)) if r else ('none',) * 4
rr = u.groups() if u else ('-',) * 5
row = '\t'.join([os.path.basename(out), crop, tfeet, tpx, on, en, pk, rc, *rr])
open(out + '/row.tsv', 'w').write('label\tcrop\ttfeet\ttpx\tonset_act\tend_act\tpeak\trise_cov\trun_cov\tcen_h\th_p5\th_p95\tground\n' + row + '\n')
print(row)
