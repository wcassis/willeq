#!/usr/bin/env python3
"""B811: the held-item before/after check for the llmbench timelines.

Defect (measured T494): in scripts/llmbench/timeline_legH.tsv the p3 rows
record `E_heldA_p3_*` with the character ALREADY holding 9997 at slot 13
(lines 24 and 27), then its `/equip 23 13` step (line 31, label H0_equip_p3)
claims `held=13=9997`, and the `E_heldB_p3_*` steps (lines 32, 35) claim
`13=9997 Dagger* (IT5) moved from slot 23`.  Nothing in the pipeline asserted
that the BEFORE state and the AFTER state differ, so a step that moved nothing
-- the character was holding the destination item all along, so `/equip` had
nothing to move -- scores as a success end to end: compile_timeline.py
compiles it, record.sh records it, and verify_labels.py's db_inventory check
compares the held-B claim against shared/db_before/inventory.tsv and finds 9997
at 13 -- the very state the claim says the step JUST created.

The shape of the fix is what the live `equip_observed` scenario does
(src/client/scenario/scenario_equip_observed.cpp:100-109): if the baseline
already holds the claimed item, the run "could not tell an equip from a
character that was wearing it all along" and must fail, not pass.  Here the
baseline is the character's last recorded `held=` BEFORE the equip step; if
the destination slot already carries the equip's target item, the step moved
nothing and every held-B step claiming the move is INVALID.

Enforcement:
  compile_timeline.py  -- rejects the timeline (non-zero, before a single
                          frame is recorded) if any step is INVALID;
  verify_labels.py     -- marks the db_inventory check [FAIL] for already-
                          recorded rows, so they can never carry a grading
                          label ("Rows that FAIL must not carry a grading
                          label: the caller parks them expect=unlabelled").
  this script          -- standalone scan, exit 1 if any committed timeline
                          carries an INVALID step.

    timeline_held_check.py <timeline>.tsv ...
"""
import os
import re
import sys

# Every `slot=item` pair in a held= value, e.g.
# `13=9997 Dagger* (IT5),14=9979 lantern` -> {13: 9997, 14: 9979}.
HELD = re.compile(r"(?:^|,)(\d{1,2})=(\d{3,6})")
# The willeq move: `/equip <db source slot> <db destination slot>`.
EQUIP = re.compile(r"^/equip\s+\d{1,4}\s+(\d{1,4})\s*$")


def parse_timeline(path):
    """(lineno, player, action, truth) per row, the way compile_timeline.py
    reads the file (comment/blank lines skipped, lineno = file line)."""
    rows = []
    with open(path) as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 4:
                continue
            kv = {}
            for p in parts[3].split(";"):
                k, _, v = p.partition("=")
                if k.strip():
                    kv[k.strip()] = v.strip()
            rows.append((n, parts[1].strip(), parts[2].strip(), kv))
    return rows


def file_line_numbers(path):
    """The file line number of each kept (non-comment, non-blank) row, in
    order -- the same skipping rules parse_timeline uses. compile_timeline.py
    parses the file itself and numbers its rows from 0, so it re-maps them
    back to file lines with this."""
    with open(path) as f:
        return [n for n, line in enumerate(f, 1)
                if line.strip() and not line.lstrip().startswith("#")]


def check_held_steps(rows):
    """Rows whose held-B claim the pipeline can no longer score.

    `rows` is (lineno, player, action, truth) per row, in timeline order --
    as parse_timeline() yields them, or as compile_timeline.py's own parse
    re-numbered with file lines via file_line_numbers().

    Returns {player: [(lineno, label, reason)]}; {} is a clean timeline."""
    issues = {}
    baseline = {}   # player -> last recorded held= {slot: item} BEFORE this row
    for lineno, player, action, truth in rows:
        m = EQUIP.match(action)
        if m and player in baseline:
            dest = int(m.group(1))
            item = truth.get("held", "")
            hm = HELD.match(item)
            # the equip row's own claim is `13=<item>`; only that shape names
            # the destination item.  (Legacy rows say `13=9990 (moved
            # in-game...)` instead; those were checked against the DB by the
            # db_inventory net and are not this defect.)
            target = int(hm.group(2)) if hm else None
            if target is not None:
                already = baseline[player].get(dest)
                if already is not None and int(already) == target:
                    issues.setdefault(player, []).append(
                        (lineno, truth.get("label", "?"),
                         f"slot {dest} already held item {target} at this point "
                         f"(last held= {', '.join(f'{s}={i}' for s, i in sorted(baseline[player].items()))}), "
                         f"so `{action}` moved nothing and every held-B step "
                         f"claiming the move scores a no-op (B811)"))
        # This row's own held= (a step's claim of what it now holds) enters the
        # baseline only for the steps AFTER it: a /equip row's held= is its
        # after-state and must not count as its own before-state.
        if "held" in truth:
            baseline[player] = {int(s): int(i) for s, i in HELD.findall(truth["held"])}
    return issues


def main():
    ap = sys.argv[1:]
    if not ap or ap[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    rc = 0
    for path in ap:
        issues = check_held_steps(parse_timeline(path))
        for player in sorted(issues):
            for lineno, label, reason in issues[player]:
                rc = 1
                print(f"INVALID {os.path.basename(path)}:{lineno} {player} {label}: {reason}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
