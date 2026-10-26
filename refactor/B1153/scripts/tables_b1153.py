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

EV = "/home/user/kb-evidence/hme/h1122"
EV = "/home/131/hme/h1122"
EV = "/home/131/kb-evidence/kb-b1153-retire"
REPO = "/home/131/src/willeq-wt/kb-b1153-retire"
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
    return rows[0] if rows else None


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
    if c[0] == "spell" or c[0].startswith("#"):
        continue
    reg[c[0]] = c
# columns: 0 spell, 3 new_land_cov, 7 new_end, 11 new_roi_l, 15 new_roi_s,
# 27 new_land_cov_masked, 33 oracle_land_cov, 34 oracle_end, 35 roi_l_band, 37 roi_s_band
gate_a = []
for spell in ("38", "41", "54", "93", "253", "458", "656"):
    r = reg.get(spell, [None] * 40)
    oracle_land = f(r[3]) if len(r) > 3 else None
    oracle_end = f(r[7]) if len(r) > 7 else None
    roi_l_band = r[35] if len(r) > 35 else ""
    roi_s_band = r[37] if len(r) > 37 else ""
    row = {
        "spell": spell,
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
        land = f(s["land_cov"])
        end = f(s["end"])
        roi_l = f(s["roi_l"])
        roi_s = f(s["roi_s"])
        ratio = land / oracle_land if (land is not None and oracle_land) else None
        row[f"{label}_land"] = land
        row[f"{label}_end"] = end
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
    gate_a.append(row)

with open(f"{OUT}/gate_a.tsv", "w") as fp:
    cols = [k for k in gate_a[0]]
    fp.write("\t".join(cols) + "\n")
    for row in gate_a:
        fp.write("\t".join("" if row[c] is None else str(row[c]) for c in cols) + "\n")

# --------------------------------------------------------------------------
# gate (b): 40-record sweep b1118 vs the B1151 harness rows
# --------------------------------------------------------------------------
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

gate_b = []
for d in sorted(os.listdir(f"{REPO}/refactor/B1153/runs"),
                key=lambda x: int(re.sub(r"\D", "", x.split("_r")[-1].split("_")[0])
                                  if re.match(r"b1153_after_b1118_r\d", x) else 999)):
    m = re.match(r"b1153_after_b1118_(r\d+)_(\d+)$", d)
    if not m:
        continue
    rec, sid = m.groups()
    s = summary(d)
    if s is None:
        continue
    b = b1151.get(f"{rec}_{sid}")
    row = {"row": rec, "spell": sid}
    for col, tol in (("src_cov", 0.05), ("land_cov", 0.05), ("end", 0.10),
                     ("roi_l", 0.02), ("roi_s", 0.02)):
        v = f(s[col])
        ref = f(b[col]) if b else None
        row[f"b1153_{col}"] = v
        row[f"b1151_{col}"] = ref
        if v is None or ref is None:
            row[f"{col}_d"] = None
            row[f"{col}_v"] = "n-a"
        else:
            dd = v - ref
            row[f"{col}_d"] = dd
            row[f"{col}_v"] = "PASS" if abs(dd) <= tol else ("short" if dd < 0 else "over")
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
try:
    subprocess.run([sys.executable, band_py, f"{EV}/b1130_evidence", new_banded], check=True)
    new_rows = {l.split("\t")[0]: l.rstrip("\n").split("\t")
                for l in open(new_banded) if l and not l.startswith("#") and l.split("\t")[0] != "record"}
except Exception as e:  # noqa: BLE001
    new_rows = {}
    print(f"gate_c: band_table.py failed: {e}", file=sys.stderr)

old_rows = {}
if os.path.exists(B1155_BANDED):
    for l in open(B1155_BANDED):
        if l.startswith("#") or l.startswith("record\t"):
            continue
        c = l.rstrip("\n").split("\t")
        if c:
            old_rows[c[0]] = c

gate_c = []
if new_rows:
    header = [c for c in new_rows[[k for k in new_rows][0]]]
    # find the verdict column index for each measured column (pattern: <col>, <col>_v)
    verdict_idx = {i: header[i] for i, h in enumerate(header) if h.endswith("_v")}
    for rec in sorted(new_rows, key=lambda x: int(re.sub(r"\D", "", x) or 0)):
        row = list(new_rows[rec])
        old = old_rows.get(rec)
        changed = ""
        if old and len(old) == len(row):
            diffs = [header[i] for i in range(len(row))
                     if row[i] != old[i] and i in verdict_idx]
            changed = "; ".join(diffs)
        gate_c.append(row + [changed])
    cols = header + ["changed_vs_B1155"]
    with open(f"{OUT}/gate_c.tsv", "w") as fp:
        fp.write("\t".join(cols) + "\n")
        for row in gate_c:
            fp.write("\t".join(str(x) for x in row) + "\n")

# --------------------------------------------------------------------------
# gate (d): records 0 (93), 40 (602), 51 (4499) vs the B1134 observer rows
# --------------------------------------------------------------------------
# B1134 observer reference rows (review/B1134/README.md, pasted values):
OBS = {
    # record: (source_onset BC+, source_centroid, src_h_lo, src_h_hi,
    #          landing_onset RISE Act+, landing_end Act+, landing_cov RUN)
    "0":  (0.46, 0.71, 0.07, 1.25, 0.43, 6.67, 1.020),   # B1130 r51 row 0 (93)
    "40": (0.368, 0.66, None, None, 0.07, 0.73, None),   # P3 ports (602): min onset/centroid, row end
    "51": (0.70, 0.21, -0.05, 0.74, None, 8.16, None),   # P4/P4b/P5 (4499): row 51
}
COLS = ("source_onset_s", "source_centroid", "src_h_lo", "src_h_hi",
        "landing_onset_s", "landing_end_s", "landing_cov")
gate_d = []
for rec, spell, tag in (("0", "93", "after_fixed_npc_r0"),
                        ("40", "602", "after_fixed_self_r40"),
                        ("51", "4499", "after_fixed_self_r51")):
    r = b1130_rows(tag)
    obs = OBS[rec]
    row = {"record": rec, "spell": spell, "tag": tag}
    for i, col in enumerate(COLS):
        v = f(r[col]) if r else None
        ref = obs[i]
        row[f"obs_{col}"] = ref
        row[f"b1130_{col}"] = v
        if col in ("source_onset_s", "landing_onset_s", "landing_end_s"):
            tol = 0.10
        elif col in ("source_centroid", "src_h_lo", "src_h_hi"):
            tol = 0.15
        else:
            tol = None
        if v is None or ref is None:
            row[f"{col}_v"] = "n-a"
        elif tol is None:
            row[f"{col}_v"] = "info"
        else:
            row[f"{col}_v"] = "in" if abs(v - ref) <= tol else (
                "short" if v < ref else "over")
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
