"""B811 regression net: a held-B step must not score when the character was
already holding the claimed item.

The defect, measured on T494's committed corpus timeline
(scripts/llmbench/timeline_legH.tsv): p3's `E_heldA_p3_*` rows (lines 24, 27)
record the character ALREADY holding 9997 at slot 13; its `/equip 23 13` step
(line 31, label H0_equip_p3) claims `held=13=9997`; and the `E_heldB_p3_*`
rows (lines 32, 35) claim `13=9997 Dagger* (IT5) moved from slot 23`.  Before
this net, nothing in the pipeline asserted the BEFORE and AFTER states differ:

  * compile_timeline.py compiled the timeline and record.sh recorded every
    frame;
  * verify_labels.py's db_inventory check compared the held-B claim
    (`13=9997`) against shared/db_before/inventory.tsv, found 9997 at slot 13,
    and printed `db_inventory=matches 13=9997 [PASS]` -- scoring the no-op
    move as a success, because the state it claimed to have just CREATED was
    the state the run STARTED in.

That is exactly the trap the live `equip_observed` scenario refuses
(src/client/scenario/scenario_equip_observed.cpp:100-109): baseline non-empty
means "this run could not tell an equip from a character that was wearing it
all along" -> Fail, never Pass.

These tests drive the production code on the committed legH rows: the compile
gate (timeline_held_check.check_held_steps, wired into compile_timeline.py)
must reject the no-op step, and the verifier (verify_labels.py) must mark its
held-B rows' db_inventory check [FAIL] instead of PASS.

Registered as LlmBenchHeldBeforeAfterHygiene in tests/cmake/hygiene.cmake
(label "hygiene"): pure python over committed TSVs, no server, no artefact.
"""
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
LEGH = os.path.join(HERE, "timeline_legH.tsv")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


thc = load("thc", os.path.join(HERE, "timeline_held_check.py"))


def issues(rows):
    """The production compile gate's verdict: {player: [(lineno, label, reason)]}."""
    return thc.check_held_steps(rows)


def legH_rows():
    return thc.parse_timeline(LEGH)


class LegHCompileGateTest(unittest.TestCase):
    """AC2/AC3: the legH row-27 situation, fed to the compile-time check."""

    def test_defect_row_is_rejected(self):
        rows = legH_rows()
        bad = issues(rows)
        # The defect is p3 line 31 (H0_equip_p3): its own slot 13 already
        # carried 9997 per the heldA rows at lines 24 and 27.
        p3 = bad.get("p3", [])
        self.assertEqual(len(p3), 1, f"exactly one p3 step must be INVALID, got {bad}")
        n, lab, why = p3[0]
        self.assertEqual(n, 31, f"the rejected step is the /equip row itself, got {n}")
        self.assertEqual(lab, "H0_equip_p3")
        self.assertIn("already held item 9997", why)
        self.assertIn("moved nothing", why)

    def test_p2_equip_is_not_rejected(self):
        # p2 genuinely starts empty-handed at slot 13 (14337 staff); its
        # /equip 23 13 does move the seeded 9997, so it must survive.
        rows = legH_rows()
        bad = issues(rows)
        p2 = bad.get("p2", [])
        self.assertEqual(p2, [], f"p2's equip is a real move and must not be rejected: {p2}")

    def test_verdict_is_explicit_and_loud(self):
        # AC3: BEFORE == AFTER -> rejected non-zero, never scored.  Run the
        # standalone gate the way record.sh's preflight would.
        r = subprocess.run([sys.executable, os.path.join(HERE, "timeline_held_check.py"), LEGH],
                           capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0, "the legH timeline must not compile clean")
        self.assertIn("INVALID", r.stdout)
        self.assertIn("H0_equip_p3", r.stdout)
        self.assertIn("B811", r.stdout)

    def test_counterfactual_timeline_passes(self):
        # If the character had started with slot 13 EMPTY, the same /equip
        # step scores fine: the row before the equip must differ from the
        # row after it.  Drop p3's heldA rows and the gate accepts the file.
        with open(LEGH) as f:
            lines = f.read().splitlines()
        kept = []
        for line in lines:
            parts = line.split("\t")
            if (len(parts) >= 4 and parts[1] == "p3"
                    and "E_heldA_p3" in parts[3]
                    and "held=13=9997" in parts[3]):
                continue
            kept.append(line)
        with tempfile.NamedTemporaryFile("w", suffix=".tsv", delete=False) as f:
            f.write("\n".join(kept) + "\n")
            name = f.name
        try:
            self.assertEqual(issues(thc.parse_timeline(name)), {},
                             "no p3 held= row precedes the /equip, so the move is genuine")
        finally:
            os.unlink(name)


def make_corpus(tmp):
    """A recorded corpus in exactly the legH p3 situation: p3 held 9997 at
    slot 13 at the START of the run (the server's db_before snapshot), the
    /equip step claimed it, and the held-B frame claims the move."""
    os.makedirs(os.path.join(tmp, "players", "p1"), exist_ok=True)
    os.makedirs(os.path.join(tmp, "players", "p3"), exist_ok=True)
    os.makedirs(os.path.join(tmp, "shared", "db_before"), exist_ok=True)
    # character 68 = p3; slot 13 already holds 9997 BEFORE the run.
    with open(os.path.join(tmp, "shared", "db_before", "inventory.tsv"), "w") as f:
        f.write("68\t13\t9997\t0\n68\t14\t9979\t0\n")
    truth = (
        "12:00:22.000\tlabel=H0_equip_p3;actor=p3;step=30;player=p3;"
        "held=13=9997 Dagger* (IT5);expect=unlabelled;pending=1\n"
        "12:00:28.000\tlabel=E_heldB_p3_chase;actor=p3;step=31;player=p3;"
        "held=13=9997 Dagger* (IT5) moved from slot 23;expect=discrepancy:B725;pending=1\n"
    )
    with open(os.path.join(tmp, "players", "p3", "truth.tsv"), "w") as f:
        f.write(truth)
    with open(os.path.join(tmp, "players", "p1", "truth.tsv"), "w") as f:
        f.write("")
    man = {
        "players": [
            {"id": "p1", "client": "official",
             "client_txt": None, "marks": None, "truth": "players/p1/truth.tsv",
             "frames_dir": None, "run_log": None},
            {"id": "p3", "client": "willeq",
             "client_txt": "players/p3/client.txt", "marks": None,
             "truth": "players/p3/truth.tsv", "frames_dir": None,
             "run_log": None},
        ],
    }
    with open(os.path.join(tmp, "players", "p3", "client.txt"), "w") as f:
        f.write("character=Testbuddy\n")
    with open(os.path.join(tmp, "players", "p1", "client.txt"), "w") as f:
        f.write("character=Oraclealpha\n")
    with open(os.path.join(tmp, "manifest.json"), "w") as f:
        json.dump(man, f)
    return tmp


def verify_held_rows(corpus):
    """Run the production verify_labels.py and return {(player, label): checks}
    for the held-claiming rows, checks = {name: (measured, ok)}."""
    out = os.path.join(corpus, "verify_labels.tsv")
    r = subprocess.run([sys.executable, os.path.join(HERE, "verify_labels.py"),
                        corpus, "--out", out], capture_output=True, text=True)
    rows = {}
    with open(out) as f:
        for line in f.read().splitlines()[1:]:
            pid, lab, exp, claim, verdict, meas = line.split("\t")
            checks = {}
            for part in meas.split("; "):
                if "=" not in part:
                    continue
                name, rest = part.split("=", 1)
                # verify_labels.py prints a bare `k=v` for a passing check and
                # appends ` [FAIL]` for a failing one.
                ok = not rest.endswith(" [FAIL]")
                measured = rest[:-len(" [FAIL]")] if not ok else rest
                checks[name] = (measured, ok)
            rows[(pid, lab)] = (verdict, checks)
    return rows


class LegHVerifierGateTest(unittest.TestCase):
    """AC3 at the scoring point: the held-B row's db_inventory check must FAIL
    when the run started already holding the claimed item."""

    def test_heldB_row_is_invalid_not_pass(self):
        tmp = tempfile.mkdtemp()
        try:
            make_corpus(tmp)
            rows = verify_held_rows(tmp)
            self.assertIn(("p3", "E_heldB_p3_chase"), rows,
                          f"the held-B row must be evaluated: {rows.keys()}")
            verdict, checks = rows[("p3", "E_heldB_p3_chase")]
            db = checks.get("db_inventory")
            self.assertIsNotNone(db, f"the held-B row must carry a db_inventory check: {checks}")
            self.assertFalse(db[1],
                             "db_inventory must be [FAIL]: the character already "
                             "held the item at run start, so the step moved nothing "
                             "and the row can never carry a grading label")
            self.assertEqual(verdict, "FAIL",
                             f"the row's verdict must be FAIL (INVALID), got {verdict}")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_real_move_still_passes(self):
        # Counter-example: the run started with slot 13 EMPTY; the /equip step
        # really moved 9997 in.  db_before must then NOT carry the item, and
        # the held-B row scores PASS.
        tmp = tempfile.mkdtemp()
        try:
            make_corpus(tmp)
            with open(os.path.join(tmp, "shared", "db_before", "inventory.tsv"), "w") as f:
                f.write("68\t14\t9979\t0\n")   # slot 13 empty at run start
            rows = verify_held_rows(tmp)
            verdict, checks = rows[("p3", "E_heldB_p3_chase")]
            self.assertTrue(checks["db_inventory"][1],
                            f"a real move must still pass db_inventory: {checks}")
            self.assertEqual(verdict, "PASS", f"a real move must PASS: {checks}")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
