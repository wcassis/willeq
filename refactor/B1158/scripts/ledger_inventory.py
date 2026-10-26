#!/usr/bin/env python3
"""B1158: build the ledger's clip inventory (one spec JSON per clip).

usage: ledger_inventory.py <gate-run-dir> <out-dir> [band-table.tsv]

Sources (all measured; see ledger_lib.py for where each number comes from):

  gate records      <gate>/measure/<kind>_r<rec>/video.mkv (the harness's own
                    30 fps run video; BeginCast/Action from <gate>/runs/<kind>_r<rec>/
                    timing.tsv, fallback fable_<spell>/meta.json) banded against
                    [band-table.tsv] (the gate's own banded table, e.g.
                    refactor/B1155/banded_table.tsv) and the B1130 oracle
                    review/B1130/measure/<capture>_<kind>.txt + record_table.tsv.
  record 51 + the   review/B1134/measure/<leg>_<spell>_<rep>.txt over
  observer legs     review/B1134/oracle/<leg>/video.mp4 (calibrated_t0 from the
                    leg's video.calibration.tsv) and the B1143 final legs
                    (worker/measure/p3_rig93_ / p3_disc325_3/ over the matching
                    worker/oracle leg video; for p3_rig93 the video leg is the
                    ONE whose calibration puts the measured cast inside the
                    file). These are oracle-only: the gate never renders them.
  missile legs      review/B1152/worker/missile_oracle.tsv (M1_38, M2_54) over
                    review/B1152/worker/oracle/<leg>/video.mp4; the willeq side
                    is a <gate>/measure/<leg>/ run (or LEDGER_EXTRA_MEASURE
                    dirs, colon-separated) when one exists, else oracle-only.

Video files that the hermes-ext copy of /mnt/projects/fable lacks (text-only
for B1134/B1143/B1152) are read from the ThinkPad-NFS fetch mirror
~/kb-evidence/kb-b1158-visual-ledger/sources/<B>/<leg>/video.mp4 (the real
files, rsynced from the ThinkPad's NFS mount; never re-rendered).

Writes <out>/specs/<id>.json per clip and prints the inventory summary.
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ledger_lib as L

B1130 = "/mnt/projects/fable/review/B1130"
B1134 = "/mnt/projects/fable/review/B1134"
B1143 = "/mnt/projects/fable/review/B1143/worker"
B1152 = "/mnt/projects/fable/review/B1152/worker"
MIRROR = os.path.expanduser("~/kb-evidence/kb-b1158-visual-ledger/sources")

SPELL_NAMES = {  # names outside record_table.tsv (quoted from the leg READMEs)
    1628: "Evacuate", 562: "North Portal", 4499: "Defensive Discipline",
    38: "Lightning Bolt", 54: "Frost Bolt",
}

# the missile gate's verdicts, quoted from refactor/B1152/README.md
# "The missile timing gate" table (oracle vs harness, all PASS)
MISSILE_VERDICTS = {
    38: [["launch", "PASS"], ["reach", "PASS"], ["arrival", "PASS"]],
    54: [["no-missile", "PASS"]],
}
MISSILE_NOTES = {
    38: ("B1152 gate (Act+): launch -0.484/-0.486, reach -0.184/-0.184, "
         "arrival +0.116/+0.116 (oracle/harness)"),
    54: "B1152 gate: record 82 has no Middle stages -- no missile, at-removal rule",
}


def spell_name(table, spell):
    for r in table:
        if r[5] == str(spell):
            return r[6]
    if spell in SPELL_NAMES:
        return SPELL_NAMES[spell]
    return L.db_spell_name(spell) or ""


def load_record_table():
    rows = []
    with open(os.path.join(B1130, "record_table.tsv")) as f:
        for line in f:
            if line.startswith("record\t"):
                continue
            rows.append(line.rstrip("\n").split("\t"))
    return rows


def band_verdicts(band_table, rec):
    """The gate's own per-column verdicts (PASS/short/over/n-a)."""
    if not band_table:
        return []
    for row in L.read_tsv_rows(band_table):
        if row[0] != str(rec):
            continue
        # banded_table.tsv data rows: rec, kind, then 9x (oracle, harness, verdict);
        # the header names those 27 columns starting at index 2.
        names = ["source_cov", "landing_cov", "landing_end_s", "p90_src",
                 "p90_land", "source_onset_s", "source_centroid", "src_h_lo", "src_h_hi"]
        out = []
        for i, n in enumerate(names):
            v = row[2 + i * 3 + 2]
            label = {"landing_end_s": "End", "source_onset_s": "Src-onset",
                     "source_cov": "Source", "landing_cov": "Landing",
                     "source_centroid": "Src-cen", "src_h_lo": "Src-lo",
                     "src_h_hi": "Src-hi", "p90_src": "p90-src",
                     "p90_land": "p90-land"}[n]
            out.append([label, v])
        return out
    return []


def harness_timing(gate, tag):
    """BeginCast/Action seconds from the run's timing.tsv (t column),
    fallback the measure fable_<spell>/meta.json."""
    tsv = os.path.join(gate, "runs", tag, "timing.tsv")
    if os.path.exists(tsv):
        bc = act = None
        for row in L.read_tsv_rows(tsv):
            if len(row) >= 3:
                if row[2] == "BeginCast":
                    bc = float(row[1])
                elif row[2] == "Action":
                    act = float(row[1])
        if bc is not None and act is not None:
            return bc, act
    measure_dir = os.path.join(gate, "measure", tag)
    for d in sorted(os.listdir(measure_dir)):
        p = os.path.join(measure_dir, d, "meta.json")
        if os.path.exists(p):
            m = L.meta_json(p)
            return m["t_begincast"], m["t_action"]
    raise ValueError(f"{measure_dir}: no timing.tsv BeginCast/Action and no meta.json")


def landing_end(measure_dir):
    rows = L.read_tsv_rows(os.path.join(measure_dir, "b1130_rows.tsv"))
    if rows and len(rows[0]) > 7:
        try:
            return float(rows[0][7])
        except ValueError:
            return None
    return None


def gate_specs(gate, band_table):
    table = load_record_table()
    by_rec = {r[0]: r for r in table}
    specs = []
    measure = os.path.join(gate, "measure")
    if not os.path.isdir(measure):
        raise SystemExit(f"not a gate-run dir (no measure/): {gate}")
    for tag in sorted(os.listdir(measure), key=lambda t: (len(t), t)):
        m = re.fullmatch(r"(fixed_npc|fixed_self)_r(\d+)", tag)
        if not m or not os.path.exists(os.path.join(measure, tag, "video.mkv")):
            continue
        kind, rec = m.group(1), int(m.group(2))
        row = by_rec.get(str(rec))
        if row is None:
            raise SystemExit(f"{tag}: record {rec} not in record_table.tsv")
        spell, name, cap, cls = row[5], row[6], row[2], row[7]
        om = L.parse_b1130_measure(os.path.join(B1130, "measure", f"{cap}_{kind}.txt"))
        ovideo = os.path.join(B1130, "oracle", cap, "video.mp4")
        if not os.path.exists(ovideo):
            raise SystemExit(f"record {rec}: oracle video missing: {ovideo}")
        bc_t, act_t = harness_timing(gate, tag)
        specs.append({
            "id": f"g_r{rec:03d}_{spell}_{kind}",
            "group": "gate",
            "title": f"rec {rec} | {spell} {name} | {kind} ({cls})",
            "oracle": {"video": ovideo, "t0": om["video_t0"],
                       "bc_epoch": om["bc_epoch"], "act_epoch": om["act_epoch"],
                       "label": f"B1130 {cap}"},
            "willeq": {"video": os.path.join(measure, tag, "video.mkv"),
                       "bc_t": bc_t, "act_t": act_t,
                       "label": f"gate {tag}"},
            "verdicts": band_verdicts(band_table, rec),
            "measured_end_s": landing_end(os.path.join(measure, tag)),
        })
    return specs


def mirror(b, leg):
    return [os.path.join(MIRROR, b, leg, "video.mp4")]


def cast_in_video(ovideo, t0, bc_epoch):
    fps, nf = L.ffprobe(ovideo)
    f = (bc_epoch - t0) * fps
    return fps < f < (nf or 10**9)


def oracle_only_spec(cid, group, title, video, mirrors, t0, mm, label):
    return {
        "id": cid, "group": group, "title": title,
        "oracle": {"video": video, "mirrors": mirrors, "t0": t0,
                   "bc_epoch": mm["bc_epoch"], "act_epoch": mm["act_epoch"],
                   "label": label},
        "willeq": None,
        "verdicts": [],
        "note": (f"measured cast: BC {mm['bc_epoch']:.3f} Act {mm['act_epoch']:.3f} "
                 f"(cast {mm['cast_ms']} s), observer rig"),
    }


def observer_specs():
    table = load_record_table()
    specs = []
    # ---- B1134 (oracle-only observer legs) ---------------------------------
    for leg in ["p2_rig93", "p2b_rig93_swap", "p3_ports", "p4_discs",
                "p4b_discs", "p5_discs_gm"]:
        odir = os.path.join(B1134, "oracle", leg)
        calp = os.path.join(odir, "video.calibration.tsv")
        if not os.path.isdir(odir) or not os.path.exists(calp):
            continue
        t0 = L.read_calibration(odir)["calibrated_t0"]
        mdir = os.path.join(B1134, "measure")
        for fn in sorted(os.listdir(mdir)):
            m = re.fullmatch(re.escape(leg) + r"_(\d+)_(\d+)\.txt", fn)
            if not m:
                continue
            mm = L.parse_leg_measure(os.path.join(mdir, fn))
            spell, rep = mm["spell"], mm["rep"]
            video = L.find_video(os.path.join(odir, "video.mp4"), mirror("B1134", leg))
            if not cast_in_video(video, t0, mm["bc_epoch"]):
                raise SystemExit(f"B1134 {leg} spell {spell}: measured cast not in the video")
            specs.append(oracle_only_spec(
                f"b1134_{leg}_{spell}_{rep}", "observer-b1134",
                f"B1134 {leg} | {spell} {spell_name(table, spell)} | observer rep {rep}",
                video, mirror("B1134", leg), t0, mm, f"B1134 {leg}"))
    # ---- B1143 (final legs) ------------------------------------------------
    obase = os.path.join(B1143, "oracle")
    for base, mdir in [("rig93", os.path.join(B1143, "measure")),
                       ("disc325", os.path.join(B1143, "measure", "p3_disc325_325"))]:
        files = []
        for fn in sorted(os.listdir(mdir)):
            m = re.fullmatch(rf"p3_{base}_(\d+)_(\d+)_(npc|player)\.txt", fn)
            if m:
                files.append((fn, int(m.group(1)), int(m.group(2)), m.group(3)))
        if not files:
            continue
        cands = [d for d in os.listdir(obase)
                 if (d == f"p3_{base}" or d.startswith(f"p3_{base}.")
                     and os.path.exists(os.path.join(obase, d, "video.calibration.tsv")))]
        for fn, spell, rep, kind in files:
            mm = L.parse_leg_measure(os.path.join(mdir, fn))
            fits = []
            for d in cands:
                t0 = L.read_calibration(os.path.join(obase, d))["calibrated_t0"]
                video = L.find_video(os.path.join(obase, d, "video.mp4"),
                                     mirror("B1143", d), required=False)
                if video and cast_in_video(video, t0, mm["bc_epoch"]):
                    fits.append((d, video, t0))
            if len(fits) != 1:
                raise SystemExit(f"B1143 p3_{base} spell {spell}: {len(fits)} leg videos "
                                 f"fit (expected 1): {[f[0] for f in fits]}")
            d, video, t0 = fits[0]
            specs.append(oracle_only_spec(
                f"b1143_p3_{base}_{spell}_{rep}", "observer-b1143",
                f"B1143 p3_{base} | {spell} {spell_name(table, spell)} | {kind} rep {rep}",
                video, mirror("B1143", d), t0, mm, f"B1143 {d}"))
    return specs


def missile_specs(gate):
    specs = []
    tsv = os.path.join(B1152, "missile_oracle.tsv")
    if not os.path.exists(tsv):
        return specs
    extra_dirs = [d for d in os.environ.get("LEDGER_EXTRA_MEASURE", "").split(":") if d]
    with open(tsv) as f:
        header = None
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("#") or not line.strip():
                continue
            c = line.split("\t")
            if header is None:
                header = c
                continue
            col = {h: i for i, h in enumerate(header)}
            leg = c[0]
            spell = int(leg.split("_", 1)[1])     # M1_38 -> 38, M2_54 -> 54
            name = c[col["spell"]]                # the spell NAME column
            # the oracle leg dir is named m{n}_rig<spell> (m1_rig38, m2_rig54)
            obase = os.path.join(B1152, "oracle")
            odir = None
            for d in os.listdir(obase):
                if re.fullmatch(rf"m\d+_rig{spell}", d):
                    odir = os.path.join(obase, d)
                    break
            if odir is None:
                raise SystemExit(f"B1152 {leg}: no oracle leg dir matching m*_rig{spell}")
            t0 = L.read_calibration(odir)["calibrated_t0"]
            video = L.find_video(os.path.join(odir, "video.mp4"),
                                 mirror("B1152", os.path.basename(odir)))
            bc_v, act_v = float(c[col["bc1_v"]]), float(c[col["act1_v"]])
            if not cast_in_video(video, t0, t0 + bc_v):
                raise SystemExit(f"B1152 {leg}: measured cast not in the video")
            w = None
            for base in [os.path.join(gate, "measure")] + extra_dirs:
                for cand in (os.path.join(base, os.path.basename(odir)),
                             os.path.join(base, leg.lower()), os.path.join(base, leg)):
                    if not os.path.isdir(cand):
                        continue
                    if os.path.exists(os.path.join(cand, "video.mkv")):
                        bc_t, act_t = _timing(cand)
                        w = {"video": os.path.join(cand, "video.mkv"),
                             "bc_t": bc_t, "act_t": act_t, "label": f"gate {leg}"}
                    elif os.path.isdir(os.path.join(cand, "frames")):
                        bc_t, act_t = _timing(cand)
                        w = {"frames": os.path.join(cand, "frames"),
                             "bc_t": bc_t, "act_t": act_t, "label": f"gate {leg}"}
                    if w:
                        break
                if w:
                    break
            specs.append({
                "id": leg, "group": "missile",
                "title": f"missile {leg} | {spell} {name} | rig (rec {c[col['rec']]})",
                "oracle": {"video": video, "mirrors": mirror("B1152", os.path.basename(odir)),
                           "t0": t0, "bc_epoch": t0 + bc_v, "act_epoch": t0 + act_v,
                           "label": f"B1152 {os.path.basename(odir)} (30 fps)"},
                "willeq": w,
                "verdicts": MISSILE_VERDICTS.get(spell, []),
                "note": MISSILE_NOTES.get(spell, ""),
            })
    return specs


def _timing(cand):
    """bc_t/act_t for a standalone run dir (timing.tsv or fable_*/meta.json)."""
    tsv = os.path.join(cand, "timing.tsv")
    if os.path.exists(tsv):
        bc = act = None
        for row in L.read_tsv_rows(tsv):
            if row[2] == "BeginCast":
                bc = float(row[1])
            elif row[2] == "Action":
                act = float(row[1])
        if bc is not None and act is not None:
            return bc, act
    for d in sorted(os.listdir(cand)):
        p = os.path.join(cand, d, "meta.json")
        if os.path.exists(p):
            m = L.meta_json(p)
            return m["t_begincast"], m["t_action"]
    raise ValueError(f"{cand}: no timing")


def main():
    gate, out_dir = sys.argv[1], sys.argv[2]
    band_table = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else None
    specs = gate_specs(gate, band_table) + missile_specs(gate) + observer_specs()
    sdir = os.path.join(out_dir, "specs")
    os.makedirs(sdir, exist_ok=True)
    for s in specs:
        with open(os.path.join(sdir, s["id"] + ".json"), "w") as f:
            json.dump(s, f, indent=1)
    n_w = sum(1 for s in specs if s["willeq"])
    print(f"[inventory] {len(specs)} clips ({n_w} side-by-side, "
          f"{len(specs) - n_w} oracle-only)", file=sys.stderr)
    for s in specs:
        print(f"  {s['id']}: {s['title']}", file=sys.stderr)
    print(json.dumps([s["id"] for s in specs]))


if __name__ == "__main__":
    main()
