#!/usr/bin/env python3
"""B1170 (#489): the decoded 0x4033 missile object, flown in Python from the
same eqgame rules the C++ object implements (refactor/B1170/README.md (A)/(B)):
the spawn (FUN_00476b12), the homing (FUN_004766aa) and the physics
(FUN_004bfe0f's missile arm) per >= 33-unit pass, the EQGraphicsDX9 512-entry
trig tables (FUN_101848a0, the _ftol2 truncation), the bearing FUN_0046e42f.
It reads the geometry the harness printed for the injected payload and
predicts, per written 30 fps frame, the object's position and the frame it is
removed in; then it compares that with the harness's missile.tsv.

usage: python3 -P missile_flight_model.py <run.log> <missile.tsv> --inject-at 4.0
           [--fps 30] [--life 30000] [--out <pred.tsv>]
Exit 0 when the harness's last frame with the object and the model's agree
within one frame and every common frame's position is within 0.01 units.
"""
import argparse
import math
import re
import sys

import numpy as np

F = np.float32

STEP = 0.012271846302734375  # [0x101ec4b0] (f64)
COS = [F(math.cos(i * STEP)) for i in range(512)]
SIN = [F(0.0)] * 512
for i in range(512):
    SIN[(i + 128) & 511] = COS[i]
SIN[0] = F(0); SIN[256] = F(0); SIN[128] = F(1); SIN[384] = F(-1)
COS[128] = F(0); COS[384] = F(0); COS[0] = F(1); COS[256] = F(-1)
TAN = [F(SIN[i] / COS[i]) for i in range(128)]


def idx(a):
    return int(a) & 0x1ff  # trunc toward zero, & 0x1ff


def wrap(a):
    i = 0
    while a >= 512.0:
        a = F(a - 512.0)
        if i > 5:
            a = F(0)
            break
        i += 1
    i = 0
    while a < 0.0:
        a = F(a + 512.0)
        if i > 5:
            a = F(0)
            break
        i += 1
    return a


def angle_diff(a, b):
    lo, hi = (b, a) if b < a else (a, b)
    d = F(hi - lo)
    if d <= 256.0:
        return d, d
    return F(-(d - 512.0)), F(d - 512.0)


def bearing(y1, x1, y2, x2):
    dy = abs(float(y1) - float(y2))
    dx = abs(float(x1) - float(x2))
    dy = max(dy, 9.999999974752427e-07)
    f = math.atan2(dx, dy) * 180.0 * 0.3183099014828645
    if y2 <= y1:
        f = (90.0 - f) + 90.0 if x1 <= x2 else f + 180.0
    elif x2 <= x1:
        f = (90.0 - f) + 270.0
    return F(f * 511.5 * float(F(0.0027777778450399637)))


def atan_index(r):
    best = 0
    for i in range(128):
        if TAN[i] <= r:
            best = i
    return best


def fly(geo, inject_at, fps, sim_hz, life):
    pos = [F(geo['sy']), F(geo['sx']), F(geo['sz'])]
    vel = [F(0), F(0), F(0)]
    word, damped, heading, aim = F(geo['word']), geo['damped'], F(geo['heading']), F(0)
    ty, tx, tz = F(geo['ty']), F(geo['tx']), F(geo['tz'])
    aimz = F(F(geo['height']) * F(0.5) + tz)
    dt_step = F(1.0 / sim_hz)
    clock = 0.0
    frames = {}
    g = 0
    spawned = False
    removed_step = None
    last = refresh = 0
    stored = F(2000)
    bear, aimt = heading, aim
    while g < sim_hz * 60:
        t = g / sim_hz
        clock += float(dt_step) * 1000.0
        now = 1 + int(clock)
        if not spawned and t + 1e-9 >= inject_at:
            spawned = True
            life_end = now + life
            last = now
            refresh = now + 2000
            stored = F(2000)
            bear, aimt = heading, aim
        elif spawned and now - last >= 33:
            hit = False
            # FUN_004766aa
            if word != 0:
                thr = 0
                if stored >= 200.0:
                    thr = 250 if stored < 250.0 else (500 if stored < 350.0 else 1000)
                elif stored < 2.0:
                    hit = True
                if not hit:
                    if ((now - refresh) & 0xffffffff) > thr:
                        dz = F(aimz - pos[2])
                        adz = abs(dz)
                        refresh = now
                        bear = bearing(pos[0], pos[1], ty, tx)
                        stored = F(math.hypot(float(pos[0]) - float(ty), float(pos[1]) - float(tx)))
                        if adz <= 0.5:
                            aimt = F(0)
                        else:
                            aimt = F(128.0 - atan_index(F(stored / adz)))
                            if dz < 0:
                                aimt = F(-aimt)
                    if stored < 32.0:
                        heading, aim = bear, aimt
                    else:
                        d1, s1 = angle_diff(heading, bear)
                        d2, s2 = angle_diff(aim, aimt)
                        if d1 >= 1.0:
                            step = d1 if d1 < 16.0 else F(16)
                            if (heading < bear and s1 < 0) or (bear < heading and s1 > 0):
                                heading = wrap(F(heading - step))
                            else:
                                heading = wrap(F(step + heading))
                        else:
                            heading = bear
                        if d2 >= 1.0:
                            if d2 >= 16.0:
                                aim = wrap(F(aim - 16.0) if (s2 < 0 or aimt < aim) else F(aim + 16.0))
                        else:
                            aim = aimt
            # FUN_004bfe0f
            dt = F(float((now - last) & 0xffffffff) * 0.019999999552965164)
            dt = min(dt, F(3.0))
            last = now
            th = [F(0), F(0), F(0)]
            if word != 0:
                th[0] = F(COS[idx(heading)] * word)
                th[1] = F(SIN[idx(heading)] * word)
                if aim != 0:
                    th[2] = F(SIN[idx(aim)] * word)
            damp = F(0.85) if damped == 0 else F(0.625)
            th = [F(v * F(0.8)) if v != 0 else v for v in th]
            vel = [F(damp * v) if v != 0 else v for v in vel]
            vel = [F(0) if abs(float(v)) < 1.1920928955078125e-07 else v for v in vel]
            vel = [F(th[i] + vel[i]) for i in range(3)]
            if damped == 0 and math.hypot(float(vel[0]), float(vel[1])) < 0.05000000074505806:
                vel = [F(0)] * 3
            pos = [F(F(dt * vel[i]) + pos[i]) for i in range(3)]
            if hit or now > life_end:
                removed_step = g
                break
        if spawned and g % (sim_hz // fps) == 0:
            frames[g // (sim_hz // fps)] = tuple(float(p) for p in pos)
        g += 1
    removed_frame = None if removed_step is None else (removed_step + (sim_hz // fps) - 1) // (sim_hz // fps)
    return frames, removed_frame


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('runlog')
    ap.add_argument('missile_tsv')
    ap.add_argument('--inject-at', type=float, required=True)
    ap.add_argument('--fps', type=int, default=30)
    ap.add_argument('--life', type=int, default=30000)
    ap.add_argument('--out')
    a = ap.parse_args()
    m = None
    for line in open(a.runlog, errors='replace'):
        m = re.search(r'0x4033 geometry: source \(y,x,z\) (\S+) (\S+) (\S+) target (\S+) (\S+) (\S+) height (\S+) '
                      r'heading (\S+) word (\S+) \+0x62 (\d+) sim_hz (\d+)', line) or m
    if not m:
        print('no geometry line in', a.runlog)
        return 2
    v = [float(x) for x in m.groups()[:9]]
    geo = dict(sy=v[0], sx=v[1], sz=v[2], ty=v[3], tx=v[4], tz=v[5], height=v[6], heading=v[7], word=v[8],
               damped=int(m.group(10)))
    sim_hz = int(m.group(11))
    frames, removed = fly(geo, a.inject_at, a.fps, sim_hz, a.life)
    harness = {}
    for line in open(a.missile_tsv):
        c = line.rstrip('\n').split('\t')
        if c[0] == 'frame' or c[2] == '0':
            continue
        harness[int(c[0])] = (float(c[3]), float(c[4]), float(c[5]))
    model_last = max(frames) if frames else None
    harness_last = max(harness) if harness else None
    worst = 0.0
    rows = ['frame\tmodel_y\tmodel_x\tmodel_z\tharness_y\tharness_x\tharness_z\tdist']
    for f in sorted(set(frames) | set(harness)):
        mp, hp = frames.get(f), harness.get(f)
        d = math.dist(mp, hp) if mp and hp else float('nan')
        if mp and hp:
            worst = max(worst, d)
        fmt = lambda p: '\t'.join('%.4f' % x for x in p) if p else '-\t-\t-'
        rows.append('%d\t%s\t%s\t%s' % (f, fmt(mp), fmt(hp), '-' if math.isnan(d) else '%.4f' % d))
    if a.out:
        open(a.out, 'w').write('\n'.join(rows) + '\n')
    first_m = min(frames) if frames else None
    first_h = min(harness) if harness else None
    print('model: first frame %s, last frame %s (removed in step-frame %s); harness: first %s, last %s; '
          'max position difference %.4f units' % (first_m, model_last, removed, first_h, harness_last, worst))
    ok = (first_m == first_h and model_last is not None and harness_last is not None
          and abs(model_last - harness_last) <= 1 and worst <= 0.01)
    print('MATCH' if ok else 'MISMATCH')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
