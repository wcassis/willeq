#!/usr/bin/env python3
"""B1067 (#387): the step-walk verdict, from the client's --pose-log TSV alone.

One leg = the player walked (W held between keydown_ms and keyup_ms) across a
straight edge at y = --edge-y, moving in --dir (+1 = +y, -1 = -y), where the
floor rises (--leg rise) or drops (--leg drop) by --rise units.

    start  = the last pose row before keydown
    pre    = the last row after keydown still >= 0.5 units short of the edge
             (the floor the player actually stood on before the edge: the
             rig seeds the character a little ABOVE the floor and the first
             moving frame snaps it down)
    end    = the last pose row (the client logged out after the walk)
    PASS   = the player's Y got >= 2 units past the edge in --dir AND
             dZ = end.Z - pre.Z is >= 0.75 * rise up (rise) / down (drop)

It prints one TSV verdict line plus a per-100 ms table of the walk (Z, Y), and
exits 0 PASS, 1 FAIL, 2 no verdict (no rows in the window).
"""
import argparse
import csv
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pose", required=True)
    ap.add_argument("--keys", required=True)
    ap.add_argument("--edge-y", type=float, required=True)
    ap.add_argument("--dir", type=int, choices=(-1, 1), required=True)
    ap.add_argument("--rise", type=float, required=True)
    ap.add_argument("--leg", choices=("rise", "drop"), required=True)
    a = ap.parse_args()

    keys = {}
    with open(a.keys) as f:
        for line in f:
            k, v = line.split("\t")
            keys[k] = int(v)
    t_down, t_up = keys["keydown_ms"], keys["keyup_ms"]

    rows = []
    try:
        with open(a.pose, newline="") as f:
            for r in csv.DictReader(f, delimiter="\t"):
                try:
                    rows.append((int(r["ts_unix_ms"]), float(r["playerX"]),
                                 float(r["playerY"]), float(r["playerZ"])))
                except (KeyError, ValueError):
                    continue
    except OSError:
        pass
    before = [r for r in rows if r[0] < t_down]
    after = [r for r in rows if r[0] > t_down]
    if not before or not after:
        print(f"{a.leg}\tNO-VERDICT\trows={len(rows)} before={len(before)} after={len(after)}")
        return 2
    b, e = before[-1], after[-1]
    short = [r for r in after if (r[2] - a.edge_y) * a.dir <= -0.5]
    pre = short[-1] if short else b
    dz = e[3] - pre[3]
    past = (e[2] - a.edge_y) * a.dir
    want_dz = 0.75 * a.rise
    z_ok = dz >= want_dz if a.leg == "rise" else dz <= -want_dz
    ok = z_ok and past >= 2.0
    print(f"{a.leg}\t{'PASS' if ok else 'FAIL'}\tstart=({b[1]:.2f},{b[2]:.2f},{b[3]:.2f})"
          f"\tpre=({pre[1]:.2f},{pre[2]:.2f},{pre[3]:.2f})\tend=({e[1]:.2f},{e[2]:.2f},{e[3]:.2f})\tdZ={dz:+.2f}\tpast_edge={past:+.2f}"
          f"\tedge_y={a.edge_y}\trise={a.rise}\trows_in_walk={sum(1 for r in rows if t_down <= r[0] <= t_up)}")
    # the walk, one row per 100 ms from keydown - 200 ms to keyup + 500 ms
    print("  t_ms_from_keydown\tplayerX\tplayerY\tplayerZ")
    nxt = t_down - 200
    for r in rows:
        if r[0] < nxt or r[0] > t_up + 500:
            continue
        print(f"  {r[0] - t_down}\t{r[1]:.2f}\t{r[2]:.2f}\t{r[3]:.2f}")
        nxt = r[0] + 100
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
