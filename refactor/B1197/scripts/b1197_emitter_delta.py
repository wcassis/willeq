#!/usr/bin/env python3
"""B1197: per-emitter isolated contribution for record 35 (B1188's method).

B1188 (refactor/B1188/README.md sec. 2): each landing emitter rendered alone
(the WILLEQ_FX_LANDING_EMITTERS knob, spell_visual_fx.cpp's
LandingEmitterFilter) plus a no-landing run e0; the landing is additive, so
the emitter's exact pixel contribution is clip(run - e0, 0) per pixel.

All runs share the plan/seed/camera, so the frame indices align one-for-one.

usage: b1197_emitter_delta.py <e0_dir> <run_dir[:label]>... [tsv_out]
       (frame_*.png, 30 fps; the e0 dir is the subtraction base; the label
       defaults to the dir's basename, so A/B dirs keep their own names)

Prints per run: peak frame + n, the lit80/px150 window integral (sum of
n over the run), and the window channel means of the ADDED light (the
contribution, not the scene). The tsv carries the per-frame series (n and
channel means per set).
"""
import os
import sys
import glob

import numpy as np
import cv2


def parse_run(arg, i):
    if ':' in arg:
        d, tag = arg.rsplit(':', 1)
        return d, tag
    return arg, os.path.basename(os.path.normpath(arg))


e0_dir = sys.argv[1]
# The last arg is the tsv only when it is not a frame dir.
args = sys.argv[2:]
tsv_out = None
if len(args) >= 1 and not os.path.isdir(args[-1]):
    tsv_out = args[-1]
    args = args[:-1]
run_dirs = [parse_run(a, i) for i, a in enumerate(args)]
FPS = 30.0
WINDOW_MIN_N = 50


def files(d):
    return sorted(glob.glob(os.path.join(d, 'frame_*.png')),
                  key=lambda p: int(os.path.basename(p)[6:11]))


def load(p):
    img = cv2.imread(p, cv2.IMREAD_COLOR)
    assert img is not None, f'unreadable frame {p}'
    return img[:, :, ::-1].astype(np.float32)


e0f = files(e0_dir)
print(f'e0 ({e0_dir}): {len(e0f)} frames')
print('emitters:', [d for d in run_dirs])

fh = open(tsv_out, 'w') if tsv_out else None
if fh:
    fh.write('emitter\tframe\tt_s\tset\tn\tR_add\tG_add\tB_add\n')

for d, tag in run_dirs:
    rf = files(d)
    assert len(rf) == len(e0f), f'{d}: {len(rf)} frames != e0 {len(e0f)}'
    print(f'\n===== emitter {tag} ({d}, {len(rf)} frames) =====')
    pk = (0, -1)
    series = []
    for i in range(len(rf)):
        contrib = np.clip(load(rf[i]) - load(e0f[i]), 0, 255)
        mx = contrib.max(axis=2)
        rec = {'i': i, 't': i / FPS}
        for nm, thr in (('lit80', 80), ('px150', 150)):
            m = mx > thr
            rec[nm] = {'n': int(m.sum()),
                       'mean': contrib[m].mean(axis=0) if m.any() else np.zeros(3)}
        series.append(rec)
        if rec['lit80']['n'] > pk[1]:
            pk = (i, rec['lit80']['n'])
    win = [r for r in series if r['lit80']['n'] >= WINDOW_MIN_N]
    i0, i1 = (win[0]['i'], win[-1]['i']) if win else ('-', '-')
    print(f'peak frame f{pk[0]} (t={pk[0] / FPS:.2f}s) lit80 n={pk[1]}')
    print(f'landing window: f{i0}..f{i1}')
    for nm in ('lit80', 'px150'):
        n_pk = series[pk[0]][nm]['n']
        n_win = sum(r[nm]['n'] for r in win)
        tot = np.array([sum(r[nm]['n'] * r[nm]['mean'][c] for r in win) for c in range(3)])
        m = tot / max(n_win, 1)
        integral = sum(r[nm]['n'] for r in series)  # whole-run integral
        print(f'  {nm}: peak px={n_pk} | window px={n_win} R={m[0]:.1f} G={m[1]:.1f} B={m[2]:.1f} '
              f'| whole-run integral n={integral}')
    if fh:
        for r in series:
            for nm in ('lit80', 'px150'):
                mm = r[nm]['mean']
                fh.write(f'{tag}\t{r["i"]}\t{r["t"]:.4f}\t{nm}\t{r[nm]["n"]}\t'
                         f'{mm[0]:.3f}\t{mm[1]:.3f}\t{mm[2]:.3f}\n')

if fh:
    fh.close()
print('\nDONE')
