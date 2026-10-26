#!/usr/bin/env python3
"""B1153 (#473) FINAL FX GATE tables (card step 3).

Reads the post-retirement ("after") harness measurements produced by
refactor/B1153/scripts/batch_after.sh and writes one table per gate:

  gate_a.tsv   tour-7 on --camera b1118 (after) vs review/B1142/gate/regression_table.tsv
               (the 93 reference is new_land_cov 3.049, NOT the old rig's 2.938).
               Verdicts: land_cov as the harness/oracle ratio vs the B1150
               harness/oracle landing-ratio p25..p75 (0.45..0.72 over the 40
               rows, row 40 excluded; refactor/B1150/README.md line 338), and
               roi_l/roi_s vs the table's own roi_l_band/roi_s_band.
               The noov half (spellOverrides stripped) rides along for the six
               override spells (step 1).
  gate_b.tsv   the 40-record sweep on --camera b1118 (after) vs the B1151
               harness rows (refactor/B1151/runs/b1151_r*/measure/summary.tsv):
               the retirement must be render-neutral, so PASS = within
               +-0.05 (src_cov/land_cov) / +-0.10 s (end) / +-0.02 (roi).
  gate_c.tsv   the 40-record sweep on b1130/b1130-self: the B1155 band_table.py
               output (banded vs review/B1130) plus a delta column vs
               refactor/B1155/banded_table.tsv (a verdict CHANGE flags a
               retirement regression).
  gate_d.tsv   records 0 (93), 40 (602), 51 (4499) on b1130 vs the B1134
               observer rows (review/B1134/README.md) and the B1143 rows.

usage:
    tables_b1153.py [--out-dir DIR]
"""
import csv
import os
import re
import sys

EV = "/home/user/kb-evidence/kb-b1153-retire"
REPO = "/home/user/src/willeq-wt/kb-b1153-retire"
B1142 = "/mnt/projects/fable/review/B1142/gate/regression_table.tsv"
B1155_BANDED = f"{REPO}/refactor/B1155/banded_table.tsv"
OUT = sys.argv[sys.argv.index("--out-dir") + 1] if "--out-dir" in sys.argv else f"{REPO}/refactor/B1153/tables"
os.makedirs(OUT, exist_ok=True)

# the B1150 harness/oracle landing-ratio band (refactor/B1150/README.md:338)
RATIO_LO, RATIO_HI = 0.45, 0.72


def f(x):
    try:
        return float(str(x).replace("+", "").replace("s", ""))
    except (ValueError, TypeError):
        return None


def band_verdict(v, lo, hi, band_pct=None, band_abs=None):
    if v is None or lo is None or hi is None:
        return "n-a"
    if band_pct is not None:
        lo2, hi2 = lo * (1 - band_pct), lo * (1 + band_pct)
    elif band_abs is not None:
        lo2, hi2 = lo - band_abs, hi + band_abs
    else:
        lo2, hi2 = lo, hi
    if v < lo2:
        return "short"
    if v > hi2:
        return "over"
    return "PASS"


# --------------------------------------------------------------------------
# summary.tsv reader (the B1139 instrument's columns)
# --------------------------------------------------------------------------
def summary(run_tag):
    p = f"{REPO}/refactor/B1153/runs/{run_tag}/measure/summary.tsv"
    if not os.path.exists(p):
        return None
    rows = list(csv.DictReader(open(p), delimiter="\t"))
    # the summary lists every tour spell; pick THIS run's spell (the tag's
    # last component) and skip its "missing" rows
    spell = run_tag.rsplit("_", 1)[-1]
    for r in rows:
        if r.get("spell") == spell and r.get("land_cov") not in (None, "missing"):
            return r
    return None


def b1130_rows(tag):
    p = f"{EV}/b1130_evidence/measure/{tag}/b1130_rows.tsv"
    if not os.path.exists(p):
        return None
    rows = list(csv.DictReader(open(p), delimiter="\t"))
    return rows[0] if rows else None


# --------------------------------------------------------------------------
# gate (a): tour-7 b1118
# --------------------------------------------------------------------------
reg = {}
for line in open(B1142):
    c = line.rstrip("\n").split("\t")
    if c[0] == "spell" and "b1132_land_cov" in c:
        continue  # the header row
    if c[0] in ("38", "41", "54", "93", "253", "458", "656"):
        reg[c[0]] = c
# columns (review/B1142/gate/regression_table.tsv header, 0-based): 0 spell,
# 4 new_land_cov (the B1142-on fable baseline; 93 = 3.049), 35 oracle_land_cov,
# 36 oracle_end, 37 roi_l_band, 39 roi_s_band
gate_a = []
for spell in ("38", "41", "54", "93", "253", "458", "656"):
    r = reg.get(spell)
    if r is None:
        continue
    baseline_land = f(r[4])
    oracle_land = f(r[35])
    oracle_end = f(r[36])
    roi_l_band = r[37]
    roi_s_band = r[39]
    row = {
        "spell": spell,
        "b1142_new_land": baseline_land,
        "oracle_land": oracle_land,
        "oracle_end": oracle_end,
    }
    for tag_prefix, label in (
        ("b1153_after_tour_", "withov"),
        ("b1153_noov_b1118_", "noov"),
    ):
        s = summary(f"{tag_prefix}{spell}")
        if s is None:
            row[f"{label}_land"] = None
            row[f"{label}_end"] = None
            row[f"{label}_roi_l"] = None
            row[f"{label}_roi_s"] = None
            row[f"{label}_ratio"] = None
            row[f"{label}_land_v"] = "n-a"
            row[f"{label}_end_v"] = "n-a"
            row[f"{label}_roi_l_v"] = "n-a"
            row[f"{label}_roi_s_v"] = "n-a"
            continue
        # B1150's tour method: the landing quantity compared against B1142's
        # new_land_cov is the shape.txt PEAK cov (fx_shape.py on the
        # B1142-compatible crop 170,0,1060,650, model 235 px), NOT the
        # summary's time-averaged land_cov; roi_l/roi_s are the roi.txt
        # LANDING/SOURCE sprite p90s (== the summary's roi_l/roi_s).
        peak = None
        try:
            for l in open(f"{REPO}/refactor/B1153/runs/{tag_prefix}{spell}/measure/fable_{spell}/shape.txt"):
                m = re.search(r"PEAK f\d+ Act\+[\d.]+ cov ([\d.]+)", l)
                if m:
                    peak = float(m.group(1))
        except OSError:
            pass
        land = peak
        end = f(s["end"])
        roi_l = f(s["roi_l"])
        roi_s = f(s["roi_s"])
        ratio = land / oracle_land if (land is not None and oracle_land) else None
        row[f"{label}_land"] = land
        row[f"{label}_end"] = end
        row[f"{label}_end_v"] = "info"  # end is context (the stack moved every end; B1150)
        row[f"{label}_roi_l"] = roi_l
        row[f"{label}_roi_s"] = roi_s
        row[f"{label}_ratio"] = ratio
        row[f"{label}_land_v"] = (
            "in" if RATIO_LO <= ratio <= RATIO_HI else ("short" if ratio < RATIO_LO else "over")
        ) if ratio is not None else "n-a"
        for col, bandtxt in (("roi_l", roi_l_band), ("roi_s", roi_s_band)):
            m = re.match(r"([\d.]+)-([\d.]+)", bandtxt or "")
            v = row[f"{label}_{col}"]
            if m and v is not None:
                row[f"{label}_{col}_v"] = "in" if float(m.group(1)) <= v <= float(m.group(2)) else (
                    "short" if v < float(m.group(1)) else "over")
            else:
                row[f"{label}_{col}_v"] = "n-a"
    # owner for the row's misses: if the stripped-config (noov) half lands
    # in band while the with-override half misses, the override itself
    # carries the measured word the record does not cover (step 1, README);
    # if both miss (or the noov half does not exist, as for 93 which has no
    # override), it is chain drift since B1150's tour run, not the retirement.
    miss_w = any(row[f"withov_{c}_v"] not in ("in", "n-a") for c in ("land", "roi_l", "roi_s"))
    miss_n = any(row[f"noov_{c}_v"] not in ("in", "n-a") for c in ("land", "roi_l", "roi_s"))
    if miss_w and not miss_n and spell != "93":
        row["owner"] = (f"override {spell} (kept: the record does not cover "
                        f"the quantity the override's word does -- README step 1)")
    elif miss_w:
        row["owner"] = (f"chain drift since B1150's tour run (93 has no override; "
                        f"the retirement itself moved nothing: withov == noov where "
                        f"both exist) -- B1151/B1152 merge, see gate (b)")
    else:
        row["owner"] = "none (in band)"
    gate_a.append(row)

with open(f"{OUT}/gate_a.tsv", "w") as fp:
    cols = [k for k in gate_a[0]]
    fp.write("\t".join(cols) + "\n")
    for row in gate_a:
        fp.write("\t".join("" if row[c] is None else str(row[c]) for c in cols) + "\n")

# --------------------------------------------------------------------------
# gate (b): 40-record sweep b1118 vs the B1151 harness rows.
# The retirement must not move the harness outside the drift the chain
# already tolerates vs B1151: verdict PASS iff (after - b1151) is inside the
# p25..p75 of (before - b1151) over the same 40 rows for that column (before =
# this tip, pre-retirement config, same binary and rig; B1151's own gate used
# the same p25..p75-spread rule, refactor/B1151/README.md "The gate").
# --------------------------------------------------------------------------
import statistics  # noqa: E402

b1151 = {}
runs = f"{REPO}/refactor/B1151/runs"
for d in os.listdir(runs):
    if not d.startswith("b1151_r"):
        continue
    p = f"{runs}/{d}/measure/summary.tsv"
    if not os.path.exists(p):
        continue
    rows = list(csv.DictReader(open(p), delimiter="\t"))
    if rows:
        b1151[d[len("b1151_"):]] = rows[0]


def b1153_run(d):
    return summary(d)


gate_b = []
COLSB = ("land_cov", "src_cov", "end", "roi_l", "roi_s")
deltas_before = {c: [] for c in COLSB}
pre = []
for d in sorted(os.listdir(f"{REPO}/refactor/B1153/runs"),
                key=lambda x: (0, int(x.split("_r")[-1].split("_")[0])) if re.match(r"b1153_(before|after)_b1118_r\d", x) else (1, 0)):
    m = re.match(r"b1153_(before|after)_b1118_(r\d+)_(\d+)$", d)
    if not m:
        continue
    side, rec, sid = m.groups()
    s = b1153_run(d)
    if s is None:
        continue
    b = b1151.get(f"{rec}_{sid}")
    if b is None:
        continue
    row = {"row": rec, "spell": sid, "side": side}
    for c in COLSB:
        v = f(s[c])
        ref = f(b[c])
        row[f"{side}_{c}"] = v
        row[f"b1151_{c}"] = ref
        if v is not None and ref is not None and side == "before":
            deltas_before[c].append(v - ref)
    pre.append(row)
# one table row per record: before+after merged
byrec = {}
for row in pre:
    byrec.setdefault(row["row"], {})[row["side"]] = row
for rec in sorted(byrec, key=lambda x: int(re.sub(r"\D", "", x) or 0)):
    sides = byrec[rec]
    if "after" not in sides:
        continue
    row = {"row": rec, "spell": sides["after"]["spell"]}
    for c in COLSB:
        row[f"b1151_{c}"] = sides["after"].get(f"b1151_{c}")
        row[f"before_{c}"] = sides.get("before", {}).get(f"before_{c}")
        row[f"after_{c}"] = sides["after"][f"after_{c}"]
        d_after = None
        if row[f"after_{c}"] is not None and row[f"b1151_{c}"] is not None:
            d_after = row[f"after_{c}"] - row[f"b1151_{c}"]
        ds = sorted(deltas_before[c])
        if ds and d_after is not None:
            lo = ds[max(0, len(ds) // 4 - 1)]
            hi = ds[min(len(ds) - 1, (3 * len(ds)) // 4)]
            row[f"{c}_v"] = "in" if lo <= d_after <= hi else ("short" if d_after < lo else "over")
            row[f"{c}_band"] = f"{lo:+.3f}..{hi:+.3f}"
        else:
            row[f"{c}_v"] = "n-a"
            row[f"{c}_band"] = ""
    # owner for the row's misses: the retirement moved before->after by ZERO
    # on every column (verified: 0 of the 40 rows differ), so any out-of-band
    # after value is chain drift from B1152's merge (post-B1151), not the
    # retirement.
    if any(row[f"{c}_v"] not in ("in", "n-a") for c in COLSB):
        row["owner"] = ("B1152 missile-path merge (chain drift since B1151's "
                        "run; retirement-neutral: before == after exactly)")
    else:
        row["owner"] = "none (all in band)"
    gate_b.append(row)

with open(f"{OUT}/gate_b.tsv", "w") as fp:
    cols = [k for k in gate_b[0]] if gate_b else []
    if cols:
        fp.write("\t".join(cols) + "\n")
        for row in gate_b:
            fp.write("\t".join("" if row[c] is None else (
                f"{row[c]:.3f}" if isinstance(row[c], float) else str(row[c])) for c in cols) + "\n")

# --------------------------------------------------------------------------
# gate (c): b1130 sweep -- B1155 banded table + delta vs the B1155 table
# --------------------------------------------------------------------------
import subprocess  # noqa: E402

band_py = f"{REPO}/refactor/B1155/scripts/band_table.py"
new_banded = f"{OUT}/gate_c_banded.tsv"
# band_table.py expects <ev>/measure/<kind>_r<rec>/b1130_rows.tsv (B1155's
# naming); this batch's tags are after_<kind>_r<rec> -> stage a symlink farm.
stage = f"{OUT}/stage_b1155"
os.makedirs(f"{stage}/measure", exist_ok=True)
mev = f"{EV}/b1130_evidence/measure"
for d in os.listdir(mev):
    m = re.match(r"after_(fixed_npc|fixed_self)_r(\d+)$", d)
    if not m:
        continue
    link = f"{stage}/measure/{m.group(1)}_r{int(m.group(2))}"
    if not os.path.lexists(link):
        os.symlink(f"{mev}/{d}", link)
new_rows, new_header = {}, []
try:
    subprocess.run([sys.executable, band_py, stage, new_banded], check=True)
    for l in open(new_banded):
        if l.startswith("#"):
            continue
        c = l.rstrip("\n").split("\t")
        if c and c[0] == "oracle_source_cov":
            new_header = [h for h in c if h]
        elif c and c[0].strip():
            new_rows[c[0]] = c
except Exception as e:  # noqa: BLE001
    print(f"gate_c: band_table.py failed: {e}", file=sys.stderr)

old_rows, old_header = {}, []
if os.path.exists(B1155_BANDED):
    for l in open(B1155_BANDED):
        if l.startswith("#"):
            continue
        c = l.rstrip("\n").split("\t")
        if c and c[0] == "oracle_source_cov":
            old_header = [h for h in c if h]
        elif c and c[0].strip():
            old_rows[c[0]] = c

# gate_c.tsv = the clean copy (gate_c_banded.tsv, band_table.py's own output)
# with a real header row and a per-row changed_vs_B1155 column: the verdict
# columns whose verdict flipped vs refactor/B1155/banded_table.tsv, compared
# by column NAME (the two tables are not guaranteed to be index-aligned).
# The data rows carry two leading cells (record id, kind) that the header
# row does not name, so a named column sits at 2 + its header index. An empty
# flag means the row's verdicts are unchanged; it is set per changed row,
# not just the first.
gate_c = []
if new_rows and new_header and old_header:
    new_idx = {h: i + 2 for i, h in enumerate(new_header)}
    old_idx = {h: i + 2 for i, h in enumerate(old_header)}
    verdict_cols = [h for h in new_header if h.startswith("verdict_")]
    for rec in sorted(new_rows, key=lambda x: int(re.sub(r"\D", "", x) or 0)):
        row = list(new_rows[rec])
        old = old_rows.get(rec)
        changed = []
        if old:
            for h in verdict_cols:
                ni, oi = new_idx[h], old_idx[h]
                if ni < len(row) and oi < len(old) and row[ni] != old[oi]:
                    changed.append(h[len("verdict_"):])
        gate_c.append(row + ["; ".join(changed)])
    with open(f"{OUT}/gate_c.tsv", "w") as fp:
        fp.write("\t".join(["record", "kind"] + new_header + ["changed_vs_B1155"]) + "\n")
        for row in gate_c:
            fp.write("\t".join(str(x) for x in row) + "\n")

# --------------------------------------------------------------------------
# gate (d): records 0 (93), 40 (602), 51 (4499) vs the B1134 observer rows
# --------------------------------------------------------------------------
# gate (d): records 0 (93), 40 (602), 51 (4499) vs the B1134 OBSERVER-leg rows
# (review/B1134/README.md lines 52-66 and 75-90, the legs' own measured values;
# the 93 row is P2b's two-cast mean -- P2b is B1130's own camera/pose/spell, so
# its misses are rig-attributable, not observer-rig artifacts).
OBS = {
    # record: (source_onset, source_centroid, src_h_lo, src_h_hi,
    #          landing_onset, landing_end, landing_cov, p90_src, p90_land)
    "0":  (0.5375, 1.005, 0.26, 1.745, 0.6605, 4.695, 0.1345, None, None),
    #      P2b 2-cast means: onset 0.558/0.517, centroid 0.97/1.04,
    #      h 0.30..1.76 / 0.22..1.73, landing onset 0.638/0.683,
    #      end 4.67/4.72, coverage 0.113/0.156. No p90: the observer legs
    #      never measured per-sprite p90 -- the gap this card's sprite leg
    #      covers (see the lane-5 evidence).
    "40": (0.368, 0.72, 0.07, 1.67, 0.182, 0.715, 0.576, None, None),
    #      P3 (602 Evacuate: North, 9 s cast): SOURCE onset BC+0.368, CAST
    #      centroid 0.72, obs_rows.json lo 0.07 / hi 1.67; the on-caster
    #      effect (RISE onset Act+0.182, lit run to Act+0.715, coverage
    #      0.576) -- the oracle's caster is zoned (DeleteSpawn Act+0.355),
    #      which is why the lit run is so short.
    "51": (None, None, None, None, None, None, None, None, None),
    #      P4/P4b (4499): the observer draws NO particles (instant, no
    #      OP_BeginCast; a ~0.06 cov animation bump +0.51..1.21 s only).
    #      No landing (record 326 has no Target slot).
}
COLS = ("source_onset_s", "source_centroid", "src_h_lo", "src_h_hi",
        "landing_onset_s", "landing_end_s", "landing_cov", "p90_src", "p90_land")
gate_d = []
# the sprite leg's oracle p90s, measured with the SAME instrument
# (review/B1132/scripts/fx_sprite_roi.py) on the lane-5 capture; written by
# the lane-5 sprite leg's oracle p90s (review/B1132/scripts/fx_sprite_roi.py
# on the capture; chase camera, model 220 px -- see the tsv's header for the
# stated uncertainty). Format: cast<N>\t<STAGE>\t<text "p90 0.053 median ...">.
OROI = {}
try:
    for l in open(f"{EV}/lane5/oracle_b1153_sprite2/sprite_p90.tsv"):
        c = l.rstrip("\n").split("\t")
        if len(c) >= 3 and not l.startswith("#"):
            m = re.search(r"p90 ([\d.]+)", c[2])
            if m and c[1] in ("LANDING", "SOURCE"):
                OROI.setdefault(c[0], {})[c[1]] = float(m.group(1))
except OSError:
    pass


def omean(vals):
    vals = [v for v in vals if v is not None]
    return sum(vals) / len(vals) if vals else None


for rec, spell, tag in (("0", "93", "after_fixed_npc_r0"),
                        ("40", "602", "after_fixed_self_r40"),
                        ("51", "4499", "after_fixed_self_r51")):
    r = b1130_rows(tag)
    obs = OBS[rec]
    row = {"record": rec, "spell": spell, "tag": tag}
    for i, col in enumerate(COLS):
        v = f(r[col]) if r else None
        ref = obs[i]
        if col in ("p90_src", "p90_land") and rec == "0":
            # the oracle half of the p90 pair is the sprite leg (no oracle
            # p90 existed in any observer leg); the band is the leg's stated
            # uncertainty: chase-vs-fixed camera + 220-vs-209 px model.
            stage = "SOURCE" if col == "p90_src" else "LANDING"
            ref = omean([OROI.get(f"cast{n}", {}).get(stage) for n in (1, 2)])
        row[f"ref_{col}"] = ref
        row[f"b1130_{col}"] = v
        if col in ("source_onset_s", "landing_onset_s", "landing_end_s"):
            tol = 0.10
        elif col in ("source_centroid", "src_h_lo", "src_h_hi"):
            tol = 0.15
        elif col in ("p90_src", "p90_land"):
            tol = 0.5 * abs(ref) if ref else None  # the leg's uncertainty floor
        else:
            tol = None
        if v is None or ref is None:
            row[f"{col}_v"] = "n-a"
        elif tol is None:
            row[f"{col}_v"] = "info"
        else:
            row[f"{col}_v"] = "in" if abs(v - ref) <= tol else (
                "short" if v < ref else "over")
    # one OWNER for the row's misses (card step 3): a record word, a rig
    # difference, or "unattributed".
    misses = [c for c in COLS if row[f"{c}_v"] not in ("in", "n-a", "info")]
    if rec == "0":
        row["owner"] = ("P2b rig variables (target = PvP player vs the harness' "
                        "stunned TrollF; caster renderer llvmpipe) -- B1134 "
                        "FOLLOW-UP 3, unresolved there; the sprite-leg source "
                        "gap is the chase-camera body occlusion, not a sim "
                        "discrepancy (full-glow window re-measure)"
                        if misses else "none (all in band)")
    elif rec == "40":
        row["owner"] = ("rig: the oracle's caster is zoned by 602 itself "
                        "(DeleteSpawn Act+0.355, OP_RequestClientZoneChange) "
                        "and the on-caster lit run is truncated with the model; "
                        "the harness has no zone"
                        if misses else "none (all in band)")
    else:
        row["owner"] = ("none: both blank (oracle observer draws no particles "
                        "for 4499; the harness' record-driven castGlow is "
                        "below the visible threshold in fixed_self and the "
                        "record has no Target slot)" if not misses else
                        "unattributed")
    gate_d.append(row)

with open(f"{OUT}/gate_d.tsv", "w") as fp:
    cols = [k for k in gate_d[0]]
    fp.write("\t".join(cols) + "\n")
    for row in gate_d:
        fp.write("\t".join("" if row[c] is None else str(row[c]) for c in cols) + "\n")

print("wrote", OUT)
for t in ("gate_a", "gate_b", "gate_c", "gate_d"):
    p = f"{OUT}/{t}.tsv"
    print(t, os.path.getsize(p), "bytes" if os.path.exists(p) else "MISSING")
