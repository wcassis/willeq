#!/usr/bin/env python3
"""T492 Leg E: replace a recorded corpus's steps with a supplementary segment's.

The segment re-recorded a subset of the corpus's steps (same labels, same
truth, same order) with a settle and the debug overlay off.  Each segment step
names the corpus step it replaces in its truth as `replaces_step=<idx>`.

For every replaced step this moves the corpus's OLD frames aside (they are the
evidence for why the segment exists), copies the segment's frames in, swaps the
truth line, and merges the segment's marks in so the timestamp join still
holds.  Nothing else in the corpus is touched.  No frame is looked at.

    merge_segment.py <corpus> <segment> <attic> [--dry-run]
"""
import argparse, json, os, re, shutil, sys
from collections import defaultdict

FRAME = re.compile(r"^(\d{2}:\d{2}:\d{2}\.\d{3})_(.+?)_(window|root)\.png$")


def kv(body):
    return dict(p.split("=", 1) for p in body.split(";") if "=" in p)


def read_tsv(p):
    if not os.path.exists(p):
        return []
    return [tuple(l.rstrip("\n").split("\t", 1)) for l in open(p, errors="replace") if l.strip()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("corpus"); ap.add_argument("segment"); ap.add_argument("attic")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    seg_steps = json.load(open(os.path.join(a.segment, "steps.json")))["steps"]
    repl = {}           # corpus idx -> (segment idx, label)
    for s in seg_steps:
        r = s["truth"].get("replaces_step")
        if r is not None:
            repl[int(r)] = (s["idx"], s["label"])
    cor_steps = {s["idx"]: s for s in json.load(open(os.path.join(a.corpus, "steps.json")))["steps"]}
    print(f"{len(repl)} corpus steps are replaced by segment steps")

    players = [p["id"] for p in json.load(open(os.path.join(a.corpus, "manifest.json")))["players"]]
    moved = copied = 0
    for pid in players:
        cdir = os.path.join(a.corpus, "players", pid)
        sdir = os.path.join(a.segment, "players", pid)
        adir = os.path.join(a.attic, "players", pid, "frames")
        os.makedirs(adir, exist_ok=True)

        # index both sides' frames by label
        def index(d):
            out = defaultdict(list)
            for f in os.listdir(os.path.join(d, "frames")):
                m = FRAME.match(f)
                if m:
                    out[m.group(2)].append((m.group(1), f))
            return out
        ci, si = index(cdir), index(sdir)

        # The pre-merge text files go to the attic too, not just the frames --
        # otherwise the old truth lines exist nowhere after the rewrite.
        if not a.dry_run:
            for name in ("truth.tsv", "marks.tsv", "chat.tsv"):
                src = os.path.join(cdir, name)
                if os.path.exists(src):
                    shutil.copy2(src, os.path.join(a.attic, "players", pid, f"pre_merge_{name}"))

        # the corpus truth lines we are replacing, by label
        old_truth = read_tsv(os.path.join(cdir, "truth.tsv"))
        labels = {lab for _, lab in repl.values()}
        kept, replaced_labels = [], set()
        for t, body in old_truth:
            d = kv(body)
            lab = d.get("label")
            if lab in labels and int(d.get("step", -1)) in repl:
                replaced_labels.add(lab)
                continue
            kept.append((t, body))
        # the segment's truth lines for those steps, with the CORPUS step id
        seg_truth = read_tsv(os.path.join(sdir, "truth.tsv"))
        new = []
        for t, body in seg_truth:
            d = kv(body)
            lab = d.get("label")
            if lab not in labels:
                continue
            d.pop("replaces_step", None)
            corpus_idx = next(i for i, (_, l) in repl.items() if l == lab)
            d["step"] = str(corpus_idx)
            d["rerecorded"] = "T492 Leg E (settle + /hideui)"
            new.append((t, ";".join(f"{k}={v}" for k, v in d.items())))

        # frames: corpus's old ones to the attic, segment's in
        for lab in replaced_labels:
            for ts, f in ci.get(lab, []):
                if not a.dry_run:
                    shutil.move(os.path.join(cdir, "frames", f), os.path.join(adir, f))
                moved += 1
            for ts, f in si.get(lab, []):
                if not a.dry_run:
                    shutil.copy2(os.path.join(sdir, "frames", f), os.path.join(cdir, "frames", f))
                copied += 1

        if not a.dry_run:
            with open(os.path.join(cdir, "truth.tsv"), "w") as fh:
                for t, b in sorted(kept + new):
                    fh.write(f"{t}\t{b}\n")
            # marks and chat: the segment's are APPENDED, never merged into a
            # line, so both recordings' marks stay readable side by side and the
            # timestamp join keeps working for either time range.
            for name in ("marks.tsv", "chat.tsv"):
                rows = read_tsv(os.path.join(cdir, name)) + read_tsv(os.path.join(sdir, name))
                with open(os.path.join(cdir, name), "w") as fh:
                    for t, b in sorted(rows):
                        fh.write(f"{t}\t{b}\n")
            for src, dst in ((os.path.join(sdir, "run.log"), os.path.join(cdir, "run_legE.log")),
                             (os.path.join(sdir, "script.txt"), os.path.join(cdir, "script_legE.txt"))):
                if os.path.exists(src):
                    shutil.copy2(src, dst)
        print(f"  {pid}: {len(replaced_labels)} labels replaced, "
              f"{len(kept)} truth lines kept, {len(new)} swapped in")

    print(f"frames: {moved} moved to the attic, {copied} copied in"
          + (" (DRY RUN)" if a.dry_run else ""))


if __name__ == "__main__":
    main()
