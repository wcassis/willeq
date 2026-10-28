#!/usr/bin/env python3
"""B1180 (#499): build the re-gate tables from the streamed measure outputs.

usage: build_tables.py <gate_after_dir> <worktree> <outdir>
  writes tour7.tsv, b1118.tsv, b1130.tsv (+ b1130_band.tsv via band_table.py)
"""
import os, re, subprocess, sys

EV, WT, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(OUT, exist_ok=True)

def read_tsv(p):
    rows = [l.rstrip("\n").split("\t") for l in open(p) if l.strip()]
    return rows[0], {r[0]: r for r in rows[1:]}

def num(x):
    try:
        v = float(x)
        return v
    except (TypeError, ValueError):
        return None

# ---- tour-7: mine vs B1171's table (before/after = pre-B1164, level 60) + oracle
hdr7, ref7 = read_tsv(os.path.join(WT, "refactor/B1171/tables/tour7_b1118.tsv"))
out = ["\t".join(["spell", "oracle_land_cov", "oracle_end",
                  "b1171_before_land_cov", "b1171_before_end", "b1171_before_src_cov",
                  "b1164_fix_land_cov",
                  "mine_land_cov", "mine_land_t", "mine_end", "mine_src_cov",
                  "mine_vs_b1171_before", "mine_vs_oracle"])]
miss = []
for sid in ["93", "38", "54", "656", "41", "253", "458"]:
    r = ref7.get(sid, [""] * len(hdr7))
    d = dict(zip(hdr7, r))
    mine_p = os.path.join(EV, "measure", f"tour_{sid}", "summary.tsv")
    if not os.path.exists(mine_p):
        miss.append(f"tour_{sid}")
        continue
    _, mrow = read_tsv(mine_p)
    m = mrow.get(sid)
    if m is None:
        miss.append(f"tour_{sid} (no row)")
        continue
    # summary.tsv: spell land_cov land_t fxt_cov end abs_on abs_end rowtop roi_l roi_s src_cov ...
    mine = [num(x) for x in m]
    lb, la = num(d.get("before_land_cov")), num(mine[1])
    orc = num(d.get("lane_oracle_land_cov"))
    out.append("\t".join([
        sid, d.get("lane_oracle_land_cov", ""), d.get("lane_oracle_end", ""),
        d.get("before_land_cov", ""), d.get("before_end", ""), d.get("before_src_cov", ""),
        "",  # b1164_fix from fix_b1118_table below
        f"{mine[1]:.3f}", m[2], m[4], f"{mine[10]:.3f}",
        f"{la - lb:+.3f}" if la is not None and lb is not None else "n/a",
        f"{orc / la:.2f}x" if orc and la else "n/a"]))
# b1164 fix column
hdrf, reff = read_tsv(os.path.join(WT, "refactor/B1164/fix_b1118_table.tsv"))
for i, row in enumerate(out[1:], start=1):
    c = row.split("\t")
    fr = reff.get(f"tour {c[0]}")
    if fr:
        c[6] = dict(zip(hdrf, fr)).get("land_after", "")
    out[i] = "\t".join(c)
open(os.path.join(OUT, "tour7.tsv"), "w").write("\n".join(out) + "\n")

# ---- b1118 40-record sweep: mine vs B1157 (the level-60 reference, both sides)
hdrb, reffb = read_tsv(os.path.join(WT, "refactor/B1157/b1118_table.tsv"))
out = ["\t".join(["row", "spell", "b1157_land_cov", "b1157_end", "b1157_src_cov", "b1157_verdict",
                  "mine_land_cov", "mine_land_t", "mine_end", "mine_src_cov",
                  "d_land_cov", "d_end"])]
for r in reffb.values():
    d = dict(zip(hdrb, r))
    if d.get("row", "").startswith("tour"):
        continue
    sid = d.get("spell", "")
    mine_p = os.path.join(EV, "measure", f"b1118_r{sid}", "summary.tsv")
    if not os.path.exists(mine_p):
        miss.append(f"b1118_r{sid}")
        out.append("\t".join([d["row"], sid, d.get("land_cov_n", ""), d.get("end_n", ""),
                              d.get("src_cov_n", ""), d.get("verdict", ""), "", "", "", "", ""]))
        continue
    _, mrow = read_tsv(mine_p)
    m = mrow.get(sid)
    if m is None:
        miss.append(f"b1118_r{sid} (no row)")
        continue
    mine = [num(x) for x in m]
    lb, le = num(d.get("land_cov_n")), num(d.get("end_n"))
    la, ea_ = mine[1], num(m[4])
    out.append("\t".join([
        d["row"], sid, d.get("land_cov_n", ""), d.get("end_n", ""), d.get("src_cov_n", ""), d.get("verdict", ""),
        f"{mine[1]:.3f}", m[2], m[4], f"{mine[10]:.3f}",
        f"{la - lb:+.3f}" if la is not None and lb is not None else "n/a",
        f"{ea_ - le:+.3f}" if ea_ is not None and le is not None else "n/a"]))
open(os.path.join(OUT, "b1118.tsv"), "w").write("\n".join(out) + "\n")

# ---- b1130: band_table.py on my EV (oracle banded verdicts) + join B1171 before/after.
# My gate names its measure dirs b1130_r<rec>; band_view.py lays them under
# band_table.py's <kind>_r<rec> names (B1155's) and writes b1130_band.tsv.
band = os.path.join(WT, "refactor/B1180/scripts/band_view.py")
subprocess.run([sys.executable, band, EV, WT, OUT], check=False)
hdr17, ref17 = read_tsv(os.path.join(WT, "refactor/B1171/tables/b1130_table.tsv"))
hdrbd = None
b1130_band_rows = {}
bp = os.path.join(OUT, "b1130_band.tsv")
if os.path.exists(bp):
    lines = [l.rstrip("\n").split("\t") for l in open(bp) if l.strip()]
    hdrbd = lines[0]
    if hdrbd[0] != "record":
        hdrbd = ["record", "kind"] + hdrbd
    for r in lines[1:]:
        if r and r[0].isdigit():
            b1130_band_rows[r[0]] = dict(zip(hdrbd, r))
out = ["\t".join(["record", "kind", "oracle_level",
                  "oracle_landing_cov", "b1171_before_landing_cov", "b1171_after_landing_cov",
                  "b1171_va_landing_cov",
                  "mine_landing_cov", "mine_landing_end_s", "mine_p90_land", "mine_source_cov",
                  "mine_verdict_landing_cov"])]
passc = tot = 0
for rec in sorted(b1130_band_rows, key=int):
    d = dict(zip(hdr17, ref17[rec])) if rec in ref17 else {}
    b = b1130_band_rows[rec]
    if b.get("verdict_landing_cov") == "PASS":
        passc += 1
    tot += 1
    out.append("\t".join([
        rec, b.get("kind", ""), d.get("oracle_level", ""),
        d.get("oracle_landing_cov", ""), d.get("before_landing_cov", ""), d.get("after_landing_cov", ""),
        d.get("va_landing_cov", ""),
        b.get("harness_landing_cov", ""), b.get("harness_landing_end_s", ""),
        b.get("harness_p90_land", ""), b.get("harness_source_cov", ""),
        b.get("verdict_landing_cov", "")]))
open(os.path.join(OUT, "b1130.tsv"), "w").write("\n".join(out) + "\n")

print(f"b1130 landing_cov PASS: {passc}/{tot}")
print(f"missing rows: {miss if miss else 'none'}")
