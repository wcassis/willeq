#!/usr/bin/env python3
"""T492 Leg E: a SUPPLEMENTARY timeline that re-records a subset of a recorded
corpus's steps, keeping their step ids, labels and truth exactly.

Will's ruling after looking at the sheets: the camera and held-item steps are
to be re-recorded with (a) a settle between the command and the frame and
(b) the willeq debug overlay off.  Everything else in the corpus stands, so the
segment must reproduce the selected steps IDENTICALLY except for those two
things -- same order, same labels, same `expect=`, same `truth` -- so its
frames can replace the originals step for step.

Selection: every p2/p3 step whose truth carries `camera=` or `held=`, plus the
p1 steps that carry `camera=`, `held_visual=` or `f9_presses=` (the official
half of the same blocks), so the pairs stay paired.

    make_timeline_segment.py <steps.json> <out.tsv>
"""
import json, re, sys
from collections import Counter

SETTLE_ROWS = [
    # The willeq debug overlay is the chat window carrying every renderer
    # command reply (`Screenshot saved:`, `[CAMERA-GET]`, combat lines) -- the
    # official client never shows those.  `/hideui` is the scriptable half of
    # F10 (T301 Leg D; the official client's F10 = Toggle UI Display, B582) and
    # is what T478 used for exactly this reason.  p1's own
    # UI is left alone: it is the official client's real UI, not a debug
    # overlay, and hiding it would also blind the chat OCR that shows the GM
    # commands landing.
    ("p2", "/hideui", "label=E0_hideui_p2;ui=hidden;expect=unlabelled"),
    ("p3", "/hideui", "label=E0_hideui_p3;ui=hidden;expect=unlabelled"),
]
ZONE_ROWS = [
    ("p1", "cmd #movechar Testcaster qeytoqrg|",
     "label=E0_zone_p2;zoning=Testcaster -> qeytoqrg by the GM's #movechar;expect=unlabelled"),
    ("p1", "cmd #movechar Testbuddy qeytoqrg|",
     "label=E0_zone_p3;zoning=Testbuddy -> qeytoqrg by the GM's #movechar;expect=unlabelled"),
    ("p1", "cmd #zone qeytoqrg|",
     "label=E0_zone_p1;zone=qeytoqrg;zoning=Oraclealpha -> qeytoqrg by the GM's #zone;expect=unlabelled"),
    ("p2", "/hideui", "label=E0_hideui2_p2;ui=hidden after the zone;expect=unlabelled"),
    ("p3", "/hideui", "label=E0_hideui2_p3;ui=hidden after the zone;expect=unlabelled"),
]

GAP_P1_CMD, GAP_P1_KEY, GAP_WILLEQ = 20, 11, 8


def gap(player, action):
    if player != "p1":
        return GAP_WILLEQ
    if action.startswith("cmd"):
        return GAP_P1_CMD
    return GAP_P1_KEY


def main():
    steps = json.load(open(sys.argv[1]))["steps"]
    out = sys.argv[2]
    sel = [s for s in steps
           if (s["player"] in ("p2", "p3") and ("camera" in s["truth"] or "held" in s["truth"]))
           or (s["player"] == "p1" and any(k in s["truth"] for k in ("camera", "held_visual", "f9_presses")))]
    qeynos = [s for s in sel if s["truth"].get("zone", "qeynos2") != "qeytoqrg"]
    qeyto = [s for s in sel if s["truth"].get("zone") == "qeytoqrg"]

    rows, t = [], 0.0

    def emit(player, action, truth, keep_id=None):
        nonlocal t
        tr = dict(truth) if isinstance(truth, dict) else None
        if tr is None:
            body = truth
        else:
            # A camera step gets `/camera get` before its frame, so the client's
            # OWN reply (`[CAMERA-GET] mode="..."`) lands in the log between the
            # command and the screenshot and proves the mode was in effect.
            body = ";".join(f"{k}={v}" for k, v in tr.items())
        if keep_id is not None:
            body += f";replaces_step={keep_id}"
        rows.append((round(t, 1), player, action, body))
        t += gap(player, action)

    for p, a, tr in SETTLE_ROWS:
        emit(p, a, tr)
    t += 6
    for s in qeynos:
        emit(s["player"], s["action"], s["truth"], s["idx"])
    t += 6
    for p, a, tr in ZONE_ROWS:
        emit(p, a, tr)
    t += 12
    for s in qeyto:
        emit(s["player"], s["action"], s["truth"], s["idx"])

    hdr = """# T492 Leg E -- SUPPLEMENTARY timeline: the camera and held-item steps only,
# re-recorded after Will looked at the sheets.  Two things differ from the
# Leg C recording of the same steps and NOTHING else does:
#   1. a settle between the command and the frame.  MEASURED on the Leg C
#      corpus: `/camera firstperson` was queued at 15:21:56.500, the renderer
#      applied it at 15:21:56.593 (`Camera mode: First Person Camera`), and the
#      `/screenshot` queued 17 ms after the command executed at 15:21:56.517 --
#      76 ms BEFORE the camera moved.  The command was never the problem.
#   2. `/hideui` on the two willeq clients, so their frames do not carry the
#      chat window full of renderer command replies.
# `replaces_step=` names the Leg C step id each row's frames replace.
"""
    with open(out, "w") as f:
        f.write(hdr + "".join(f"{a}\t{b}\t{c}\t{d}\n" for a, b, c, d in rows))
    per = Counter(r[1] for r in rows)
    exp = Counter(re.search(r"expect=([^;]*)", r[3]).group(1) for r in rows)
    print(f"{out}: {len(rows)} rows, {rows[-1][0]/60:.1f} min; per player {dict(per)}")
    print(f"  replaces {len(sel)} Leg C steps; expect {dict(exp)}")


if __name__ == "__main__":
    main()
