#!/usr/bin/env python3
"""B929 (#247): the zone budget census.

Acceptance 2 of GitLab issue #247: a repeatable check that fails on ANY of the
era zones over ANY budget. The gate (scripts/ci/visual_gate.sh) asserts the
five manifest zones only; the offline zone-content sweep
(scripts/ci/zone_content_sweep.sh, B852) loads every zone in
scripts/ci/zone_content_sweep_zones.tsv (118) but only checks load cleanliness
and the 256 MB RssAnon ceiling, so a zone that loads fine yet exceeds its
memory budget (B929's growthplane: 28,687,788 B of placeableObjectsBytes
against 25,165,824, refactor/B927/category_census.tsv) was invisible to CI.

This checker reads the sweep's per-zone logs/<zone>.mem.json -- the same file
visual_gate.py asserts the budgets against -- and asserts, for every swept
zone:

  1. every ESTIMATE budget pair, i.e. every pair in
     visual_gate.py's MEMORY_BUDGET_PAIRS EXCEPT the two that are also
     exact-baselined there (textureCacheBytesUsed / framebufferBytes, which
     the gate's exact-match check covers):
       placeableObjectsBytes, zoneGeometryBytes, s3dZoneSourceBytes,
       vboEboBytes, characterModelsBytes, gpuTextureBytes,
       eqe1MappedBytes, totalEstimate.
     The budget is the one the benchmark read out of
     ConstrainedRendererConfig (the same values the gate compares against),
     so the budget file stays the single source: nothing is re-derived here.
  2. every ledger budget in the run's ledgerBudgets object
     (config/constrained_presets.json memoryLedgerBudgets): the category's
     live bytes at the benchmark_end snapshot must not exceed its budget,
     the same point and the same comparison the gate's assert_memory_ledger()
     makes on its five zones.

The budgets are BLESS-ONLY (Will): this script reads them and fails against
them; it never edits them, and a zone that is over its budget is a code
defect to fix, not a number to move (the ratchet-down rule, docs/ci.md).

Zone verdicts:
  ok          loaded, every budget held
  over_budget loaded, at least one budget exceeded (the check fails)
  missing     the sweep did not produce a memory JSON (its results.tsv row
              says why) -- the check fails: an unasserted zone is not an
              asserted zone
  not_loaded  results.tsv says the run did not reach a clean load -- the
              budget is meaningless for a zone that FATALed; the check
              fails until the zone loads (that is B852's sweep verdict,
              surfaced here so one script owns the budget question)

Usage:
  zone_budget_census.py --check-budgets --sweep-dir <sweep out-dir> [--zones z1,z2]
  zone_budget_census.py --check-budgets --sweep-dir <dir1> [<dir2> ...]

Exit codes:
  0  every zone present in the sweep dir(s) is ok
  1  at least one zone is over_budget / missing / not_loaded
  2  the sweep dir has no results.tsv, or a listed zone has neither a JSON
     nor a results.tsv row (the input is not a sweep output)
  3  bad usage

The output is one TSV row per zone x budgeted category plus a per-zone
verdict summary; the row layout extends refactor/B927/zone_parse_census.py's
category_census.tsv (zone, category, budget, asserted_live, peak_high_water)
with a status column, so the two stay readable side by side.
"""
import argparse
import csv
import json
import os
import sys

# The ESTIMATE pairs, from visual_gate.py's MEMORY_BUDGET_PAIRS minus the two
# whose usage is ALSO exact-baselined there (MEMORY_BUDGET_PAIRS_ALSO_BASELINED
# = textureCacheBytesUsed / framebufferBytes -- the gate's exact-match on its
# five zones covers those, so the census asserts the remaining eight).
ESTIMATE_PAIRS = [
    ("gpuTextureBytes", "gpuTextureBudgetBytes"),
    ("zoneGeometryBytes", "zoneGeometryBudgetBytes"),
    ("placeableObjectsBytes", "placeableObjectsBudgetBytes"),
    ("s3dZoneSourceBytes", "s3dZoneSourceBudgetBytes"),
    ("vboEboBytes", "vboEboBudgetBytes"),
    ("characterModelsBytes", "characterModelsBudgetBytes"),
    ("eqe1MappedBytes", "eqe1MappedBudgetBytes"),
    ("totalEstimate", "totalEstimateBudgetBytes"),
]


def load_mem(sweep_dir, zone):
    path = os.path.join(sweep_dir, "logs", zone + ".mem.json")
    try:
        with open(path) as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return None


def sweep_status(results_rows, zone):
    """The results.tsv status for the zone ('' when absent)."""
    for row in results_rows:
        if row.get("zone") == zone:
            return row.get("status", "")
    return ""


def check_zone(zone, mem, status):
    """(verdict, rows, over) for one zone. rows = category rows to print."""
    rows, over = [], []
    if mem is None:
        verdict = "not_loaded" if (status and status != "ok") else "missing"
        rows.append({"zone": zone, "category": "(no memory JSON)",
                     "budget": "", "asserted_live": "", "peak_high_water": "",
                     "status": verdict})
        return verdict, rows, over
    # Estimate pairs: usage <= budget at the benchmark_end snapshot.
    for use, bud_key in ESTIMATE_PAIRS:
        budget = int(mem.get(bud_key, 0) or 0)
        live = int(mem.get(use, 0) or 0)
        if not budget:
            # The active preset configures no budget for this category: the
            # gate prints and skips it (visual_gate.py assert_memory_budgets,
            # budget 0), and so does the census -- asserting usage <= 0 would
            # fail a category the preset does not bound.
            status_i = "no_budget"
            if live > 0:
                pass  # reported, not a failure
        elif live > budget:
            status_i = "over"
            over.append(f"{use} {live} > {budget}")
        else:
            status_i = "ok"
        rows.append({"zone": zone, "category": "estimate:" + use,
                     "budget": budget, "asserted_live": live,
                     "peak_high_water": "", "status": status_i})
    # Ledger budgets: live at the benchmark_end snapshot, the same comparison
    # the gate's assert_memory_ledger() makes on its five zones.
    cats = mem.get("ledger", {}).get("categories", {})
    for name, budget in mem.get("ledgerBudgets", {}).items():
        budget = int(budget or 0)
        if not budget:
            continue
        c = cats.get(name, {})
        live = int(c.get("live", 0) or 0)
        peak = c.get("peak", "")
        status_i = "over" if live > budget else "ok"
        if status_i == "over":
            over.append(f"ledger {name} live {live} > {budget}")
        rows.append({"zone": zone, "category": name, "budget": budget,
                     "asserted_live": live, "peak_high_water": peak,
                     "status": status_i})
    verdict = "over_budget" if over else "ok"
    return verdict, rows, over


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check-budgets", action="store_true",
                    help="assert every swept zone against every budget (the "
                         "B929 net); without it this script just prints")
    ap.add_argument("--sweep-dir", action="append", default=[],
                    help="a zone_content_sweep.sh output directory (repeatable "
                         "for sharded runs, as refactor/B927/zone_parse_census.py "
                         "takes several)")
    ap.add_argument("--zones", default="",
                    help="comma list: check only these (default: every zone "
                         "in the sweep dir(s))")
    ap.add_argument("--out", default="",
                    help="write the row TSV here (default: stdout only)")
    args = ap.parse_args()

    if not args.sweep_dir:
        print("FATAL: need at least one --sweep-dir", file=sys.stderr)
        return 3
    for d in args.sweep_dir:
        if not os.path.isfile(os.path.join(d, "results.tsv")):
            print(f"FATAL: {d} is not a sweep output dir (no results.tsv) -- "
                  "produce it with scripts/ci/zone_content_sweep.sh", file=sys.stderr)
            return 2

    want = set(z for z in args.zones.split(",") if z)
    all_rows, verdicts, zones_seen = [], {}, []
    for d in args.sweep_dir:
        with open(os.path.join(d, "results.tsv"), newline="") as f:
            results_rows = list(csv.DictReader(f, delimiter="\t"))
        for row in results_rows:
            zone = row.get("zone", "")
            if not zone or (want and zone not in want):
                continue
            if zone in verdicts:
                continue  # a zone swept in two shards: first dir wins
            mem = load_mem(d, zone)
            verdict, rows, over = check_zone(zone, mem, row.get("status", ""))
            verdicts[zone] = (verdict, over)
            all_rows.extend(rows)
            zones_seen.append(zone)

    # A --zones name with neither JSON nor results row is a usage error, not
    # a hollow pass (the sweep script's own --zones rule, zone_content_sweep.sh).
    for z in sorted(want):
        if z not in verdicts:
            print(f"FATAL: --zones names unknown zone '{z}' (no results.tsv "
                  f"row in any --sweep-dir)", file=sys.stderr)
            return 3

    # Print.
    print(f"== zone budget census: {len(zones_seen)} zones ==")
    for r in all_rows:
        if r["status"] in ("ok", "no_budget"):
            continue  # the rows that matter are the failures; full TSV via --out
        print(f"  OVER/UNASSERTED: {r['zone']:14s} {r['category']:24s} "
              f"live={r['asserted_live']} budget={r['budget']} status={r['status']}")
    for zone in zones_seen:
        verdict, over = verdicts[zone]
        detail = "; ".join(over) if over else ""
        print(f"  {zone:14s} {verdict}" + (f"  [{detail}]" if detail else ""))

    if args.out:
        with open(args.out, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=["zone", "category", "budget",
                                              "asserted_live", "peak_high_water",
                                              "status"],
                               delimiter="\t", lineterminator="\n")
            w.writeheader()
            for r in all_rows:
                w.writerow(r)
        print(f"rows: {args.out}")

    if not args.check_budgets:
        return 0
    bad = [z for z in zones_seen if verdicts[z][0] != "ok"]
    if bad:
        print(f"FAIL: {len(bad)} of {len(zones_seen)} zones not inside every "
              f"budget: {', '.join(bad)}", file=sys.stderr)
        return 1
    print(f"PASS: all {len(zones_seen)} zones inside every budget")
    return 0


if __name__ == "__main__":
    sys.exit(main())
