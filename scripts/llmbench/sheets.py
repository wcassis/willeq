#!/usr/bin/env python3
"""T492 Leg D: contact sheets of every labelled step pair, for Will's bless.

One tile row per labelled step: the OFFICIAL window frame beside the willeq
window frame from the same step, captioned with the step label, the timestamp
of each frame and the `expect=` label.  That is all -- the sheet exists so a
HUMAN can look at the pair.  Nothing here scores, ranks or describes a frame,
and no model looks at one.

    sheets.py <corpus-dir> <out-dir> [--per-sheet 6]
"""
import argparse, json, os, re, subprocess, sys, glob
from collections import defaultdict

FRAME = re.compile(r"^(\d{2}:\d{2}:\d{2}\.\d{3})_(.+?)_(window|root)\.png$")


def index(pdir):
    out = {}
    for f in glob.glob(os.path.join(pdir, "frames", "*_window.png")):
        m = FRAME.match(os.path.basename(f))
        if m:
            out[m.group(2)] = (m.group(1), f)
    return out


def truth_by_step(pdir):
    d = {}
    path = os.path.join(pdir, "truth.tsv")
    if not os.path.exists(path):
        return d
    for line in open(path):
        t, _, body = line.rstrip("\n").partition("\t")
        kv = dict(p.split("=", 1) for p in body.split(";") if "=" in p)
        if "label" in kv:
            d[kv["label"]] = (t, kv)
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("corpus")
    ap.add_argument("outdir")
    ap.add_argument("--per-sheet", type=int, default=6)
    a = ap.parse_args()
    man = json.load(open(os.path.join(a.corpus, "manifest.json")))
    official = next(p["id"] for p in man["players"] if p["client"] == "official")
    off_frames = index(os.path.join(a.corpus, "players", official))
    os.makedirs(a.outdir, exist_ok=True)

    tiles = []
    for p in man["players"]:
        if p["id"] == official:
            continue
        pdir = os.path.join(a.corpus, "players", p["id"])
        frames = index(pdir)
        for label, (t, kv) in sorted(truth_by_step(pdir).items()):
            e = kv.get("expect", "unlabelled")
            if e == "unlabelled":
                continue
            if label not in frames or label not in off_frames:
                continue
            ot, of = off_frames[label]
            wt, wf = frames[label]
            tiles.append((e, label, p["id"], of, ot, wf, wt, kv))

    tiles.sort(key=lambda x: (x[0], x[1], x[2]))
    groups = defaultdict(list)
    for tile in tiles:
        groups[tile[0]].append(tile)

    made = []
    for e, items in groups.items():
        safe = e.replace(":", "_")
        for n in range(0, len(items), a.per_sheet):
            chunk = items[n:n + a.per_sheet]
            rows = []
            for i, (_, label, pid, of, ot, wf, wt, kv) in enumerate(chunk):
                cap = (f"{label}  [{pid}]  expect={e}\\n"
                       f"official {ot}   |   willeq {wt}\\n"
                       + "  ".join(f"{k}={v}" for k, v in kv.items()
                                   if k in ("zone", "camera", "held", "target", "sky", "trees")))
                row = os.path.join(a.outdir, f".row_{safe}_{n}_{i}.png")
                subprocess.run(["montage", of, wf, "-tile", "2x1", "-geometry", "+4+4",
                                "-background", "black", "-label", cap, "-pointsize", "13",
                                "-fill", "white", row], check=True)
                rows.append(row)
            out = os.path.join(a.outdir, f"T492_{safe}_{n//a.per_sheet+1}.png")
            subprocess.run(["montage", *rows, "-tile", "1x", "-geometry", "+2+2",
                            "-background", "black", out], check=True)
            for r in rows:
                os.remove(r)
            made.append(out)
            print(f"{out}: {len(chunk)} step pairs, expect={e}")
    if not made:
        print("no labelled step had BOTH an official and a willeq frame", file=sys.stderr)
        return 1
    print(f"{len(made)} sheets, {len(tiles)} labelled step pairs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
