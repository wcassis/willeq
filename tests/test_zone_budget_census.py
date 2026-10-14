#!/usr/bin/env python3
"""B929 (#247): unit tests for scripts/ci/zone_budget_census.py, the zone
budget census.

The census fails on ANY era zone over ANY budget (issue #247 acceptance 2);
the 118-zone run is the CI gate-stage job (zone-budget-census) and the 3-zone
smoke is zone_budget_census_smoke. This test pins the CHECKER itself,
offline: it builds synthetic sweep output in a temp dir and drives
zone_budget_census.py through its real exit-code contract (the CI job reads
exit 0/1 straight from it, so the contract is the net). No benchmark binary,
no EQ assets, no zone load.

What it covers:
  1. an estimate over its budget (B929's growthplane: placeableObjectsBytes
     28,687,788 > 25,165,824) -> exit 1, and the failing line names the
     figure; the same JSON with the value under budget -> exit 0.
  2. a ledger category over its budget -> exit 1 naming the category and its
     live bytes (the ledger comparison is the one the gate's
     assert_memory_ledger() makes on its five zones).
  3. a budget of 0 -> skipped, not a usage <= 0 failure (the gate's own rule:
     a preset that configures no budget for a category is not over it).
  4. a swept zone with no memory JSON -> exit 1 with a non-ok verdict (an
     unasserted zone is not an asserted zone).
  5. a --zones name absent from the sweep -> exit 3 (the sweep script's own
     loud-unknown-zone rule, not a hollow pass).
  6. the pair list is the gate's MEMORY_BUDGET_PAIRS minus the two
     also-baselined (textureCacheBytesUsed / framebufferBytes, whose usage
     the gate exact-matches): the census must never drift from the gate's
     estimate set, and must never double-assert the baselined pair.
"""
import csv
import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CEN = os.path.join(ROOT, "scripts", "ci", "zone_budget_census.py")
VG = os.path.join(ROOT, "scripts", "ci", "visual_gate.py")


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def write_sweep(out_dir, zones):
    """zones: {zone: {"status": str, "mem": dict-or-None}} -> sweep layout."""
    os.makedirs(os.path.join(out_dir, "logs"), exist_ok=True)
    with open(os.path.join(out_dir, "results.tsv"), "w", newline="") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["zone", "expansion", "status", "exit_code", "wall_s",
                    "fatal_count", "error_count", "first_fatal",
                    "missing_texture_count", "missing_eqe1_count",
                    "placeholder_model_count", "zone_load_complete",
                    "peak_rss_anon", "clean"])
        for zone, spec in zones.items():
            w.writerow([zone, "classic", spec["status"], "0", "4",
                        "0", "0", "", "0", "0", "0",
                        "y" if spec["status"] == "ok" else "n",
                        "100000000", "1" if spec["status"] == "ok" else "0"])
            if spec["mem"] is not None:
                with open(os.path.join(out_dir, "logs", zone + ".mem.json"), "w") as mf:
                    json.dump(spec["mem"], mf)


def run_census(sweep_dir, extra=()):
    out = io.StringIO()
    with redirect_stdout(out):
        proc = subprocess.run(
            [sys.executable, CEN, "--check-budgets", "--sweep-dir", sweep_dir,
             *extra],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return proc.returncode, out.getvalue() + proc.stdout + proc.stderr


class ZoneBudgetCensusTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="b929_census_")
        self.dir = self._tmp.name
        self.cen = load_module(CEN, "zone_budget_census_under_test")
        self.base_mem = {
            "placeableObjectsBytes": 100000,
            "placeableObjectsBudgetBytes": 25165824,
            "zoneGeometryBytes": 100000,
            "zoneGeometryBudgetBytes": 16777216,
            "s3dZoneSourceBytes": 100000,
            "s3dZoneSourceBudgetBytes": 14680064,
            "vboEboBytes": 100000,
            "vboEboBudgetBytes": 41943040,
            "characterModelsBytes": 100000,
            "characterModelsBudgetBytes": 2097152,
            "gpuTextureBytes": 100000,
            "gpuTextureBudgetBytes": 39845888,
            "eqe1MappedBytes": 100000,
            "eqe1MappedBudgetBytes": 268435456,
            "totalEstimate": 100000,
            "totalEstimateBudgetBytes": 134217728,
        }  # type: dict[str, object]

    def tearDown(self):
        self._tmp.cleanup()

    def _sweep(self, zones):
        d = os.path.join(self.dir, "sweep")
        write_sweep(d, zones)
        return d

    def test_estimate_over_budget_fails_naming_the_figure(self):
        # B929's own measured numbers: growthplane 28,687,788 against 25,165,824.
        mem = dict(self.base_mem)
        mem["placeableObjectsBytes"] = 28687788
        d = self._sweep({"growthplane": {"status": "ok", "mem": mem}})
        rc, out = run_census(d, ("--zones", "growthplane"))
        self.assertEqual(rc, 1, out)
        self.assertIn("growthplane", out)
        self.assertIn("28687788 > 25165824", out)
        self.assertIn("FAIL", out)

    def test_estimate_under_budget_passes(self):
        d = self._sweep({"qeynos2": {"status": "ok",
                                     "mem": dict(self.base_mem)}})
        rc, out = run_census(d, ("--zones", "qeynos2"))
        self.assertEqual(rc, 0, out)
        self.assertIn("PASS: all 1 zones inside every budget", out)

    def test_ledger_category_over_budget_fails(self):
        mem = dict(self.base_mem)
        mem["ledgerBudgets"] = {"objects": 41943040}
        mem["ledger"] = {"categories": {"objects": {"live": 42000000,
                                                    "peak": 43000000}}}
        d = self._sweep({"growthplane": {"status": "ok", "mem": mem}})
        rc, out = run_census(d, ("--zones", "growthplane"))
        self.assertEqual(rc, 1, out)
        self.assertIn("objects", out)
        self.assertIn("42000000 > 41943040", out)

    def test_ledger_category_under_budget_passes(self):
        mem = dict(self.base_mem)
        mem["ledgerBudgets"] = {"objects": 41943040}
        mem["ledger"] = {"categories": {"objects": {"live": 1648640,
                                                    "peak": 16937200}}}
        d = self._sweep({"growthplane": {"status": "ok", "mem": mem}})
        rc, out = run_census(d, ("--zones", "growthplane"))
        self.assertEqual(rc, 0, out)

    def test_zero_budget_is_skipped_not_a_failure(self):
        # The gate's rule (visual_gate.py assert_memory_budgets): a budget of
        # 0 means the preset configures none for the category; assert
        # usage <= 0 would fail every zone the preset does not bound.
        mem = dict(self.base_mem)
        mem["vboEboBudgetBytes"] = 0
        d = self._sweep({"qeynos2": {"status": "ok", "mem": mem}})
        rc, out = run_census(d, ("--zones", "qeynos2"))
        self.assertEqual(rc, 0, out)

    def test_zone_without_memory_json_is_not_ok(self):
        # A zone the sweep could not load (no benchmark_end JSON) cannot have
        # been asserted against any budget: it must surface, not pass.
        d = self._sweep({"soldunga": {"status": "failed", "mem": None}})
        rc, out = run_census(d, ("--zones", "soldunga"))
        self.assertEqual(rc, 1, out)
        self.assertIn("soldunga", out)
        self.assertIn("not_loaded", out)

    def test_unknown_zone_name_fails_loud(self):
        d = self._sweep({"qeynos2": {"status": "ok",
                                     "mem": dict(self.base_mem)}})
        rc, out = run_census(d, ("--zones", "nope"))
        self.assertEqual(rc, 3, out)
        self.assertIn("unknown zone", out)

    def test_estimate_pair_set_matches_the_gate(self):
        # The census asserts the gate's estimate pairs MINUS the two whose
        # usage the gate also exact-baselines. Import visual_gate.py
        # LAZILY (module import pulls PIL, which a dev machine may lack) and
        # compare, so a gate edit that adds a budgeted pair shows up as a red
        # census instead of a silently unasserted zone.
        gate = load_module(VG, "visual_gate_under_test")
        pair_set = {(u, b) for u, b, _label in gate.MEMORY_BUDGET_PAIRS}
        base_pairs = {(u, b) for u, b in pair_set
                      if u not in gate.MEMORY_BUDGET_PAIRS_ALSO_BASELINED}
        self.assertEqual({(u, b) for u, b in self.cen.ESTIMATE_PAIRS},
                         base_pairs)
        # And the budgeted pair keys in the gate's pair list are real fields
        # the benchmark's memory JSON carries (writeMemoryJson,
        # tools/hwcaps/zone_bench_memory.cpp) -- checked against the JSON
        # printer's own source so a renamed key cannot slip past. The keys
        # appear C-escaped inside the fprintf format string
        # (\"s3dZoneSourceBytes\"), so the search is on the bare key.
        src = open(os.path.join(ROOT, "tools", "hwcaps",
                                "zone_bench_memory.cpp")).read()
        for use, bud in base_pairs:
            self.assertIn(use, src)
            self.assertIn(bud, src)

    def test_check_zone_reports_rows_for_the_tsv(self):
        mem = dict(self.base_mem)
        mem["placeableObjectsBytes"] = 28687788
        verdict, rows, over = self.cen.check_zone("growthplane", mem, "ok")
        self.assertEqual(verdict, "over_budget")
        self.assertEqual(len(over), 1)
        by_cat = {r["category"]: r for r in rows}
        self.assertEqual(by_cat["estimate:placeableObjectsBytes"]["status"],
                         "over")
        self.assertEqual(by_cat["estimate:placeableObjectsBytes"]
                         ["asserted_live"], 28687788)
        self.assertEqual(by_cat["estimate:placeableObjectsBytes"]["budget"],
                         25165824)


if __name__ == "__main__":
    unittest.main()
