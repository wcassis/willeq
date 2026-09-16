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
import csv, os, re, sys

corpus = sys.argv[1]
# T492 Leg H: a parked label goes back ONLY if verify_labels.py passed that pair.
# Restoring one the verifier could not confirm is how the corpus got into this
# state in the first place.
passed = set()
rep = os.path.join(corpus, "verify_labels.tsv")
if os.path.exists(rep):
    with open(rep) as f:
        passed = {r["label"] for r in csv.DictReader(f, delimiter="\t") if r["result"] == "PASS"}
    print(f"verifier PASS list: {len(passed)} labels")
else:
    sys.exit("no verify_labels.tsv -- run verify_labels.py first; refusing to promote blind")
for p in ("p1", "p2", "p3"):
    path = os.path.join(corpus, "players", p, "truth.tsv")
    out, n = [], 0
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        m = re.search(r"pending=(B\d{3}|match)", line)
        lm = re.search(r"label=([^;\t]+)", line)
        if m and "expect=unlabelled" in line and lm and lm.group(1) in passed:
            tag = "match" if m.group(1) == "match" else f"discrepancy:{m.group(1)}"
            line = line.replace("expect=unlabelled", f"expect={tag}")
            line = re.sub(r";?pending=(B\d{3}|match)", "", line)
            n += 1
        out.append(line)
    open(path, "w").write("\n".join(out) + "\n")
    print(f"{p}: {n} rows restored from pending= to expect=discrepancy:B###")
