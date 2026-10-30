#!/usr/bin/env python3
"""B1162 /disc record leg (card t_a74670e3): build the committed /disc harness
rows for records 325/326 in the B1143 item-3 row shape
(refactor/B1143/rows/p3_disc325_325.tsv, "the B1132 18 columns +
stage/target/target_id"):

  record spell rep stage target target_id who onset_act centroid h_p5 h_p95
  dx_p5 dx_p95 cov ground legs torso shoulders head above verdict

from the measured dirs produced by refactor/B1162/scripts/disc_harness_rows.sh
(committed plans + the committed B1132-instrument fork
harness_measure_b1162_anchor.py at the preset and the rendered anchors):

  <out_root>/render_env.tsv                     (tip, pack, binary, render args)
  <out_root>/measure/<tag>/anchor_table.tsv     (both anchors, per cast)
  <out_root>/measure/<tag>/rendered/measure.txt (RISE + RUN + SOURCE lines)
  <out_root>/frames/<tag>/fx_activity.tsv       (per-frame effects/missiles/caster track)
  <out_root>/frames/<tag>/slots.tsv             (drawn spellsnew.eff slots)

One row per record per stage: SOURCE (action-only: no OP_BeginCast, so none)
and LANDING (the caster box = the self-cast target ROI, at the RENDERED anchor
-- the B1174 convention for the /disc rows; the preset anchor's numbers sit in
the header table and in anchor_table.tsv). The swing track facts (the caster's
CastingAnim: record 325 -> 44 = t06, record 326 -> 27 = s01, eqgame's code
table, refactor/B1148) come from fx_activity.tsv; the slots/effect/missile
censuses from slots.tsv / fx_activity.tsv.

usage: disc_rows_tsv.py <out_root> <out_tsv>
"""
import os
import re
import sys

TAGS = [("disc325_8604", 325, 8604, "t06"), ("disc326_4499", 326, 4499, "s01")]


def read(path):
    with open(path) as f:
        return f.read()


def anchor_table(root, tag):
    rows = [l.rstrip("\n").split("\t")
            for l in read(os.path.join(root, "measure", tag, "anchor_table.tsv")).splitlines()][1:]
    return {r[0]: dict(spell=r[1], crop=r[2], tfeet=r[3], tpx=r[4], onset=r[5],
                       end=r[6], peak=r[7], rise_cov=r[8], run_cov=r[9], cen_h=r[10],
                       h_p5=r[11], h_p95=r[12], ground=r[13]) for r in rows}


def measure_lines(root, tag, anchor):
    text = read(os.path.join(root, "measure", tag, anchor, "measure.txt"))
    rise = re.search(r"RISE onset f(\d+) Act([+-][\d.]+)s end f(\d+) Act([+-][\d.]+)s "
                     r"peak f(\d+) Act([+-][\d.]+)s coverage ([\d.]+)", text)
    run = re.search(r"RUN f(\d+)-f(\d+) \((\d+) frames\): cov ([\d.]+) cen_h ([\d.-]+) "
                    r"cen_dx ([+\d.-]+) dx_p5\.\.p95 ([\d.-]+)\.\.([+\d.-]+) "
                    r"h_p5\.\.p95 ([\d.-]+)\.\.([+\d.-]+) off_torso ([\d.-]+) L-R ([+\d.-]+) "
                    r"bands ground=([\d.]+) legs=([\d.]+) torso=([\d.]+) shoulders=([\d.]+) "
                    r"head=([\d.]+) above=([\d.]+)", text)
    src = re.search(r"SOURCE (none above thr \(max (\d+)px = ([\d.]+) model\^2\)"
                    r"|NONE before Action \(max cov BC\.\.Act ([\d.]+), thr ([\d.]+)\))", text)
    return rise, run, src


def fx_activity(root, tag):
    rows = [l.rstrip("\n").split("\t")
            for l in read(os.path.join(root, "frames", tag, "fx_activity.tsv")).splitlines()][1:]
    anims = sorted({r[4] for r in rows if r[4] != "p01"})
    swing = None
    for a in anims:
        fr = [int(r[0]) for r in rows if r[4] == a]
        swing = (a, min(fr), max(fr))
    return dict(n=len(rows),
                effects=max((int(r[2]) for r in rows), default=0),
                missiles=max((int(r[3]) for r in rows), default=0),
                swing=swing)


def slots(root, tag):
    return len(read(os.path.join(root, "frames", tag, "slots.tsv")).splitlines()) - 1


def main():
    root, out = sys.argv[1], sys.argv[2]
    env = dict(l.split("\t") for l in read(os.path.join(root, "render_env.tsv")).split())
    tip = env["tip"]

    tables = []
    for tag, rec, sid, want_track in TAGS:
        at = anchor_table(root, tag)
        assert "rendered" in at and "preset" in at, f"{tag}: anchor_table missing an anchor"
        rise, run, src = measure_lines(root, tag, "rendered")
        fx = fx_activity(root, tag)
        ns = slots(root, tag)
        a = at["rendered"]
        assert a["spell"] == str(sid), f"{tag}: anchor_table spell {a['spell']} != {sid}"
        assert rise and run, f"{tag}: rendered anchor has no RISE/RUN line (all-none run)"
        assert fx["effects"] == 0 and fx["missiles"] == 0 and ns == 0, \
            f"{tag}: the /disc run drew a slot/effect/missile: slots={ns} fx={fx}"
        assert src, f"{tag}: no SOURCE line in rendered/measure.txt"
        track, f0, f1 = fx["swing"]
        assert track == want_track, \
            f"{tag}: the caster played {track!r}, expected {want_track!r} (CastingAnim of record {rec})"
        onset = rise.group(1)
        # SOURCE row
        if src.group(1).startswith("none"):
            srcmax = "max " + src.group(2) + "px = " + src.group(3) + " model^2"
        else:
            srcmax = "max cov BC..Act " + src.group(5)
        tables.append({
            "rec": rec, "sid": sid, "track": track,
            "srcmax": srcmax,
            "land": [rec, sid, 1, "SOURCE", "SELF", 0, "fable", "none", "-", "-", "-", "-", "-",
                     "-", "-", "-", "-", "-", "-",
                     "action-only: no OP_BeginCast, " + srcmax],
            "land2": [rec, sid, 1, "LANDING", "SELF", 0, "fable",
                      "+" + rise.group(2).lstrip("+-") + " (f" + onset + ")",
                      run.group(5), run.group(9), run.group(10),
                      run.group(7), run.group(8), rise.group(7),
                      run.group(13), run.group(14), run.group(15),
                      run.group(16), run.group(17), run.group(18),
                      ("no slot (%d rows), no effect, no missile (fx_activity %d rows); "
                       "the lit run is the caster %s swing f%d..f%d (30 fps); no sprite -- "
                       "record %d has an empty Target stage and no OP_BeginCast here"
                       % (ns, fx["n"], track, f0, f1, rec))],
            "at": at, "fx": fx,
        })

    hdr = [
        "# B1162 /disc record leg (card t_a74670e3): the /disc harness rows for records",
        "# 325/326 -- the two disciplines that LANDED on the real /disc path (C->S",
        "# OP_CastSpell slot 10 -> S->C OP_Action, no OP_BeginCast): 8604 Leechcurse",
        "# Discipline (record 325, CastingAnim 44 = t06) and 4499 Defensive Discipline",
        "# (record 326, CastingAnim 27 = s11), action-only self-cast at level 60 on the",
        "# FX harness (model_viewer --spell-test), b1130-self camera, the previous legs'",
        "# plans (refactor/B1162/plans/harness_8604.plan, harness_4499.plan):",
        "#   1.5 <sid> self 0 0 60 action",
        "#",
        "# Measured at fable tip %s (render_env.tsv). Render (ThinkPad, surfaceless" % tip,
        "# Quadro at 1280x800, the render_gate.sh b1130 path, Will 2026-10-29) and",
        "# measure (here, the committed B1132 instruments via",
        "# refactor/B1162/scripts/harness_measure_b1162_anchor.py, preset + rendered",
        "# anchors) with:",
    ]
    for tag, rec, sid, want_track in TAGS:
        hdr.append("#   bash refactor/B1162/scripts/disc_harness_rows.sh <worktree> <slug> <out_root> %s" % tag)
    hdr += [
        "#",
        "# Row shape: refactor/B1143/rows/p3_disc325_325.tsv (the B1132 18 columns +",
        "# stage/target/target_id). LANDING rows are at the rendered anchor (the caster",
        "# box the harness actually drew, the B1174 convention); the preset anchor's",
        "# numbers (harness_measure_b1162_anchor.py's anchor_table.tsv, both anchors) are:",
    ]
    for t in tables:
        for anchor in ("preset", "rendered"):
            a = t["at"][anchor]
            hdr.append("#   %s %s %s: %s (crop %s, tfeet %s / %s px) onset %s end %s peak %s "
                       "rise_cov %s run_cov %s cen_h %s h %s..%s ground %s"
                       % (t["sid"], t["rec"], anchor, "none" if a["onset"] == "none" else a["onset"],
                          a["crop"], a["tfeet"], a["tpx"], a["onset"], a["end"], a["peak"],
                          a["rise_cov"], a["run_cov"], a["cen_h"], a["h_p5"], a["h_p95"],
                          a["ground"]))
    hdr += [
        "#",
        "# Oracle references (the existing lane-5 captures, no re-capture): record 325 ->",
        "# B1174 leg b1174_disc8604 (review/B1174/README.md sec. 4: lit +0.422..+4.355,",
        "# run cov 0.052, peak cov 0.066, no sprite); record 326 -> B1162 leg",
        "# b1162_disc4499_v3 (refactor/B1162/README.md:623: lit +0.454..+1.120, run cov",
        "# 0.054, peak cov 0.067 = the caster's arm animation, no sprite).",
        "#",
        "# Ledger clip: review/B1162/worker/ledger/index.tsv (b1162_disc4499_defensive),",
        "# rebuilt with refactor/B1162/scripts/make_disc_clip_4499.py (this tip's willeq",
        "# side; oracle side = the existing capture).",
    ]
    cols = ["record", "spell", "rep", "stage", "target", "target_id", "who",
            "onset_act", "centroid", "h_p5", "h_p95", "dx_p5", "dx_p95", "cov",
            "ground", "legs", "torso", "shoulders", "head", "above", "verdict"]
    lines = hdr + ["\t".join(cols)]
    for t in tables:
        lines.append("\t".join(map(str, t["land"])))
        lines.append("\t".join(map(str, t["land2"])))
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines[len(hdr) + 1:]))
    print(f"[disc-rows] wrote {out} ({len(lines) - len(hdr) - 1} rows + {len(hdr)} header lines)")


if __name__ == "__main__":
    main()
