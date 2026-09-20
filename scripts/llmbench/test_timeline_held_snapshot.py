"""B793 (issue #75) regression net: the llmbench timeline's held= claims must
come from the inventory snapshot, not from hand-typed constants.

The defect, measured on the committed Leg C timeline
(scripts/llmbench/timeline.tsv): `E_heldA_p2_*` claimed Testcaster holds
`13=9998 short sword,14=9979 lantern` while the run's own inventory snapshot
(shared/db_before/inventory.tsv, the authority verify_labels.py:133 reads)
says Testcaster holds 14337 / 20656 in slots 13 / 14.  Testbuddy's
`13=9997,14=9979` did match, so the verifier's db_inventory check
discriminates -- and the check found the p2 rows false at Leg G.  Worse, the
`/equip 23 13` the `E_heldB_*` steps relied on was REFUSED by the client
("Bread Cakes* cannot go in Primary (13) -- wrong slot type for this item":
slot 23 is food, per the snapshot), so every `held=13=9990` after-claim was
false too.

What this net drives:

  * make_timeline.build_timeline() -- the production generator, fed the
    committed T492 reference snapshot (scripts/llmbench/fixtures/T492/
    db_before/, a copy of the recorded run's <corpus>/shared/db_before/).
    Every held= claim it emits for the committed timeline must equal what
    the snapshot says, the /equip after-state must not be claimed, and a
    missing snapshot must be a loud error, never a fallback to constants.

  * scripts/llmbench/timeline.tsv -- the committed generated output.  It must
    carry the snapshot-derived values, not the old hand-typed ones.  This is
    the RED/GREEN half: on the pre-B793 tree the committed file still claims
    9998 where the snapshot says 14337, so the committed-file tests fail
    until the timeline is regenerated with the fixed generator.

Registered as LlmBenchTimelineHeldFromSnapshot in tests/cmake/hygiene.cmake
(label "hygiene"): pure python over committed TSVs, no server, no artefact.
"""
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
TIMELINE = os.path.join(HERE, "timeline.tsv")
FIXTURE = os.path.join(HERE, "fixtures", "T492", "db_before")
GENERATOR = os.path.join(HERE, "make_timeline.py")

# The snapshot's own answer (read from the fixture, asserted below against
# the generator, and then the committed timeline's claims are checked
# against it): the authority is the file, these are its rows.
SNAP = {
    # character: {slot: item}  (inventory.slot_id numbering, NOT converted)
    "Testcaster": {13: 14337, 14: 20656, 23: 9990},
    "Testbuddy": {13: 9997, 14: 9979, 23: 9990},
}


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


gen = load("make_timeline", GENERATOR)


def check_fixture():
    """Sanity: the constants this test checks are really the fixture's rows."""
    inv = gen.read_inventory(FIXTURE)
    chars = gen.read_characters(FIXTURE)
    for name, slots in SNAP.items():
        cid = chars[name]
        for slot, item in slots.items():
            got = inv.get(cid, {}).get(slot)
            assert got == item, (
                f"fixture {FIXTURE} no longer says {name} slot {slot} = "
                f"{item} (got {got}); update SNAP or the fixture")


def committed_rows():
    """(action, truth) per label for the committed timeline's E-block rows."""
    out = {}
    with open(TIMELINE) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line.strip() or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 4:
                continue
            kv = dict(p.split("=", 1) for p in parts[3].split(";") if "=" in p)
            lab = kv.get("label", "")
            if lab.startswith(("E_held", "E_equip")):
                out.setdefault(lab, (parts[2].strip(), kv))
    return out


class GeneratorFromSnapshotTest(unittest.TestCase):
    """AC1: build_timeline() derives every held= claim from the snapshot."""

    def setUp(self):
        check_fixture()

    def test_heldA_claims_come_from_the_snapshot(self):
        rows = gen.build_timeline(FIXTURE)
        got = {}
        for _, player, _, truth in rows:
            m = re.search(r"label=(E_heldA_\w+);", truth)
            if m:
                got[m.group(1)] = re.search(r"held=([^;]*)", truth).group(1)
        self.assertEqual(got.get("E_heldA_p2_chase"), "13=14337,14=20656",
                         "Testcaster's held-A must claim what the snapshot "
                         "says (14337/20656), not the old hand-typed 9998/9979")
        self.assertEqual(got.get("E_heldA_p2_tether"), "13=14337,14=20656")
        self.assertEqual(got.get("E_heldA_p3_chase"), "13=9997,14=9979",
                         "Testbuddy's held-A matches the snapshot -- the "
                         "check discriminates, which is why it caught p2")
        self.assertEqual(got.get("E_heldA_p3_tether"), "13=9997,14=9979")

    def test_no_hand_typed_item_id_remains_in_a_held_claim(self):
        rows = gen.build_timeline(FIXTURE)
        for _, player, _, truth in rows:
            m = re.search(r"held=([^;]*)", truth)
            if not m:
                continue
            claim = m.group(1)
            for slot, item in re.findall(r"(\d{1,2})=(\d{3,6})", claim):
                self.assertIn(int(slot), (13, 14),
                              f"a held= claim names a slot outside 13/14: "
                              f"{claim!r}")
                expect = SNAP[{"p2": "Testcaster", "p3": "Testbuddy"}[player]][int(slot)]
                self.assertEqual(int(item), expect,
                                 f"{player} {slot}: claim {item} != snapshot "
                                 f"{expect} -- a hand-typed value survived "
                                 f"(B793)")

    def test_equip_step_does_not_claim_the_after_state(self):
        rows = gen.build_timeline(FIXTURE)
        for _, player, action, truth in rows:
            if not action.startswith("/equip"):
                continue
            self.assertNotIn("held=", truth,
                             f"the /equip step claims the after-state "
                             f"({truth!r}); no repo file establishes the "
                             f"source slot's eligibility for Primary (13) "
                             f"(B793, AC3)")

    def test_heldB_frames_are_parked_with_why(self):
        rows = gen.build_timeline(FIXTURE)
        parked = 0
        for _, player, _, truth in rows:
            m = re.search(r"label=(E_heldB_\w+);", truth)
            if not m:
                continue
            parked += 1
            self.assertIn("expect=unlabelled", truth,
                          f"{m.group(1)} carries a grading label although the "
                          f"after-state cannot be established offline: {truth!r}")
            why = re.search(r"why=([^;]*)", truth)
            self.assertIsNotNone(why, f"{m.group(1)} is parked without a why=")
            self.assertIn("Primary (13)", why.group(1),
                          f"the why= must name the missing input "
                          f"(Primary(13) eligibility): {why.group(1)!r}")
        self.assertEqual(parked, 4, "the four E_heldB_* frames must be parked")

    def test_missing_snapshot_is_a_loud_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(gen.InventoryError):
                gen.read_inventory(tmp)
            # ...and the CLI fails with a non-zero exit, writing nothing.
            r = subprocess.run([sys.executable, GENERATOR,
                                os.path.join(tmp, "out.tsv"),
                                "--db-before", tmp],
                               capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0,
                                "a missing snapshot must fail loudly, never "
                                "fall back to constants (B793)")
            self.assertIn("B793", r.stderr)
            self.assertFalse(os.path.exists(os.path.join(tmp, "out.tsv")),
                             "nothing may be written when the snapshot is "
                             "missing")

    def test_slot_with_no_item_is_a_loud_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            # A snapshot where Testcaster's slot 13 is empty: the held-A
            # claim cannot be derived, so it must raise, not guess.
            with open(os.path.join(tmp, "inventory.tsv"), "w") as f:
                f.write("67\t14\t20656\t0\n68\t13\t9997\t1\n68\t14\t9979\t1\n")
            with open(os.path.join(tmp, "snapshot.tsv"), "w") as f:
                f.write("67\tTestcaster\t34\t0\t0\t0\t0\t1\t1\n"
                        "68\tTestbuddy\t36\t0\t0\t0\t0\t1\t0\n")
            with self.assertRaises(gen.InventoryError):
                gen.build_timeline(tmp)


class CommittedTimelineTest(unittest.TestCase):
    """AC2 (RED on the pre-B793 tree): the committed timeline.tsv carries the
    snapshot-derived values.  On today's tree this fails -- the file claims
    9998 where the snapshot says 14337 -- and passes after it is regenerated
    with the fixed generator (AC4)."""

    def setUp(self):
        check_fixture()

    def test_heldA_rows_claim_the_snapshot(self):
        rows = committed_rows()
        for label in ("E_heldA_p2_chase", "E_heldA_p2_tether"):
            action, kv = rows[label]
            self.assertEqual(kv.get("held"), "13=14337,14=20656",
                             f"{label} claims {kv.get('held')!r} but the "
                             f"snapshot says 14337/20656 -- the hand-typed "
                             f"value (9998) survived (B793)")
        for label in ("E_heldA_p3_chase", "E_heldA_p3_tether"):
            self.assertEqual(rows[label][1].get("held"), "13=9997,14=9979",
                             f"{label}: Testbuddy's rows must stay at the "
                             f"snapshot's 9997/9979")

    def test_equip_rows_carry_no_held_claim(self):
        for label in ("E_equip_p2_23to13", "E_equip_p3_23to13"):
            action, kv = committed_rows()[label]
            self.assertNotIn("held", kv,
                             f"{label} still claims the after-state "
                             f"{kv.get('held')!r} (13=9990) although the "
                             f"client refused the move (B793)")

    def test_heldB_rows_are_parked(self):
        for label in ("E_heldB_p2_chase", "E_heldB_p3_chase",
                      "E_heldB_p2_tether", "E_heldB_p3_tether"):
            action, kv = committed_rows()[label]
            self.assertNotIn("held", kv,
                             f"{label} still claims held={kv.get('held')!r}; "
                             f"the after-state was refused by the client "
                             f"(B793)")
            self.assertEqual(kv.get("expect"), "unlabelled",
                             f"{label} carries a grading label for an "
                             f"unscorable after-state (B793)")
            self.assertTrue(kv.get("why"),
                            f"{label} is parked without a why= naming the "
                            f"missing input (B793)")


if __name__ == "__main__":
    unittest.main(verbosity=2)
