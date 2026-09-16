#!/usr/bin/env python3
"""T492 Leg G -- write verify_labels.py's verdicts back onto the truth rows.

A grading label is a claim; a claim that was CONTRADICTED by measurement must
not carry one, and must say why.  This adds `why=` to every row whose pair
failed, and `unverified=` to every row whose pair could not be checked.  It
never invents a label: a row that is already `expect=unlabelled` stays that way,
and nothing is promoted to `match` or `discrepancy` here (that is
restore_pending.py's job, and only for pairs that PASS).

    apply_verdicts.py <corpus> [--dry-run]
"""
import csv, os, re, sys

corpus = sys.argv[1]
dry = "--dry-run" in sys.argv
rep = os.path.join(corpus, "verify_labels.tsv")
verdict, reason = {}, {}
with open(rep) as f:
    for r in csv.DictReader(f, delimiter="\t"):
        verdict[r["label"]] = r["result"]
        reason[r["label"]] = r["measured"]


def short(m):
    bits = []
    for part in m.split("; "):
        if "[FAIL]" in part or "[?]" in part:
            bits.append(part.replace(" [FAIL]", "").replace(" [?]", ""))
    s = "; ".join(bits) or m
    return re.sub(r"[;\t\n]+", " ", s)[:150]


changed = 0
for p in ("p1", "p2", "p3"):
    path = os.path.join(corpus, "players", p, "truth.tsv")
    out = []
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        m = re.search(r"label=([^;\t]+)", line)
        lab = m.group(1) if m else None
        v = verdict.get(lab)
        if v in ("FAIL", "INCONCLUSIVE") and "why=" not in line and "unverified=" not in line:
            key = "why" if v == "FAIL" else "unverified"
            line += f";{key}={short(reason[lab])}"
            changed += 1
        out.append(line)
    if not dry:
        open(path, "w").write("\n".join(out) + "\n")
print(f"{changed} truth rows annotated" + (" (DRY RUN)" if dry else ""))
