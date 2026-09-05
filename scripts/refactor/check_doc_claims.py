#!/usr/bin/env python3
"""Check that the live orientation docs do not name retired symbols (T371).

`DocCitations` (scripts/refactor/check_doc_citations.py) proves that every
file PATH a live doc cites still resolves. It says nothing about the CLAIMS
those docs make. T371 swept T352-T370's nineteen merged units against the
tree and found sentences naming commands, methods and members that had been
renamed or deleted out from under the docs.

This is the regression net for the deletion half of that: a small table of
(retired phrase, why, unit) pairs. If a retired name reappears anywhere in
the live-doc set, ctest fails and names the file, line and replacement.

The set scanned is the ORIENTATION docs -- the ones a new agent reads to
learn what the tree IS: the root CLAUDE.md, every */CLAUDE.md, README.md and
docs/*.md (not docs/reference/) -- plus refactor/BOARD.md, which is the
FIRST thing a new agent reads.

T371 left the board out because running the table over it produced 39 hits,
every one legitimate: a struck entry names the thing it retired, on purpose.
T373 (board B131) replaced that exemption with a RULE. The board is scanned
one BULLET at a time, and only LIVE bullets count -- a top-level bullet whose
text begins "~~" is a struck ledger entry and is skipped along with every
continuation line under it, up to the next top-level bullet or heading. What
is left is exactly the text that describes live code, which is what the table
is for. The batch close-outs (refactor/K_*.md and the other Batch ledgers),
the T/J/P unit plans and refactor/completed/ stay out entirely: they are
historical records end to end, with no live half to separate.

A doc may still discuss a retired name deliberately (a tombstone: "renamed
from /newskills", "setOnSpellLand is gone"). Such a line must carry the
marker RETIRED-OK on it, which exempts that one line. Keep those rare: the
point of the table is that the name stops being used as if it were live.

Usage:
    check_doc_claims.py [--repo REPO_ROOT]

Exit 0 if no live doc names a retired symbol outside an exempted line
(always printing what was checked -- never a silent pass, the T37 rule);
exit 1 on any hit.
"""
import argparse
import os
import re
import sys

EXEMPT_MARKER = "RETIRED-OK"

# (retired phrase, what is true now, the unit that measured it).
# Every row was confirmed absent from the live docs by T371 before landing.
RETIRED = [
    ("/newskills",
     "renamed to /skills, no alias (registry 3)", "T363 (B058)"),
    ("/newinv",
     "renamed to /inventory, no alias (registry 3)", "T365 (B090)"),
    ("setOnSpellLand",
     "the typedef, setter, member and invocation were all deleted",
     "T357 (B061)"),
    ("SpellLandCallback",
     "deleted with setOnSpellLand", "T357 (B061)"),
    ("EntitiesComponent::FindEntityByName",
     "deleted; EntityManager::findEntityByName (ranked) is the only "
     "name resolver left", "T362 (B070)"),
    ("drawZoneLineOverlay",
     "deleted; renderZoneLineOverlays() draws only the box labels",
     "T354 (B042)"),
    ("addRegionToCollision",
     "deleted; collision is built once at load step 9", "T354 (B047)"),
    ("addDoorToCollision",
     "deleted; collision is built once at load step 9", "T354 (B047)"),
    ("addObjectToCollision",
     "deleted; collision is built once at load step 9", "T354 (B047)"),
    ("findBspRegionForPoint",
     "deleted, zero callers", "T354 (B047)"),
    ("clearCollisionDebugLines",
     "deleted, zero callers", "T354 (B047)"),
    ("checkResourceRequirement",
     "deleted with SkillData's cost block -- resource costs are a spell "
     "property, not a skill one, in this era", "T353"),
    ("SkillData::endurance_cost",
     "deleted; only SpellData::endurance_cost exists", "T353"),
    ("SkillData::mana_cost",
     "deleted; only SpellData::mana_cost exists", "T353"),
    ("EverQuest::m_test_bypass_send_guard",
     "moved to eqt::PacketSender; a test arms it with "
     "eq.Net().Test_SetBypassSendGuardForSeam(true)", "T366 (B038)"),
    ("kWilleqDbExpectedVersion 5",
     "the expected willeq.db version is 7 (spells.target_anim, "
     "spells.travel_type)", "T384 (B163)"),
    ("kWilleqDbExpectedVersion 6",
     "the expected willeq.db version is 7 -- T384 appended target_anim "
     "(field 121) and travel_type (field 122) to the spells table, 99 "
     "columns -> 101, and every build tree must be rebuilt", "T384 (B163)"),
    ("InputAction::ToggleInventory",
     "deleted -- it had no consumer in InputActionBridge", "T365 (B090)"),
    ("InputAction::ToggleSkills",
     "deleted -- it had no consumer in InputActionBridge", "T365"),
    ("InputAction::ToggleGroup",
     "deleted -- it had no consumer in InputActionBridge", "T365"),
    ("InputAction::TogglePetWindow",
     "deleted -- it had no consumer in InputActionBridge", "T368 (B100)"),
    ("InputAction::ToggleSpellbook",
     "deleted -- it had no consumer in InputActionBridge", "T368 (B100)"),
    ("InputAction::ToggleTrainer",
     "deleted -- it had no consumer in InputActionBridge", "T368 (B100)"),
    ("InputAction::ToggleVendor",
     "deleted -- it had no consumer in InputActionBridge", "T368 (B100)"),

    # --- T388, from T376/T381 -------------------------------------------
    ("m_pending_door_clicks",
     "deleted; DoorStateManager owns the one pending-click set "
     "(pendingClicks_, addPendingClick/isClickPending), and "
     "ZoneProcessMoveDoor must ask BEFORE setDoorState consumes it",
     "T376 (B141)"),
    ("ClearPendingDoorClicks",
     "deleted with m_pending_door_clicks; doors().clear() clears doors_ "
     "and pendingClicks_ together", "T376 (B141)"),
    ("m_doors",
     "there is no such member; doors live in DoorStateManager::doors_, "
     "reached as GameState::doors()", "T376 (B141)"),
    ("setSpellDatabase",
     "deleted with SpellVisualFX::m_spell_db; the renderer is fed "
     "EQ::SpellVisualVocabulary by value over the bridge "
     "(noteSpellVocabulary), because the SpellDatabase LRU is game-thread "
     "owned and unlocked", "T381 (B160)"),
]


PRUNE_DIRS = {"third_party", ".git"}

# The board is scanned by the live-entry rule below, not whole.
BOARD_REL = os.path.join("refactor", "BOARD.md")


def board_live_lines(lines):
    """(lineno, text) for the LIVE parts of refactor/BOARD.md.

    The board is a bullet list of one entry each. A top-level bullet ("- " at
    column 0) opens an entry and owns every following line until the next
    top-level bullet or a markdown heading; an entry whose text starts "~~"
    is struck (closed) and is skipped whole, because struck text names the
    thing it retired on purpose. Prose that belongs to no bullet -- the
    class/rules preamble, section headings, the horizontal rules -- is not an
    entry and is not scanned.

    Returns (kept_lines, live_entry_count).
    """
    kept = []
    live = 0
    in_entry = False
    entry_is_live = False
    for n, line in enumerate(lines, 1):
        if line.startswith("#"):
            in_entry = False
            entry_is_live = False
            continue
        if line.startswith("- "):
            in_entry = True
            body = line[2:].lstrip()
            entry_is_live = not body.startswith("~~")
            if entry_is_live:
                live += 1
        if in_entry and entry_is_live:
            kept.append((n, line))
    return kept, live


def find_orientation_docs(repo_root):
    """Root CLAUDE.md + every */CLAUDE.md + README.md + docs/*.md."""
    found = []
    for dirpath, dirnames, filenames in os.walk(repo_root):
        dirnames[:] = [d for d in dirnames
                       if d not in PRUNE_DIRS and not d.startswith("build")]
        if "CLAUDE.md" in filenames:
            found.append(os.path.join(dirpath, "CLAUDE.md"))
    readme = os.path.join(repo_root, "README.md")
    if os.path.isfile(readme):
        found.append(readme)
    docs_dir = os.path.join(repo_root, "docs")
    if os.path.isdir(docs_dir):
        for name in sorted(os.listdir(docs_dir)):
            full = os.path.join(docs_dir, name)
            if os.path.isfile(full) and name.endswith(".md"):
                found.append(full)
    board = os.path.join(repo_root, BOARD_REL)
    if os.path.isfile(board):
        found.append(board)
    seen = set()
    deduped = []
    for d in sorted(found):
        if d not in seen:
            seen.add(d)
            deduped.append(d)
    return deduped


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    args = ap.parse_args()
    repo = os.path.abspath(args.repo)

    docs = find_orientation_docs(repo)

    # Word-boundary-ish: a slash command must not match a longer word, and a
    # C++ name must not match a longer identifier. Phrases containing "::"
    # or "/" are matched literally with a trailing-identifier guard.
    patterns = [(phrase, truth, unit,
                 re.compile(r"(?<![A-Za-z0-9_])%s(?![A-Za-z0-9_])"
                            % re.escape(phrase)))
                for phrase, truth, unit in RETIRED]

    failures = []
    scanned = 0
    exempted = 0
    board_live = 0
    for doc in docs:
        rel = os.path.relpath(doc, repo)
        try:
            with open(doc, "r", encoding="utf-8", errors="replace") as fh:
                lines = fh.readlines()
        except OSError as exc:
            print("FAIL  %s: unreadable (%s)" % (rel, exc))
            failures.append(rel)
            continue
        scanned += 1
        if rel == BOARD_REL:
            numbered, board_live = board_live_lines(lines)
        else:
            numbered = list(enumerate(lines, 1))
        for n, line in numbered:
            for phrase, truth, unit, rx in patterns:
                if not rx.search(line):
                    continue
                if EXEMPT_MARKER in line:
                    exempted += 1
                    continue
                failures.append((rel, n, phrase, truth, unit))

    print("DocClaims: %d orientation docs scanned (including %s's %d LIVE "
          "entries; struck entries skipped), %d retired names in the table, "
          "%d exempted line hits (%s)"
          % (scanned, BOARD_REL, board_live, len(RETIRED), exempted,
             EXEMPT_MARKER))

    if not failures:
        print("OK: no live doc names a retired symbol.")
        return 0

    for f in failures:
        if isinstance(f, tuple):
            rel, n, phrase, truth, unit = f
            print("FAIL  %s:%d names the retired '%s' -- %s (%s). "
                  "Correct the sentence, or mark the line %s if it is a "
                  "deliberate tombstone."
                  % (rel, n, phrase, truth, unit, EXEMPT_MARKER))
        else:
            print("FAIL  %s" % f)
    print("DocClaims: %d failure(s)." % len(failures))
    return 1


if __name__ == "__main__":
    sys.exit(main())
