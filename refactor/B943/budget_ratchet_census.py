#!/usr/bin/env python3
"""B943: recompute the measured worst of six orangepi budgets and the B927
convention value (worst x 1.25, rounded up to a whole MiB).

Sources (all committed or on the review tree; nothing is re-measured):
  benchmark : refactor/B927/category_census.tsv (118-zone x86 census at 3a2762e2,
              the value the gate ASSERTS, benchmark_end)
  x86 client: refactor/B909/summary_before.md + summary_after.md (client columns
              zone_load_complete / ready / steady_60s, four zones)
  gate      : refactor/B943/gate_mem_json/*_mem.json, the memory JSON of BOTH
              gate invocations (golden = S3D path, no EQE1; coverage = EQE1) of
              the five gate zones, from a visual_gate.py run on a build with the
              pre-change texture budget (64 MiB). The golden qeynos2 run holds
              31,735,808 B of GPU textures and a 42,226,984 B cache, far above
              the census's EQE1-path figures; the gate asserts it.
  Pi client : <pi_sweep>/zones/*/details.json points zone_load_complete / ready /
              steady_60s (B854 rendered sweep, 45 zones plus retries)

Usage: budget_ratchet_census.py [repo_root] [pi_sweep_zones_dir]
"""
import csv, glob, json, math, os, re, sys

MIB = 1 << 20
ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..")
PI = sys.argv[2] if len(sys.argv) > 2 else "/mnt/projects/fable/review/B854-rendered/sweep-full/zones"
POINTS = ("zone_load_complete", "ready", "steady_60s")

# (label, current budget MiB, ledger category or estimate name)
BUDGETS = [
    ("memoryLedgerBudgets.archives", 4, "archives"),
    ("memoryLedgerBudgets.textures_decoded", 20, "textures_decoded"),
    ("memoryLedgerBudgets.entity_models", 84, "entity_models"),
    ("gpuTextureBytes", 64, "estimate:gpuTextureBytes"),
    ("textureCacheBytesUsed", 64, "estimate:textureCacheBytesUsed"),
    ("characterModelsBytes", 40, "estimate:characterModelsBytes"),
]


def bench():
    worst = {}
    for r in csv.DictReader(open(os.path.join(ROOT, "refactor/B927/category_census.tsv")), delimiter="\t"):
        if r["asserted_live"] == "":
            continue
        v = int(r["asserted_live"])
        if v > worst.get(r["category"], (-1, ""))[0]:
            worst[r["category"]] = (v, r["zone"])
    return worst


def gate():
    worst = {}
    for f in sorted(glob.glob(os.path.join(ROOT, "refactor/B943/gate_mem_json/*_mem.json"))):
        d = json.load(open(f))
        name = os.path.basename(f).replace("_mem.json", "")
        vals = {"estimate:" + k: d[k] for k in ("gpuTextureBytes", "textureCacheBytesUsed", "characterModelsBytes")}
        vals.update({c: v["live"] for c, v in d["ledger"]["categories"].items()})
        for k, v in vals.items():
            if v > worst.get(k, (-1, ""))[0]:
                worst[k] = (v, name)
    return worst


def x86_client():
    worst = {}
    for fn in ("summary_before.md", "summary_after.md"):
        zone = None
        for line in open(os.path.join(ROOT, "refactor/B909", fn)):
            m = re.match(r"^### (\S+)", line)
            if m:
                zone = m.group(1)
                continue
            m = re.match(r"^\| `(\w+)` \|", line)
            if not m or zone is None:
                continue
            cols = [c.strip() for c in line.strip().strip("|").split("|")]
            # cols: name, budget, zlc, ready, %, steady, %, ...
            for idx in (2, 3, 5):
                v = int(re.sub(r"[^0-9]", "", cols[idx].split("**")[0]) or 0)
                if v > worst.get(m.group(1), (-1, ""))[0]:
                    worst[m.group(1)] = (v, f"{zone} ({fn})")
    return worst


def pi_client():
    worst = {}
    for p in sorted(glob.glob(os.path.join(PI, "*", "details.json"))):
        zone = os.path.basename(os.path.dirname(p))
        d = json.load(open(p))
        for pt in POINTS:
            cats = d.get("points", {}).get(pt, {}).get("categories", {})
            for k, v in cats.items():
                if v > worst.get(k, (-1, ""))[0]:
                    worst[k] = (v, f"{zone} @{pt}")
    return worst


def main():
    b, x, p, g = bench(), x86_client(), pi_client(), gate()
    hdr = ["budget", "now MiB", "worst benchmark", "worst x86 client", "worst Pi client", "worst gate run",
           "worst B", "worst source", "x1.25 B", "new MiB", "new B", "verdict"]
    print("\t".join(hdr))
    for label, now, key in BUDGETS:
        cands = []
        for tag, src in (("benchmark", b), ("x86 client", x), ("Pi client", p), ("gate", g)):
            if key in src:
                cands.append((src[key][0], f"{tag}: {src[key][1]}"))
        cell = lambda src: f"{src[key][0]:,} ({src[key][1]})" if key in src else "-"
        w, ws = max(cands)
        conv = math.ceil(w * 1.25 / MIB)
        verdict = "LOWER" if conv < now else ("SAME" if conv == now else "HIGHER: do not raise, file finding")
        print("\t".join([label, str(now), cell(b), cell(x), cell(p), cell(g), f"{w:,}", ws,
                         f"{w * 1.25:,.0f}", str(conv), f"{conv * MIB:,}", verdict]))


GATE = ("qeynos2", "gfaydark", "befallen", "iceclad", "timorous")


def headroom():
    """Every measured zone and the five gate zones against the NEW budgets, at
    the asserted point (category_census.tsv, taken at 3a2762e2)."""
    new = {key: c for (_, _, key), c in
           zip(BUDGETS, [3, 2, 38, 38, 51, 2])}
    rows = {}
    for r in csv.DictReader(open(os.path.join(ROOT, "refactor/B927/category_census.tsv")), delimiter="\t"):
        if r["asserted_live"] != "":
            rows.setdefault(r["category"], {})[r["zone"]] = int(r["asserted_live"])
    print()
    print("headroom against the new budget at the asserted point (census)")
    print("\t".join(["category", "zones", "over budget", "worst zone %"] + [z + " %" for z in GATE]))
    for key, mib in new.items():
        b = mib * MIB
        z = rows[key]
        over = [k for k, v in z.items() if v > b]
        wz = max(z, key=z.get)
        print("\t".join([key, str(len(z)), ",".join(over) or "0",
                         f"{100 * z[wz] / b:.1f} ({wz})"] + [f"{100 * z[g] / b:.1f}" for g in GATE]))


def gate_headroom():
    new = {key: c for (_, _, key), c in zip(BUDGETS, [3, 2, 38, 38, 51, 2])}
    print()
    print("gate invocations (golden + coverage JSON) against the new budget, % of budget")
    print("\t".join(["file"] + list(new)))
    for f in sorted(glob.glob(os.path.join(ROOT, "refactor/B943/gate_mem_json/*_mem.json"))):
        d = json.load(open(f))
        vals = {"estimate:" + k: d[k] for k in ("gpuTextureBytes", "textureCacheBytesUsed", "characterModelsBytes")}
        vals.update({c: v["live"] for c, v in d["ledger"]["categories"].items()})
        print("\t".join([os.path.basename(f)] + [
            ("n/a (0)" if vals[k] == 0 else f"{100 * vals[k] / (m * MIB):.1f}") for k, m in new.items()]))


main()
headroom()
gate_headroom()
