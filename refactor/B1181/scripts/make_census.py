#!/usr/bin/env python3
"""B1181 fix leg (t_613a8781): the acceptance-B byte-identity census.

Reads the rendered frame dirs (base = pre-B1181 merge-base ac8c26bc6, tip =
MR head) from the evidence copy, md5s every frame, and writes
  b1181_gate_md5.tsv      frame  base_md5  tip_md5  identical
  gate_frames_manifest.tsv  (per-frame md5 manifest of every frame dir)
plus the identical/total counts to stdout.
"""
import hashlib, os, sys

EV = "/home/user/kb-evidence/kb-b1181-item-missile"
FR = os.path.join(EV, "gate_frames")
ROWS = os.path.join(EV, "gate_census/rows.tsv")
OUT = os.path.join(EV, "b1181_gate_md5.tsv")
MAN = os.path.join(EV, "gate_frames_manifest.tsv")

tags = []
with open(ROWS) as f:
    for line in f:
        p = line.rstrip("\n").split("\t")
        if p:
            tags.append(p[0])

def md5(p):
    h = hashlib.md5()
    with open(p, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

frames_by = {}
for tree in ("base", "tip"):
    for tag in tags:
        d = os.path.join(FR, tree, tag)
        if not os.path.isdir(d):
            print(f"MISSING DIR {tree}/{tag}", file=sys.stderr)
            continue
        fs = sorted(f for f in os.listdir(d) if f.endswith(".png"))
        frames_by[(tree, tag)] = fs

n = n_ident = 0
missing = 0
with open(OUT, "w") as out, open(MAN, "w") as man:
    out.write("tag\tframe\tmd5_base\tmd5_tip\tidentical\n")
    man.write("tree\ttag\tframe\tmd5\n")
    for tag in tags:
        fb = frames_by.get(("base", tag), [])
        ft = frames_by.get(("tip", tag), [])
        nf = max(len(fb), len(ft))
        for i in range(nf):
            if i < len(fb) and i < len(ft):
                assert fb[i] == ft[i], f"frame name mismatch {tag} {i}: {fb[i]} vs {ft[i]}"
                f = fb[i]
            elif i < len(fb):
                f = fb[i]
            else:
                f = ft[i]
            mb = md5(os.path.join(FR, "base", tag, fb[i])) if i < len(fb) else ""
            mt = md5(os.path.join(FR, "tip", tag, ft[i])) if i < len(ft) else ""
            if i < len(fb):
                man.write(f"base\t{tag}\t{fb[i]}\t{mb}\n")
            if i < len(ft):
                man.write(f"tip\t{tag}\t{ft[i]}\t{mt}\n")
            same = "y" if (mb and mt and mb == mt) else "n"
            out.write(f"{tag}\t{f}\t{mb}\t{mt}\t{same}\n")
            n += 1
            n_ident += same == "y"
            if not mb or not mt:
                missing += 1

print(f"rows={n} identical={n_ident} differing={n - n_ident} missing_side={missing}")
print(f"tags base={sum(1 for t in tags if ('base', t) in frames_by)} tip={sum(1 for t in tags if ('tip', t) in frames_by)}")
