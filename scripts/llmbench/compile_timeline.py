#!/usr/bin/env python3
"""T492 Leg A: compile ONE timeline into the per-player drive files.

The timeline (scripts/llmbench/timeline*.tsv) is the single source of the
recording.  Every row is one STEP:

    t_offset <TAB> player <TAB> action <TAB> truth k=v;k=v;...

  t_offset  seconds after the driver's T0 (T0 = the moment every client is in
            world; see record.sh).  Rows must be sorted.
  player    p1 | p2 | p3 -- who ACTS on this step.
  action    p1 (official, driven live by record.sh):
              cmd <text>        type <text> into the chat line
              key <keyspec>...  xdotool keys
              hold <key> <sec>
              video <label> <sec>
              nop               act on nothing, just capture
            p2/p3 (willeq, driven by --script): one slash-command line, or
              nop
  truth     the ground truth for the step, WRITTEN BY THE DRIVER FROM WHAT IT
            COMMANDED.  `label=` names the step (frame filenames); `expect=`
            is match | discrepancy:B### | unlabelled and is added by the
            executor, never by a model.

EVERY step yields a frame for EVERY player: p1 gets a window+root pair from
record.sh, p2 and p3 get a /screenshot from their own client.  That is what
makes a "paired official frame" available for every match/discrepancy step.

Outputs (under <outdir>):
    players/p2/script.txt, players/p3/script.txt   willeq --script files
    steps.tsv                                      the driver's step table
    steps.json                                     the same, for the checker
"""
import argparse, json, os, sys

WILLEQ_PLAYERS = ("p2", "p3")


def parse_truth(s):
    d = {}
    for part in s.split(";"):
        part = part.strip()
        if not part:
            continue
        k, _, v = part.partition("=")
        d[k.strip()] = v.strip()
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("timeline")
    ap.add_argument("outdir")
    ap.add_argument("--lead", type=float, default=120.0,
                    help="seconds of `wait` the willeq scripts burn before step 0, "
                         "so their command steps start after zone-in AND at the "
                         "driver's T0 (command steps are HELD until fully "
                         "connected; waits tick during connection)")
    ap.add_argument("--frames-root", default=None,
                    help="path the willeq client writes its frames under; "
                         "defaults to <outdir>. p3 runs on another host, so its "
                         "frames-root is that host's path, not this one's.")
    ap.add_argument("--p3-frames-root", default=None)
    ap.add_argument("--settle", type=float, default=0.0,
                    help="seconds of `wait` between a step's command and its "
                         "/screenshot. MEASURED on the Leg C corpus: with no "
                         "settle the screenshot is queued 17 ms after the "
                         "command and the renderer executes it 76 ms BEFORE the "
                         "camera change lands, so the frame is of the previous "
                         "camera. A camera step additionally gets a `/camera "
                         "get` in the gap, whose `[CAMERA-GET] mode=\"...\"` "
                         "reply is the client's OWN evidence, in its own log, "
                         "that the mode was in effect before the frame.")
    a = ap.parse_args()

    rows = []
    with open(a.timeline) as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 4:
                sys.exit(f"{a.timeline}:{n}: need 4 tab-separated fields, got {len(parts)}")
            t, player, action, truth = parts[0], parts[1], parts[2], parts[3]
            rows.append((float(t), player.strip(), action.strip(), parse_truth(truth)))

    for i in range(1, len(rows)):
        if rows[i][0] < rows[i - 1][0]:
            sys.exit(f"{a.timeline}: t_offset goes backwards at row {i+1}")

    froot = {"p2": a.frames_root or a.outdir,
             "p3": a.p3_frames_root or a.frames_root or a.outdir}

    steps = []
    for idx, (t, player, action, truth) in enumerate(rows):
        label = truth.get("label") or f"step{idx:03d}"
        steps.append({"idx": idx, "t_offset": t, "player": player,
                      "action": action, "label": label, "truth": truth})

    os.makedirs(a.outdir, exist_ok=True)

    # --- the two willeq --script files ------------------------------------
    for p in WILLEQ_PLAYERS:
        d = os.path.join(a.outdir, "players", p)
        os.makedirs(os.path.join(d, "frames"), exist_ok=True)
        out = [
            f"# T492 generated from {os.path.basename(a.timeline)} for {p} -- DO NOT EDIT BY HAND.",
            "# Command steps are held until fully zoned in; the waits below tick",
            "# during connection, so the lead wait is what puts step 0 at the",
            "# driver's T0.  Every step ends in a /screenshot, so the client's own",
            "# 'Screenshot saved:' INFO line timestamps the frame in run.log.",
            f"wait {a.lead:g}",
        ]
        # `prev` is where the PREVIOUS step's block ENDED, not where it began:
        # the settle sits between a step's command and its frame, so it is part
        # of that step's block and must come out of the gap to the next one.
        # Without this the schedule slips by one settle per step -- 3 s x 76
        # steps is nearly four minutes by the end of a segment, and the three
        # players stop being contemporaneous.
        prev = 0.0
        for s in steps:
            dt = s["t_offset"] - prev
            if dt > 0:
                out.append(f"wait {dt:g}")
            prev = s["t_offset"] + a.settle
            acted = s["player"] == p and s["action"] != "nop"
            if acted:
                out.append(s["action"])
            if a.settle > 0:
                out.append(f"wait {a.settle:g}")
                if "camera" in s["truth"]:
                    out.append("/camera get")
            shot = f"{froot[p]}/players/{p}/frames/{s['idx']:03d}_{s['label']}_window.png"
            out.append(f"/screenshot {shot}")
        out += ["wait 5", "/camp", "wait 20", "/quit"]
        with open(os.path.join(d, "script.txt"), "w") as f:
            f.write("\n".join(out) + "\n")

    # --- the driver's step table ------------------------------------------
    with open(os.path.join(a.outdir, "steps.tsv"), "w") as f:
        for s in steps:
            f.write("\t".join([str(s["idx"]), f"{s['t_offset']:g}", s["player"],
                               s["label"], s["action"],
                               ";".join(f"{k}={v}" for k, v in s["truth"].items())]) + "\n")
    with open(os.path.join(a.outdir, "steps.json"), "w") as f:
        json.dump({"lead": a.lead, "steps": steps}, f, indent=1)

    print(f"{len(steps)} steps; "
          + "; ".join(f"{p}={sum(1 for s in steps if s['player']==p)} own actions"
                      for p in ("p1", "p2", "p3")))


if __name__ == "__main__":
    main()
