#!/usr/bin/env python3
"""T492 Leg F step 3 -- put back the expect= labels that were parked in pending=.

When Will's review found the official client in the wrong camera on the
B721/B722/B725 sheets, those steps were moved to `expect=unlabelled` with the
board number parked in `pending=`, so the grader would ignore a pair nobody
could check.  Leg F re-recorded them with the official camera driven closed
loop and VERIFIED by score, so the claim is checkable again and the label goes
back.  Only rows that carry `pending=` are touched; idempotent.

    restore_pending.py <corpus>
"""
import os, re, sys

corpus = sys.argv[1]
for p in ("p1", "p2", "p3"):
    path = os.path.join(corpus, "players", p, "truth.tsv")
    out, n = [], 0
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        m = re.search(r"pending=(B\d{3})", line)
        if m and "expect=unlabelled" in line:
            line = line.replace("expect=unlabelled", f"expect=discrepancy:{m.group(1)}")
            line = re.sub(r";?pending=B\d{3}", "", line)
            n += 1
        out.append(line)
    open(path, "w").write("\n".join(out) + "\n")
    print(f"{p}: {n} rows restored from pending= to expect=discrepancy:B###")
