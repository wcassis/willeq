#!/usr/bin/env python3
"""T492: `expect=match` only where the FRAME can show the claim.

Leg D labelled every step taken under a fitted camera `match`, including
say/tell/who/loc/camget steps.  Those steps' claim is about chat text, which the
re-recorded willeq frames cannot show (/hideui, B791) and which the corpus
grades from chat.tsv / run.log anyway.  Will could not tell what such a pair
was asking him to compare.  This keeps `match` on camera and target steps
(the frame IS the claim) and moves the rest to `unlabelled` with
`graded_from=log`.  Idempotent; rewrites truth.tsv in place.
"""
import re, sys, os
FRAME_KINDS = ("cam",)          # A_cam_*, D_cam_*, H_cam_*
TARGET_KINDS = ("target",)
def kind(label):
    m = re.match(r"[A-Z]_([a-z]+)", label)
    return m.group(1) if m else ""
corpus = sys.argv[1]
for p in ("p1", "p2", "p3"):
    path = os.path.join(corpus, "players", p, "truth.tsv")
    out, moved = [], 0
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        if "expect=match" in line:
            m = re.search(r"label=([^;]+)", line)
            k = kind(m.group(1)) if m else ""
            if not (k.startswith(FRAME_KINDS) or k.startswith(TARGET_KINDS)):
                line = line.replace("expect=match", "expect=unlabelled;graded_from=log")
                moved += 1
        out.append(line)
    open(path, "w").write("\n".join(out) + "\n")
    print(f"{p}: {moved} match -> unlabelled(graded_from=log)")
