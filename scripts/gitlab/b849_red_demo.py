#!/usr/bin/env python3
"""B849 RED demonstration against the PRE-CHANGE script (origin/fable).

Two defects, shown against the real pre-change source:

  (1) dedup: the old script listed issues with state=opened ONLY, so a
      CLOSED issue titled `B817: ...` never entered the skip map and B817
      was planned for creation again. The new script reads ALL states.
  (2) header: the old create path had no notion of an entry whose own
      header says the work is fixed; the new plan_actions() reports it as
      an ANOMALY and does not create it.

FakeHttp stands in for GitLab so this needs no network or token.
"""
import importlib.util
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "board_to_issues.py")
REPO = os.path.dirname(os.path.dirname(HERE))
old_src = subprocess.run(
    ["git", "show", "origin/fable:scripts/gitlab/board_to_issues.py"],
    capture_output=True, text=True, cwd=REPO).stdout
open(os.path.join(HERE, "_b849_old.py"), "w").write(old_src)
_spec = importlib.util.spec_from_file_location(
    "b849_old", os.path.join(HERE, "_b849_old.py"))
assert _spec and _spec.loader
old = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(old)

_nspec = importlib.util.spec_from_file_location("b849_new", SRC)
assert _nspec and _nspec.loader
new = importlib.util.module_from_spec(_nspec)
_nspec.loader.exec_module(new)


class FakeHttp:
    """GitLab: exactly one issue, B817, and it is CLOSED."""
    def __call__(self, method, url, token, payload=None):
        if "state=opened" in url:
            return 200, json.dumps([])  # closed B817 absent from this view
        if "state=all" in url:
            return 200, json.dumps([
                {"iid": 10, "title": "B817: The client held two copies.",
                 "state": "closed", "labels": ["board-migrated"]}])
        return 404, "{}"


# B817 as it sat on the board: unstruck, pointer written, header says fixed.
B817 = {"num": "817", "cls": "A", "line": 0,
        "body": "- **B817** (A, *measured*, **fixed in T496**) "
                "**The client held two complete copies.** \u2192 issue #10"}

# ---- (1) dedup -----------------------------------------------------------
have_old = old.open_issue_titles(FakeHttp(), "x")          # state=opened
planned_old = [e["num"] for e in [B817] if e["num"] not in have_old]
have_new = new.issue_title_map(FakeHttp(), "x")            # state=all
planned_new, skips_new, _ = new.plan_actions([B817], have_new, {"A"})

print("DEFECT 1 (dedup against open issues only)")
print(f"  OLD open map : {have_old}  -> B817 planned for creation: "
      f"{'817' in planned_old}")
print(f"  NEW all map  : {have_new}  -> planned: {planned_new}, "
      f"skips: {skips_new}")

# ---- (2) header says fixed ----------------------------------------------
_, _, anom_new = new.plan_actions(
    [dict(B817, body=B817["body"].replace(" \u2192 issue #10", ""))],
    {}, {"A"})
print("DEFECT 3 (unstruck entry whose header says fixed)")
print(f"  OLD create path: no such check -- B817 would be created")
print(f"  NEW anomalies  : {anom_new}")

ok = ("817" in planned_old
      and planned_new == []
      and any("B817" in a for a in anom_new))
print()
print("RED (old code re-creates the closed issue) then "
      f"GREEN (new code does not): {'PASS' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)
