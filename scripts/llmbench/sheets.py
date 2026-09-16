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



BOARD = {
    "B721": "B721 Chase framing: official figure larger/closer than fable's (T478 STOP). Weigh: how the own model is framed.",
    "B722": "B722 held items (set A: short sword + lantern): compare presence + orientation of what is in each hand.",
    "B723": "B723 set B: fable draws a kite shield on the left arm, official shows no shield.",
    "B724": "B724 set C: the 2H sword is held differently (official roughly horizontal).",
    "B725": "B725 IT10 arrow model held along a different axis.",
    "B781": "B781 sky: official dusk gradient vs fable's time-of-day tint (now B789: wrong, time-varying colour).",
    "B782": "B782 distant trees pale/untextured in fable (now also B790: pink untextured cards).",
}
def claim(e, kv):
    ign = "  IGNORE everything else (sky colour, pink trees, UI, nametags, lighting) -- boarded, unscored."
    if e == "match":
        if "target" in kv:
            return f"CLAIM: both clients have {kv['target']} targeted in {kv.get('zone','?')}. Weigh: same entity singled out." + ign
        return (f"CLAIM: both clients are in the {kv.get('camera','?')} camera in {kv.get('zone','?')}. "
                f"Weigh the VIEWPOINT only: eye height, distance, whether the own model is drawn." + ign)
    b = e.split(":", 1)[1] if ":" in e else e
    extra = f"  held={kv['held']}" if "held" in kv else ""
    return "CLAIM (discrepancy): " + BOARD.get(b, b) + extra + ign

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
                cap = (f"OFFICIAL (left) vs WILLEQ (right)   step {label}  [{pid}]   expect={e}\n"
                       f"official {ot}   |   willeq {wt}\n"
                       + "  ".join(f"{k}={v}" for k, v in kv.items()
                                   if k in ("zone", "camera", "held", "target", "sky", "trees"))
                       + ("\n(willeq UI hidden in this step, B791)" if "rerecorded" in kv else "")
                       + "\n" + claim(e, kv))
                row = os.path.join(a.outdir, f".row_{safe}_{n}_{i}.png")
                pair = os.path.join(a.outdir, f".pair_{safe}_{n}_{i}.png")
                subprocess.run(["montage", of, wf, "-tile", "2x1", "-geometry", "+4+4",
                                "-background", "black", pair], check=True)
                # A caption STRIP under the pair.  (`montage -label` only applies to
                # inputs that FOLLOW it, so the first version captioned nothing --
                # Will could not find the labels.)
                width = subprocess.run(["identify", "-format", "%w", pair], capture_output=True,
                                       text=True, check=True).stdout.strip()
                subprocess.run(["convert", pair, "-background", "#202020", "-fill", "white",
                                "-pointsize", "22", "-gravity", "west", "-size", f"{width}x",
                                "caption:" + cap.replace("\\n", "\n"),
                                "-append", row], check=True)
                os.remove(pair)
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
