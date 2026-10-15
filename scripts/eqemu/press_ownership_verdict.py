#!/usr/bin/env python3
"""B1066 (#386): press_ownership_check.sh's verdict, from the pose log alone.

For each gesture window [<g>_start, <g>_end] in marks.tsv, the heading
(heading0_512) and the camera pitch (degrees, from the camX..targetZ columns)
of the last pose row at or before each mark. ui_* gestures must leave both
unchanged; view_right must change the heading; view_cross must keep changing
it after the pointer is inside the window (view_cross_inside -> _end).

Exit 0 PASS, 1 FAIL, 2 no verdict.
"""
import argparse
import math
import sys

UNCHANGED_HEADING = 0.01   # EQ heading units (512 per turn)
UNCHANGED_PITCH = 0.01     # degrees
CHANGED_HEADING = 1.0      # a 20 px drag at the default sensitivity is ~10 units


def load_pose(path):
    rows = []
    with open(path) as f:
        header = f.readline().rstrip("\n").split("\t")
        idx = {name: i for i, name in enumerate(header)}
        for line in f:
            c = line.rstrip("\n").split("\t")
            if len(c) != len(header):
                continue
            try:
                ts = int(c[idx["ts_unix_ms"]])
                h = float(c[idx["heading0_512"]])
                cx, cy, cz = (float(c[idx[k]]) for k in ("camX", "camY", "camZ"))
                tx, ty, tz = (float(c[idx[k]]) for k in ("targetX", "targetY", "targetZ"))
            except (ValueError, KeyError):
                continue
            pitch = math.degrees(math.atan2(tz - cz, math.hypot(tx - cx, ty - cy)))
            rows.append((ts, h, pitch))
    return rows


def at(rows, ts):
    best = None
    for r in rows:
        if r[0] <= ts:
            best = r
        else:
            break
    return best


def dh(a, b):
    d = (b - a) % 512.0
    return d - 512.0 if d > 256.0 else d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pose", required=True)
    ap.add_argument("--marks", required=True)
    a = ap.parse_args()
    rows = load_pose(a.pose)
    marks = {}
    with open(a.marks) as f:
        f.readline()
        for line in f:
            k, v = line.split()
            marks[k] = int(v)
    if not rows:
        print("NO VERDICT: no pose rows")
        return 2

    def delta(m0, m1):
        r0, r1 = at(rows, marks[m0]), at(rows, marks[m1])
        if r0 is None or r1 is None:
            return None
        return (r0, r1, dh(r0[1], r1[1]), r1[2] - r0[2])

    ok = True
    print("gesture\tfrom_mark\tto_mark\theading_before\theading_after\td_heading\tpitch_before\tpitch_after\td_pitch\texpect\tresult")
    checks = [
        ("ui_right", "ui_right_start", "ui_right_end", "unchanged"),
        ("ui_left", "ui_left_start", "ui_left_end", "unchanged"),
        ("view_right", "view_right_start", "view_right_end", "changed"),
        ("view_cross", "view_cross_start", "view_cross_inside", "changed"),
        ("view_cross(inside)", "view_cross_inside", "view_cross_end", "changed"),
    ]
    for name, m0, m1, expect in checks:
        if m0 not in marks or m1 not in marks:
            print(f"{name}\t{m0}\t{m1}\t-\t-\t-\t-\t-\t-\t{expect}\tNO VERDICT (mark missing)")
            ok = False
            continue
        d = delta(m0, m1)
        if d is None:
            print(f"{name}\t{m0}\t{m1}\t-\t-\t-\t-\t-\t-\t{expect}\tNO VERDICT (no pose row)")
            ok = False
            continue
        r0, r1, d_h, d_p = d
        if expect == "unchanged":
            good = abs(d_h) < UNCHANGED_HEADING and abs(d_p) < UNCHANGED_PITCH
        else:
            good = abs(d_h) >= CHANGED_HEADING
        ok &= good
        print(f"{name}\t{m0}\t{m1}\t{r0[1]:.4f}\t{r1[1]:.4f}\t{d_h:+.4f}\t{r0[2]:.4f}\t{r1[2]:.4f}\t{d_p:+.4f}\t{expect}\t{'PASS' if good else 'FAIL'}")
    print("VERDICT", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
