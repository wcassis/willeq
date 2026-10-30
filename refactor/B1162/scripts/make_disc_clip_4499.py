#!/usr/bin/env python3
"""B1162 /disc record leg (card t_a74670e3): rebuild the b1162_disc4499_defensive
ledger clip (record 326, 4499 Defensive Discipline) with the willeq side at the
CURRENT fable tip, and regenerate the ledger index.

The oracle side is the EXISTING lane-5 capture (the B1162 /disc landing leg,
review/B1162/worker/oracle/b1162_disc4499_v3) -- no re-capture: the card
re-captures only if the capture is unfit, and it is not (the verdicts are all
packet facts + the lit-run anatomy, both already measured for it; the capture
carries the S->C OP_Action epoch the whole clip windows on).

Everything is measured or stated, never assumed:
  - t0                  from the capture's video.calibration.tsv (calibrated_t0)
  - the Act epoch       from the leg's own measurement header, decoded from the
                        pcap (the BC=Act line: cast_ms 0, INSTANT, no BeginCast)
  - the willeq bc_t/act_t from the new harness run's timing.tsv
                        (action-only: BC = Act, no BeginCast)
  - the swing facts     from the run's fx_activity.tsv + slots.tsv

It rewrites the spec (specs/b1162_disc4499_defensive.json), re-runs the B1158
clip builder for this id only (make_clip.py -- the other alignment lines are
untouched), appends the new alignment line, and regenerates index.tsv +
index.html with make_index.py.

usage: make_disc_clip_4499.py <ledger-dir> <capture-dir> <willeq-frames-dir>
       [measurement-header]
"""
import json
import os
import re
import subprocess
import sys

B1158 = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "B1158", "scripts")
ID = "b1162_disc4499_defensive"


def read(path):
    with open(path) as f:
        return f.read()


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    ledger, cap, frames = sys.argv[1], sys.argv[2], sys.argv[3]
    meas = (sys.argv[4] if len(sys.argv) > 4
            else os.path.join(cap, "..", "..", "measure", "disc4499_v3",
                              "b1162_disc4499_v3_4499_1_npc.txt"))
    tip = read(os.path.join(os.path.dirname(os.path.dirname(frames)), "render_env.tsv")).split("\t")[1]
    tip9 = tip[:9]

    # --- the oracle side (the existing capture; measured, not assumed) ---
    t0 = None
    cal = [l.split("\t") for l in read(os.path.join(cap, "video.calibration.tsv")).splitlines() if l.strip()]
    assert cal and "calibrated_t0" in cal[0], "video.calibration.tsv has no calibrated_t0 column"
    i = cal[0].index("calibrated_t0")
    t0 = float(cal[1][i])
    mh = read(meas)
    m = re.search(r"^## spell 4499 rep 1 .*BC (\d+\.\d+) .* Act (\d+\.\d+) cast ([\d.]+)s", mh, re.M)
    assert m, "no '## spell 4499' header line in " + meas
    bc_epoch = float(m.group(1))
    act_epoch = float(m.group(2))
    assert m.group(3) == "0.000", "the 4499 leg was an instant action-only cast (cast 0.000s)"
    assert bc_epoch == act_epoch, "no OP_BeginCast in the 4499 leg: BC = Act"

    # --- the willeq side (the new harness run at the current tip) ---
    act_t = bc_t = None
    for l in read(os.path.join(frames, "timing.tsv")).splitlines():
        p = l.split("\t")
        if len(p) >= 8 and p[2] == "Action":
            act_t = float(p[7])
    assert act_t is not None, "timing.tsv has no Action event"
    bc_t = act_t  # action-only: BC = Act
    assert abs(act_t - 1.5) < 0.05, f"the plan casts at t 1.5; timing says Action at {act_t}"

    fx = [l.rstrip("\n").split("\t") for l in read(os.path.join(frames, "fx_activity.tsv")).splitlines()][1:]
    anims = sorted({r[4] for r in fx if r[4] != "p01"})
    assert anims == ["s01"], f"the caster track of the 4499 run is {anims}, expected ['s01'] (CastingAnim 27)"
    swing_fr = [int(r[0]) for r in fx if r[4] == "s01"]
    sw0, sw1 = min(swing_fr), max(swing_fr)
    effects = max(int(r[2]) for r in fx)
    missiles = max(int(r[3]) for r in fx)
    ns = len(read(os.path.join(frames, "slots.tsv")).splitlines()) - 1
    assert effects == 0 and missiles == 0 and ns == 0, \
        f"the /disc run drew a slot/effect/missile: slots={ns} effects={effects} missiles={missiles}"

    # --- the spec (the clip is windowed BC-1.0..Act+8.0 on BOTH sides) ---
    note = ("oracle: S->C OP_Action 4499 (target = source 259) at %s (cast_ms 0, no "
            "OP_BeginCast); the observer's caster box lit Act+0.454..+1.120, run cov "
            "0.054, peak cov 0.067 = the caster's arm animation (strip in the ledger "
            "clips/); no sprite. willeq (tip %s): action-only 4499 draws no slot, no "
            "effect, no missile; the caster plays s01 (CastingAnim 27) frames %d..%d "
            "(Act+0.00..%+.3f), anchor_table cov/run in the MR header."
            % (bc_epoch, tip9, sw0, sw1, (sw1 - sw0) / 30))
    spec = {
        "id": ID,
        "group": "observer-b1162",
        "title": "rec 326 | 4499 Defensive Discipline (/disc, WAR 60, C->S OP_CastSpell "
                 "slot 10 -> OP_Action, no BeginCast) | caster s01 swing, no sprite",
        "oracle": {
            "video": os.path.join(cap, "video.mp4"),
            "t0": t0,
            "bc_epoch": bc_epoch,
            "act_epoch": act_epoch,
            "label": "B1162 b1162_disc4499_v3 observer (BC=Act: no BeginCast; lit "
                     "+0.454..+1.120, peak cov 0.067, no sprite)",
        },
        "willeq": {
            "frames": frames,
            "bc_t": bc_t,
            "act_t": act_t,
            "label": "harness b1130-self 4499 action-only (tip %s)" % tip9,
        },
        "verdicts": [
            ["Source none (both)", "PASS"],
            ["Landing sprite none (both)", "PASS"],
            ["caster s01 swing at the packet (both)", "PASS"],
            ["swing anatomy: different cameras", "n/a"],
        ],
        "note": note,
    }
    os.makedirs(os.path.join(ledger, "specs"), exist_ok=True)
    spec_path = os.path.join(ledger, "specs", ID + ".json")
    with open(spec_path, "w") as f:
        json.dump(spec, f, indent=1)

    # --- the clip (make_clip.py <spec.json> <clips-dir>; prints the alignment line on stdout) ---
    clips = os.path.join(ledger, "clips")
    os.makedirs(clips, exist_ok=True)
    env = dict(os.environ, PYTHONHASHSEED="0")
    p = subprocess.run([sys.executable, "-I", os.path.join(B1158, "make_clip.py"),
                        spec_path, clips],
                       capture_output=True, text=True, env=env)
    assert p.returncode == 0, "make_clip.py failed:\n" + p.stderr
    line = p.stdout.strip()
    assert line and line.startswith("{")
    json.loads(line)  # it must be a JSON line
    print("[disc-clip] " + p.stderr.strip().splitlines()[-1])

    # --- the index: drop the old line for this id, append the new, regenerate ---
    aj = os.path.join(ledger, "alignments.jsonl")
    rows = [l for l in read(aj).splitlines() if l and json.loads(l).get("id") != ID]
    rows.append(line)
    with open(aj, "w") as f:
        f.write("\n".join(rows) + "\n")
    p = subprocess.run([sys.executable, "-I", os.path.join(B1158, "make_index.py"),
                        aj, ledger],
                       capture_output=True, text=True, env=env)
    assert p.returncode == 0, "make_index.py failed:\n" + p.stderr
    for l in p.stderr.strip().splitlines():
        print("[disc-clip] " + l)
    print("[disc-clip] spec + clip + index rebuilt: " + aj)
    print("[disc-clip] willeq side: tip %s, s01 f%d..f%d (Act+0.00..%+.3f), slots=%d effects=%d missiles=%d"
          % (tip, sw0, sw1, (sw1 - sw0) / 30, ns, effects, missiles))


if __name__ == "__main__":
    main()
