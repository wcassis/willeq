"""B849 regression net for scripts/gitlab/board_to_issues.py.

The migration script used to:
  1. dedup against OPEN issues only, so a CLOSED issue titled `B817: ...`
     would not stop B817 being created again, and it never read the
     `→ issue #N` pointer the entry carries -- so "re-running it never
     duplicates" (docs/issues.md) was false;
  2. treat an unstruck bullet as live even when its own header says the
     work is fixed (`fixed in T496`, `FIXED`, `CLOSED by T500`), migrating
     B817/B819/B830/B805 and later B837 as open work;
  3. leave every migrated issue without a `status::` label, so the
     shepherd's label sync had nothing to move.

No network: `plan_actions` takes the issue listing it would have fetched
(open_issue_titles -> {B-number: (title, state)}), and
`label_missing_status_plan` takes the open-issue listing, so both are
driven here with injected fixtures the way the real http() would have
shaped them.

Registered as BoardToIssuesHygiene in tests/cmake/hygiene.cmake (label
"hygiene"): pure parser + planning logic, no artefact, no server.
"""
import importlib.util
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "board_to_issues", os.path.join(HERE, "board_to_issues.py"))
assert _spec is not None and _spec.loader is not None
bti = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bti)


def entry(num, body):
    return {"num": num, "cls": "A", "line": 0, "body": body}


class PlanActionsTest(unittest.TestCase):
    """Defect 1 (B849): a closed issue must not stop the entry being seen
    as already migrated, and the board's own pointer must count."""

    def test_closed_issue_titled_like_the_entry_still_skips_creation(self):
        # The RED case from the card: `open_issue_titles()` returned only
        # state=opened, so this map (a CLOSED issue titled B817, iid 10) left
        # B817 planned for creation. The skip must cite the REAL iid (#10),
        # not re-derive it as the B-number.
        entries = [entry("817", "The client held two copies of a SoundFont.")]
        issues = {"817": ("B817: The client held two copies of a SoundFont.",
                          "closed", 10)}
        planned, skips, _ = bti.plan_actions(entries, issues, {"A"})
        self.assertEqual(planned, [],
                         "B817 must not be planned while ANY issue titled "
                         "B817: exists -- closed ones included")
        self.assertIn("817", skips)
        self.assertEqual(skips["817"], "issue #10 (closed)")

    def test_pointer_to_closed_issue_skips_creation(self):
        # The entry carries the pointer the migration wrote; the issue is
        # closed. The pointer is the migration's own record and must be
        # honoured regardless of the issue listing.
        entries = [entry("817", "The client held two copies. → issue #10")]
        planned, skips, _ = bti.plan_actions(entries, {}, {"A"})
        self.assertEqual(planned, [])
        self.assertIn("817", skips)

    def test_pointer_to_open_issue_skips_even_if_listing_drops_it(self):
        entries = [entry("017", "A guild invite prints the wrong text. → issue #5")]
        planned, skips, _ = bti.plan_actions(entries, {}, {"A", "C"})
        self.assertEqual(planned, [])
        self.assertIn("017", skips)

    def test_pointer_to_closed_issue_still_skips(self):
        # A closed issue behind an unstruck entry is the NORMAL post-close
        # state (docs/issues.md: entries are not struck when their issue
        # closes), so the entry is skipped -- never re-created -- with the
        # pointer named as the record.
        entries = [entry("817", "The client held two copies. → issue #10")]
        issues = {"817": ("B817: The client held two copies.", "closed", 10)}
        planned, skips, anomalies = bti.plan_actions(entries, issues, {"A"})
        self.assertEqual(planned, [])
        self.assertIn("817", skips)
        self.assertEqual(skips["817"], "issue #10 (closed)")
        self.assertEqual(anomalies, [])

    def test_entry_without_pointer_or_issue_is_planned(self):
        entries = [entry("837", "The merged-textures path retained every model.")]
        planned, skips, anomalies = bti.plan_actions(entries, {}, {"A"})
        self.assertEqual([e["num"] for e in planned], ["837"])
        self.assertEqual(skips, {})
        self.assertEqual(anomalies, [])


class FixedHeaderTest(unittest.TestCase):
    """Defect 3 (B849): an unstruck entry whose own header says the work is
    done is not live work. The check reaches no further than the header
    parenthetical and the first sentence -- no guessing."""

    def test_fixed_in_parenthetical_is_not_planned(self):
        entries = [
            entry("817", "**B817** (A, *measured*, **fixed in T496**) "
                         "**The client held two complete copies.**"),
        ]
        planned, skips, anomalies = bti.plan_actions(entries, {}, {"A"})
        self.assertEqual(planned, [])
        self.assertTrue(any("B817" in a for a in anomalies),
                        "fixed-in-header must be reported, not silently "
                        "dropped: %r" % anomalies)

    def test_fixed_in_parenthetical_second_form(self):
        entries = [
            entry("830", "**B830** (A, *measured*, **fixed in T496 Leg D**) "
                         "**The client read 125 MB of sound archives.**"),
        ]
        planned, _, anomalies = bti.plan_actions(entries, {}, {"A"})
        self.assertEqual(planned, [])
        self.assertTrue(any("B830" in a for a in anomalies))

    def test_fixed_first_sentence_is_anomaly(self):
        # B805's real shape: parenthetical carries only the class, and the
        # entry's first sentence (its own summary) says FIXED.
        entries = [
            entry("805", "**B805** (A) **`held_item_candidate_search` compared "
                         "its 24 candidates at DIFFERENT poses -- FIXED by "
                         "pausing the animator for the sweep.** `ISceneManager"
                         "::drawAll()` calls `OnAnimate()` with the device timer."),
        ]
        planned, _, anomalies = bti.plan_actions(entries, {}, {"A"})
        self.assertEqual(planned, [])
        self.assertTrue(any("B805" in a for a in anomalies))

    def test_closed_by_unit_first_sentence_is_anomaly(self):
        # B837's real shape (added by T500 after the migration): the first
        # sentence names the closing unit.
        entries = [
            entry("837", "**B837** (A) **CLOSED by T500.** "
                         "`getMergedTextures()` retained every CharacterModel."),
        ]
        planned, _, anomalies = bti.plan_actions(entries, {}, {"A"})
        self.assertEqual(planned, [])
        self.assertTrue(any("B837" in a for a in anomalies))

    def test_fixed_later_in_body_is_NOT_an_anomaly(self):
        # The check does not reach past the first sentence: B819 says
        # "Fixed by asserting what the ledger promises" mid-first-sentence,
        # but its header parenthetical reads `fixed in T496 Leg D` -- both
        # caught. A live entry that merely mentions "fixed" in a LATER
        # sentence stays planned.
        entries = [
            entry("446", "**B446** (A) The ledger undercounts a real class. "
                         "T490 noted the adjacent case was already fixed by "
                         "T485; this one is not."),
        ]
        planned, _, anomalies = bti.plan_actions(entries, {}, {"A"})
        self.assertEqual([e["num"] for e in planned], ["446"])
        self.assertEqual(anomalies, [])

    def test_struck_entry_is_still_skipped_by_the_parser(self):
        text = "- ~~**B806** (B) some detail atlas underflow.~~\n"
        entries, _ = bti.parse_live_entries(text)
        self.assertEqual(entries, [])


class IssueTitleMapTest(unittest.TestCase):
    """Defect 1 (B849) pinned at the HTTP boundary. The card's review
    finding: no test ever injected a fake http_ into issue_title_map(), so
    the all-states read (state="all") was unpinned -- reverting it to
    state="opened" kept every plan_actions() fixture test green. This fake
    GitLab answers the way the real API does: state=opened returns only
    open issues, so a CLOSED issue titled B817 (iid 10) reaches the map
    ONLY through state="all"."""

    def test_issue_title_map_reads_closed_issues(self):
        import json
        import urllib.parse

        closed_issue = {
            "title": "B817: The client held two copies of a SoundFont.",
            "state": "closed",
            "iid": 10,
        }

        def fake_http(method, url, token, payload=None):
            self.assertEqual(method, "GET")
            state = urllib.parse.parse_qs(
                urllib.parse.urlparse(url).query).get("state", [""])[0]
            if state == "opened":
                return 200, "[]"
            if state in ("all", "closed"):
                return 200, json.dumps([closed_issue])
            raise AssertionError(f"unexpected state={state!r} in {url}")

        m = bti.issue_title_map(fake_http, "token")
        self.assertIn("817", m,
                      "a CLOSED issue must enter the title map -- "
                      "reverting state=\"all\" to \"opened\" goes RED here")
        self.assertEqual(m["817"],
                         ("B817: The client held two copies of a SoundFont.",
                          "closed", 10))


class ParserBodyTest(unittest.TestCase):
    """B849 (defect found during this unit): an entry's body must stop at its
    OWN last line, not swallow the next bullet's. The pre-fix parser joined
    `lines[i : j + 1]` and `j` was the terminating line (the next bullet) --
    so two live entries separated by no blank line (the board's B792/B793,
    B793's pointer `→ issue #75`) cross-attributed each other's pointers, and
    every issue B848 created next to a blank-line-free neighbour carried that
    neighbour's first line in its description (15 issues, repaired 2026-09-18).
    """

    def test_contiguous_entries_do_not_swallow_each_other(self):
        text = (
            "- **B900** (A) first entry needs an issue.\n"
            "  its continuation line.\n"
            "- **B901** (A) second entry, already migrated. → issue #5\n"
            "  its continuation.\n"
        )
        entries, _ = bti.parse_live_entries(text)
        self.assertEqual([e["num"] for e in entries], ["900", "901"])
        self.assertIsNone(bti.POINTER.search(entries[0]["body"]),
                          "B900 must not inherit B901's pointer")
        self.assertEqual(bti.POINTER.search(entries[1]["body"]).group(1), "5")
        # the bodies are exactly their own lines -- nothing borrowed
        self.assertFalse(entries[0]["body"].lstrip().startswith(
            "- **B901**"))
        self.assertEqual(entries[0]["body"].rstrip().split("\n")[-1],
                         "  its continuation line.")
        self.assertEqual(entries[1]["body"].rstrip().split("\n")[-1],
                         "  its continuation.")

    def test_last_entry_still_ends_at_eof(self):
        text = ("- **B900** (A) only entry.\n"
                "  its continuation line.\n")
        entries, _ = bti.parse_live_entries(text)
        self.assertEqual(len(entries), 1)
        self.assertTrue(entries[0]["body"].rstrip().endswith(
            "its continuation line."))


class LabelMissingStatusPlanTest(unittest.TestCase):
    """Defect 2 (B849): the 86 migrated issues carried no `status::` label,
    so the shepherd's label sync had nothing to move. The plan must find
    exactly the open board-migrated issues without one."""

    def test_plans_only_open_board_migrated_issues_without_status(self):
        issues = [
            {"iid": 5, "state": "opened",
             "labels": ["class::C", "board-migrated", "lane::offline"]},
            {"iid": 6, "state": "opened",
             "labels": ["class::C", "board-migrated", "lane::vm",
                        "status::ready"]},
            {"iid": 7, "state": "closed",
             "labels": ["class::A", "board-migrated"]},
            {"iid": 8, "state": "opened",
             "labels": ["class::A"]},  # not board-migrated
        ]
        plan = bti.label_missing_status_plan(issues)
        self.assertEqual(plan, [5],
                         "only open board-migrated issues without a "
                         f"status:: label, got {plan}")

    def test_empty_when_everyone_has_a_status(self):
        issues = [
            {"iid": 1, "state": "opened",
             "labels": ["board-migrated", "status::triage"]},
        ]
        self.assertEqual(bti.label_missing_status_plan(issues), [])


if __name__ == "__main__":
    unittest.main()
