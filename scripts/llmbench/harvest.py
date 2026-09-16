#!/usr/bin/env python3
"""T492 Leg A: turn one willeq client's run.log into its corpus half.

The client names its own frames (`/screenshot <dir>/<idx>_<label>_window.png`)
and logs a `Screenshot saved:` line when the renderer has actually written
them, so the log is the authority for WHEN each step happened.  This renames
each frame to the handoff's `<HHMMSS.mmm>_<label>_window.png` and writes the
marks/truth/chat files keyed on that same timestamp.

`truth.tsv`'s k=v come from the TIMELINE -- what the driver commanded -- and
never from anything read back out of a frame.  Only the timestamp comes from
the log.

p3 runs on another host.  --clock-offset is that host's clock minus the dev
host's, measured in Leg 0; every timestamp this writes for p3 is shifted by
-offset so the whole corpus joins on ONE clock (the dev host's).  run.log is
left exactly as the client wrote it.
"""
import argparse, json, os, re, shutil, sys
from datetime import datetime, timedelta

LOG = re.compile(r"^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\] \[(\w+)\s*\] \[(\w+)\s*\] (.*)$")
SHOT = re.compile(r"Screenshot saved: (\S+)")
EXEC = re.compile(r"Script: executing '(.*)' \(step (\d+)/(\d+)\)")
FRAME = re.compile(r"^(\d{3})_(.*)_window\.png$")
# What an OBSERVING client logs when another player's chat reaches it.  Kept
# wide on purpose and reported verbatim: the assertion is "the observer saw
# it", so the observer's own words are the evidence.
CHATISH = re.compile(r"\b(says|tells you|shouts|auctions|Chat|CHAT|tell|say)\b")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--steps", required=True)
    ap.add_argument("--player", required=True)
    ap.add_argument("--player-dir", required=True)
    ap.add_argument("--clock-offset", type=float, default=0.0)
    a = ap.parse_args()

    steps = {s["idx"]: s for s in json.load(open(a.steps))["steps"]}
    pdir = a.player_dir
    runlog = os.path.join(pdir, "run.log")
    frames = os.path.join(pdir, "frames")
    if not os.path.exists(runlog):
        sys.exit(f"no run.log at {runlog}")

    def stamp(s):
        t = datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f") - timedelta(seconds=a.clock_offset)
        return t.strftime("%H:%M:%S.") + f"{t.microsecond//1000:03d}"

    marks, chat = [], []
    shot_ts = {}   # idx -> HH:MM:SS.mmm
    with open(runlog, errors="replace") as f:
        for line in f:
            m = LOG.match(line.rstrip("\n"))
            if not m:
                continue
            ts, _lvl, _mod, body = m.groups()
            t = stamp(ts)
            e = EXEC.search(body)
            if e:
                marks.append((t, f"cmd {e.group(1)}"))
                continue
            s = SHOT.search(body)
            if s:
                base = os.path.basename(s.group(1))
                fm = FRAME.match(base)
                if fm:
                    idx = int(fm.group(1))
                    shot_ts[idx] = t
                    marks.append((t, f"shot {fm.group(2)}"))
                continue
            if CHATISH.search(body):
                chat.append((t, body))

    # --- rename the frames the client wrote, and drop a truth line per step --
    renamed = missing = 0
    truth = []
    for idx in sorted(steps):
        s = steps[idx]
        src = os.path.join(frames, f"{idx:03d}_{s['label']}_window.png")
        t = shot_ts.get(idx)
        if t is None or not os.path.exists(src):
            missing += 1
            continue
        dst = os.path.join(frames, f"{t}_{s['label']}_window.png")
        shutil.move(src, dst)
        renamed += 1
        kv = dict(s["truth"])
        kv.setdefault("actor", s["player"])
        kv["step"] = str(idx)
        truth.append((t, ";".join(f"{k}={v}" for k, v in kv.items())))

    def write(name, rows):
        with open(os.path.join(pdir, name), "w") as f:
            for t, v in sorted(rows):
                f.write(f"{t}\t{v}\n")
        return len(rows)

    n_m = write("marks.tsv", marks)
    n_t = write("truth.tsv", truth)
    n_c = write("chat.tsv", chat)
    print(f"{a.player}: {renamed} frames named, {missing} missing, "
          f"{n_m} marks, {n_t} truth lines, {n_c} chat-ish observer lines "
          f"(clock offset {a.clock_offset:+.3f}s applied)")


if __name__ == "__main__":
    main()
