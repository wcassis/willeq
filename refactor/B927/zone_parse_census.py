#!/usr/bin/env python3
"""B927: tabulate the ledger's zone_parse (and every other budgeted category)
over a zone_content_sweep.sh output directory.

Input: <sweep>/results.tsv, <sweep>/logs/<zone>.log (the benchmark's
[memledger] lines, one per lifecycle point) and <sweep>/logs/<zone>.mem.json
(the memory JSON visual_gate.py asserts the budgets against; its "ledger"
object is the benchmark_end snapshot, taken after the frames).

Output:
  zone_parse_census.tsv  one row per zone: zone_parse live bytes at every
                         lifecycle point the benchmark emits, the category's
                         high-water mark (peak, which includes every transient
                         between points -- the true mid-load peak), the value
                         the gate ASSERTS (memory JSON, benchmark_end), and
                         s3dZoneSourceBytes (memory JSON).
  category_census.tsv    one row per zone x budgeted category: asserted live
                         and high-water peak.

Usage: zone_parse_census.py <sweep_dir> [<sweep_dir> ...] <out_dir>
(several sweep dirs: the sweep run in parallel shards with --zones)
"""
import csv
import json
import os
import re
import sys

POINTS = ["startup", "zone_parse", "mesh_build_textures", "objects",
          "spatial_bsp", "entities", "zone_load_complete", "ready", "steady"]

LINE_RE = re.compile(r"^\[memledger\] point=(\S+) ")


def parse_log(path):
    """{point: {category: (live, peak)}} from the [memledger] lines."""
    out = {}
    try:
        with open(path, errors="replace") as f:
            for line in f:
                m = LINE_RE.match(line)
                if not m:
                    continue
                cats = {}
                for tok in line.split():
                    if "=" not in tok:
                        continue
                    k, v = tok.split("=", 1)
                    if "/" in v:
                        a, b = v.split("/", 1)
                        if a.isdigit() and b.isdigit():
                            cats[k] = (int(a), int(b))
                out[m.group(1)] = cats
    except FileNotFoundError:
        pass
    return out


def main():
    sweeps, out_dir = sys.argv[1:-1], sys.argv[-1]
    rows = []
    for sweep in sweeps:
        for r in csv.DictReader(open(os.path.join(sweep, "results.tsv")), delimiter="\t"):
            r["_dir"] = sweep
            rows.append(r)
    zp_rows, cat_rows = [], []
    for r in rows:
        zone, sweep = r["zone"], r["_dir"]
        pts = parse_log(os.path.join(sweep, "logs", zone + ".log"))
        mem_path = os.path.join(sweep, "logs", zone + ".mem.json")
        mem = json.load(open(mem_path)) if os.path.exists(mem_path) else None
        zp = {"zone": zone, "expansion": r["expansion"], "status": r["status"],
              "clean": r["clean"]}
        for p in POINTS:
            zp["live@" + p] = pts.get(p, {}).get("zone_parse", ("", ""))[0]
        # peak: the high-water mark at the LAST point the log carries
        last = next((pts[p] for p in reversed(POINTS) if p in pts), {})
        zp["peak_high_water"] = last.get("zone_parse", ("", ""))[1]
        lives = [zp["live@" + p] for p in POINTS if zp["live@" + p] != ""]
        zp["max_live_over_points"] = max(lives) if lives else ""
        if mem:
            cats = mem["ledger"]["categories"]
            zp["asserted_live"] = cats["zone_parse"]["live"]
            zp["s3dZoneSourceBytes"] = mem["s3dZoneSourceBytes"]
            zp["budget"] = mem["ledgerBudgets"]["zone_parse"]
            for name, budget in mem["ledgerBudgets"].items():
                if not budget:
                    continue
                c = cats.get(name, {})
                cat_rows.append({"zone": zone, "category": name, "budget": budget,
                                 "asserted_live": c.get("live", ""),
                                 "peak_high_water": c.get("peak", "")})
            for est in ("s3dZoneSourceBytes", "zoneGeometryBytes", "placeableObjectsBytes",
                        "characterModelsBytes", "gpuTextureBytes", "vboEboBytes",
                        "textureCacheBytesUsed", "eqe1MappedBytes", "totalEstimate"):
                cat_rows.append({"zone": zone, "category": "estimate:" + est, "budget": "",
                                 "asserted_live": mem.get(est, ""), "peak_high_water": ""})
        else:
            zp["asserted_live"] = zp["s3dZoneSourceBytes"] = zp["budget"] = ""
        zp_rows.append(zp)

    zp_fields = (["zone", "expansion", "status", "clean"] + ["live@" + p for p in POINTS]
                 + ["max_live_over_points", "peak_high_water", "asserted_live", "s3dZoneSourceBytes", "budget"])
    with open(os.path.join(out_dir, "zone_parse_census.tsv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=zp_fields, delimiter="\t", lineterminator="\n")
        w.writeheader()
        for zp in sorted(zp_rows, key=lambda z: -(z["asserted_live"] or 0)):
            w.writerow(zp)
    with open(os.path.join(out_dir, "category_census.tsv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["zone", "category", "budget", "asserted_live",
                                          "peak_high_water"],
                           delimiter="\t", lineterminator="\n")
        w.writeheader()
        for c in cat_rows:
            w.writerow(c)
    print(f"{len(zp_rows)} zones, {sum(1 for z in zp_rows if z['asserted_live'] != '')} with a memory JSON")


if __name__ == "__main__":
    main()
