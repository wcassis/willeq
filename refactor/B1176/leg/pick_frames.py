#!/usr/bin/env python3
"""B1176 (#495): pick the measurement frames from a frame_diffs.sh TSV.

The fable tour (refactor/B1176/leg/fable_tour.txt) is: zone in, hold
FIRST PERSON (the login default) through `wait 60/10` + `/fpcam dump`,
then `/camera user1` (the third-person row) and hold through the two
`/camera get`s and the final wait, then /q. Between the commands the
scene is a static hold, so the consecutive-frame AE diffs (ImageMagick)
are small inside a hold and large at the camera transition. Segments of
consecutive diffs below --thr are the holds:

  - the FP hold  = the first long stable segment after the zone-in
    transition (the zone-in itself is the first big diff cluster);
  - the chase    = the final stable segment (after the user1 switch).

Prints the selected frame file names (FP = the LAST stable frame of the
FP hold -- closest to the /camera user1 switch; chase = the LAST stable
frame of the final hold -- closest to /q, camera settled) plus the
segment map for the record. usage: pick_frames.py <diffs.tsv>
[--thr 15000] [--minlen 3]
"""
import sys


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    kv = {a[2:]: sys.argv[i + 1] for i, a in enumerate(sys.argv)
          if a.startswith("--") and i + 1 < len(sys.argv)}
    thr = float(kv.get("thr", 15000))
    minlen = int(kv.get("minlen", 3))

    rows = []
    with open(args[0]) as fh:
        for line in fh:
            p = line.rstrip("\n").split("\t")
            if len(p) < 3 or p[0] == "index":
                continue
            rows.append((int(p[0]), p[1], float(p[2])))

    if not rows:
        raise SystemExit("empty tsv")
    n = rows[-1][0] + 1

    # stable[i] = frame i looks the same as frame i-1
    stable = {r[0]: r[2] < thr for r in rows}

    # segments of consecutive frames (1, 2, ..., k) where every step
    # i-1 -> i is stable
    segs = []
    start = None
    for i in range(1, n):
        if stable.get(i, False) and start is None:
            start = i - 1
        if (not stable.get(i, True)) and start is not None:
            segs.append((start, i - 1))
            start = None
    if start is not None:
        segs.append((start, n - 1))
    segs = [s for s in segs if s[1] - s[0] + 1 >= minlen]

    print("segments (first, last, len):")
    for a, b in segs:
        print("  %d %d %d" % (a, b, b - a + 1))

    if len(segs) < 2:
        raise SystemExit("need >=2 stable segments (got %d)" % len(segs))
    # zone-in is the first big transition: the first long segment after
    # it is the FP hold; the last segment is the chase hold. If the
    # pre-zone login screen produced a stable segment, it is BEFORE the
    # zone-in transition, so dropping segs[0] is only correct when there
    # were at least 3 segments; with exactly 2, segs[0] IS the FP hold.
    fp = segs[0] if len(segs) == 2 else segs[1]
    chase = segs[-1]
    by_index = {r[0]: r[1] for r in rows}
    # frame i's file name: the diff row for index i names frame i; frame 0
    # has no row, so derive its name arithmetically (the dump is
    # frame_%06d.png, application_frame.cpp).
    import re
    def fname(i):
        if i in by_index:
            return by_index[i]
        if not rows:
            raise SystemExit("cannot derive frame 0 name")
        m = re.match(r"frame_(\d+)\.png$", rows[0][1])
        return "frame_%06d.png" % (int(m.group(1)) - 1)
    print("fp_frame\t%s\t(segment %d-%d)" % (fname(fp[1]), fp[0], fp[1]))
    print("chase_frame\t%s\t(segment %d-%d)" % (fname(chase[1]), chase[0], chase[1]))
    # neighbours for sanity pulls
    print("fp_prev\t%s" % fname(max(0, fp[1] - 1)))
    print("chase_prev\t%s" % fname(max(0, chase[1] - 1)))


if __name__ == "__main__":
    main()
