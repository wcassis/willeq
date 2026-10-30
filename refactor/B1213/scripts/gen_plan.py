#!/usr/bin/env python3
"""B1213: generate the observer row-walk plan from row_layout.tsv.

The observer (Oraclewatch, a GM official client, second display :99 = the
grabbed video) walks the row: for each character it #gotoss to
(target + OFFSET, target + OFFSET) at HEADING, settles, and holds 20 s while
the continuous video runs. The fixed camera (F9 x3) is set once, on the first
character, and persists (it does not follow heading -- the target is centred
by the offset, not by rotation).

Camera recipe (calibrated on FxR01, the Fiery Avenger positive control, in
refactor/B1213/calib3): observer at target + (1.5, 1.5), heading 320, F9 x3
-> the character is centred (~0.48 frame width) at ~1/3 frame height with its
worn-item effect clearly visible.

Output: a plan.tsv the committed oracle driver
(scripts/eqemu/oracle/oracle_remote.sh run <dir> <lane>) consumes.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent  # refactor/B1213
TABLE = ROOT / "tables" / "row_layout.tsv"
OUT = ROOT / "plans" / "walk_plan.tsv"

# Camera recipe (calibrated).
OFFSET = 1.5          # observer = target + (OFFSET, OFFSET)
HEADING = 320         # EQ heading units (225 deg): observer faces the row
CAM_KEYS = 3          # F9 x3 = the fixed camera
SETTLE_MS = 3000      # settle after each #goto
HOLD_MS = 20000       # 20 s hold per character (spec)
Z = 3.128             # row ground z
MAIN_GOTO = "#goto -300 250 3 160"  # park the main client (Testcaster) off-view


def read_layout(path):
    rows = []
    for line in path.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        p = line.split("\t")
        if len(p) < 8 or p[0] == "row_index":
            continue
        rows.append({"idx": p[0], "char": p[1], "x": float(p[5]),
                     "y": float(p[6]), "note": p[7]})
    return rows


def main():
    rows = read_layout(TABLE)
    if not rows:
        sys.exit("no rows in %s" % TABLE)
    n = len(rows)
    L = []
    L.append("# action\ttarget\thold_ms\tstill_name")
    L.append("# B1213 row walk: observer (Oraclewatch, GM) walks %d characters," % n)
    L.append("# each at target+(%g,%g) heading %d, F9 x%d fixed camera, %d s hold."
             % (OFFSET, OFFSET, HEADING, CAM_KEYS, HOLD_MS // 1000))
    L.append("# fps=30")
    L.append("# observer=Oraclewatch")
    L.append("# obs-ini-set=Defaults.LastCharSel=1")
    L.append("login\tTestcaster\t0\tchar_select")
    L.append("obs\tlogin\tOraclewatch\t0\tobs_char_select")
    L.append("zone\tqeynos2\t0\tin_world")
    L.append("obs\tzone\tqeynos2\t0\tobs_in_world")
    L.append("wait\t8000\t0\tsettle")
    L.append("obs\tclick\t1229,555\t0\tobs_close_tip")
    L.append("click\t1229,555\t0\tclose_tip")
    L.append("command\t%s\t0\ttgt_far" % MAIN_GOTO)
    for i, r in enumerate(rows):
        ox = r["x"] + OFFSET
        oy = r["y"] + OFFSET
        ii = "%02d" % i
        L.append("obs\tcommand\t#goto %g %g %g %d\t0\tgoto_%s" % (ox, oy, Z, HEADING, ii))
        L.append("obs\twait\t%d\t0\tsettle_%s" % (SETTLE_MS, ii))
        if i == 0:
            # set the fixed camera on the first character (persists for the rest)
            for k in range(CAM_KEYS):
                L.append("obs\tpress\tF9\t0\tcam_%s" % "abc"[k])
        L.append("obs\twait\t%d\t0\tstill_%s" % (HOLD_MS, ii))
    L.append("obs\tcommand\t/q\t0\tobs_quit")
    L.append("command\t/q\t0\tquit")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(L) + "\n")
    print("wrote %s (%d steps, %d characters)" % (OUT, len(L), n))


if __name__ == "__main__":
    main()
