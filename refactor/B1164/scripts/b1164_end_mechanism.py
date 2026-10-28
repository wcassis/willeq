#!/usr/bin/env python3
"""B1164 (#483) fix leg (review note 7941 finding 5): why a LANDING onset / end moves between two
renders of one row. Re-derives review/B1118/scripts/fx_timing.py's LANDING rule from the lit80.tsv
it saves (the TAIL-baseline series lit2): floor2 = p95 of lit2 over the last 1.5 s, thr2 =
max(4 floor2, 0.003 model_px^2), stail = max lit2 over Act-0.25..0 s, onset = first Act >= 0 frame
with lit2 > max(thr2, 1.5 stail), end = last frame above thr2 of the run from the onset (0.3-s gaps
allowed). Prints both runs' thr2 / stail / onset / end and the lit2 series around each run's onset
and end, so the frame where one run falls below (or rises above) the threshold is read, not inferred.
usage: python3 -I b1164_end_mechanism.py <before fable_<sid> dir> <after fable_<sid> dir> [model_px 235]"""
import sys


def load(d):
    rows = [l.rstrip('\n').split('\t') for l in open(d + '/lit80.tsv')][1:]
    return [(int(r[0]), float(r[1]), float(r[2]), int(r[3])) for r in rows]


def pct95(v):
    s = sorted(v)
    k = (len(s) - 1) * 0.95
    lo = int(k)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def rule(R, mpx):
    ta_last = R[-1][2]
    floor2 = pct95([x[3] for x in R if x[2] >= ta_last - 1.5])
    thr2 = max(4 * floor2, 0.003 * mpx * mpx)
    st = [x[3] for x in R if -0.25 <= x[2] < 0]
    stail = max(st) if st else 0
    on = next((i for i, x in enumerate(R) if x[2] >= 0 and x[3] > max(thr2, 1.5 * stail)), None)
    end = on
    if on is not None:
        last_t = R[on][2]
        for i in range(on, len(R)):
            if R[i][3] > thr2:
                if R[i][2] - last_t > 0.3:
                    break
                end = i
                last_t = R[i][2]
    return thr2, stail, on, end


def main():
    b, a = load(sys.argv[1]), load(sys.argv[2])
    mpx = float(sys.argv[3]) if len(sys.argv) > 3 else 235.0
    rb, ra = rule(b, mpx), rule(a, mpx)
    for lab, R, (thr2, stail, on, end) in (('before', b, rb), ('after', a, ra)):
        if on is None:
            print(f'{lab}: thr2 {thr2:.0f}px stail {stail}px onset none')
            continue
        print(f'{lab}: thr2 {thr2:.0f}px stail {stail}px onset-threshold {max(thr2, 1.5 * stail):.0f}px '
              f'onset f{R[on][0]} Act{R[on][2]:+.3f}s end f{R[end][0]} Act{R[end][2]:+.3f}s')
    idx = {x[0]: x for x in a}
    marks = set()
    for R, (thr2, stail, on, end) in ((b, rb), (a, ra)):
        if on is not None:
            for k in (on, end):
                marks.update(range(R[k][0] - 3, R[k][0] + 4))
    print('frame\tt_act\tlit2_before\tlit2_after')
    for x in b:
        if x[0] in marks and x[0] in idx:
            print(f'{x[0]}\t{x[2]:+.3f}\t{x[3]}\t{idx[x[0]][3]}')


if __name__ == '__main__':
    main()
