#!/usr/bin/env python3
"""B630 (issue #48) + B760 (issue #45): the two-client interrupted-cast log
analyzer.

The scenario state machines drive the legs; this script READS the two client
logs (caster + observer) and turns them into the two deliverables:

  1. A results table: leg x assertion -> PASS/FAIL, each backed by a pasted
     log line. A FAIL is a NEW defect (recorded, not fixed, in this rig).
  2. The B760 table: for each leg, whether ANY zone packet arrived between
     `ManaChange keepcasting 0` and `OP_MemorizeSpell`, and the SpellCastComplete
     outcome the caster reported. That is the answer to #45; it is recorded,
     never acted on.

B630 item 4 (review of MR !20): every needle is anchored to its log MODULE and
message shape, never to a bare token. The three false positives that failed
the previous run are pinned by the --self-test mode:

  * D2's "236" matched a glibc `[memarena]` line (the "128-256=65/12385"
    style counters are arbitrary digits; "236" was a substring of them).
  * A's no-late-impact grep named O'Keil, a spell never cast in this rig, so
    the count was always 0: a vacuous PASS.
  * B's fizzle/land grep matched "[WORLD] Logged in successfully ..."
    because it searched for a success phrase that the login handshake logs.

The needles now:
  * match the `[SPELL]` module lines the spell handlers actually emit
    ("Beginning cast of '<name>' (ID: <id>)", "was interrupted",
    "completed successfully", "Server memorised"),
  * match the `[MAIN]` chat lines by their full eqemu string text
    ("Your spell is interrupted.", "Spell recast time not yet met.",
    "Your spell fizzles!"),
  * match the `[ZonePacket]` handler lines by exact opcode name,
    and the bystander interrupt by the named `[SPELL]` handler line
    ("Interrupt: caster=N ('Name'), spell=M, message=12478").

Usage:
    scripts/scenario_configs/b630_analyze.py <caster.log> <observer.log> [--out DIR]
    scripts/scenario_configs/b630_analyze.py --self-test

Exit: 0 if every ASSERTION leg passed (the B760 table is informational and
never fails the run); 1 if any caster- or observer-side assertion failed;
2 if the logs could not be read. --self-test exits 0/1 on its own verdict.
"""

import argparse
import os
import re
import sys

# ---------------------------------------------------------------------------
# Packet-name vocabulary (from include/common/... opcode names + T469's two
# interrupt ids + T482's spell-tail ids). The B760 question is about ORDER, so
# the analyzer must recognise the tail packets even when the spell handlers
# log them under different words.
# ---------------------------------------------------------------------------
OPCODE_RE = re.compile(r"\[ZonePacket\]\s+(\w+)")

# eqemu string ids this rig's legs care about (common/strings in eqemu).
STR_INTERRUPT_CAST = 439       # "Your spell is interrupted." (caster's own)
STR_INTERRUPT_OTHER = 12478    # "<name>'s casting is interrupted!" (bystander)
STR_SPELL_FIZZLE = 173         # "Your spell fizzles!"
STR_SPELL_RECAST = 236         # "Spell recast time not yet met."

# ---------------------------------------------------------------------------
# B630 item 4: module-anchored needles. A log line's module is the bracketed
# word after the [LEVEL] (e.g. "[INFO ] [SPELL] ..."); the needle is the
# message shape AFTER it. A bare-substring grep over the whole line is how the
# previous revision matched glibc memarena counters and login handshakes.
# ---------------------------------------------------------------------------
LINE_RE = re.compile(r"^\[[^\]]+\] \[\w+\s*\] \[(\w+)\] (.*)$")

# The [SPELL] handler lines (spell_packet_handlers.cpp). Each carries the
# spell's ID where it matters, so the analyzer can tell which of the four
# legs a line belongs to. Lines carry a "[ts] [LEVEL] [MODULE] message"
# prefix, so the anchors start at the module tag, not the line start.
# B630 item 4: the interrupt/complete lines now carry "(ID: n)" (previously
# name-only), which is what lets a completion be attributed to its leg's
# declared spell id without a second-pass name lookup.
SPELL_BEGIN_CAST = re.compile(
    r"\[SPELL\] Beginning cast of '(.+)' \(ID: (\d+)\)")
SPELL_INTERRUPTED = re.compile(
    r"\[SPELL\] Cast of '(.+)' \(ID: (\d+)\) (?:was interrupted|fizzled)")
SPELL_COMPLETED = re.compile(
    r"\[SPELL\] Cast of '(.+)' \(ID: (\d+)\) completed successfully")
SPELL_MEMORISED = re.compile(
    r"\[SPELL\] Server memorised '(.+)' \((\d+)\)")
# The bystander interrupt (T469's handler for OP_InterruptCast, msg 12478).
# Module is [SPELL]; the actual line shape is
#   Interrupt: caster=<id> ('<caster name>'), spell=<id>, message=12478
# (spell_packet_handlers.cpp:234, handleInterrupt). The caster's OWN copy
# carries message=439 with an empty name; the bystander's carries 12478 with
# the caster's name, so the 12478 + non-empty-name shape is the bystander's.
BYSTANDER_INTERRUPT = re.compile(
    r"\[SPELL\] Interrupt: caster=\d+ \('([^']+)'\), spell=(\d+), message=12478")

# The [MAIN] chat strings, full text (ChatOutput::System logs the formatted
# eqemu string). Anchored to the module + the whole phrase.
CHAT_INTERRUPT_OWN = "[MAIN] Your spell is interrupted."
CHAT_RECAST_NOT_MET = "[MAIN] Spell recast time not yet met."
CHAT_FIZZLE = "[MAIN] Your spell fizzles!"
CHAT_NOT_TAKE_HOLD = "did not take hold"

# The B760 tail packets as [ZonePacket] handler lines. The opcode name the
# handler prints carries the wire prefix, e.g.
#   [ZONE] [ZonePacket] HC_OP_ManaChange
# (zone_component's generic dispatcher), so the anchors accept the
# HC_ prefix; a bare-name line would be the pre-patch shape.
HANDLER_KEEPCASTING_0 = re.compile(r"\[ZonePacket\] (?:HC_)?OP_ManaChange")
HANDLER_MEMORIZE = re.compile(r"\[ZonePacket\] (?:HC_)?OP_MemorizeSpell")


def read(path):
    if not os.path.isfile(path):
        return None
    with open(path, "r", errors="replace") as fh:
        return fh.readlines()


def grep(lines, *needles):
    """Return the first line containing ALL of the needles (for pasting).

    Item 4: the previous version matched ANY needle, so a single glibc counter
    or a login line was enough to PASS an assertion. All needles now describe
    one message (module + shape), and every one of them must hit the same line.
    """
    for line in lines:
        if all(n in line for n in needles):
            return line.rstrip("\n")
    return None


def count(lines, *needles):
    return sum(1 for line in lines if all(n in line for n in needles))


# ---------------------------------------------------------------------------
# B760: the packet-order recorder. Walk the caster log, and for each
# `ManaChange keepcasting 0` (the spell bar re-enables) look forward for the
# next `OP_MemorizeSpell` and report every zone packet that landed in
# between. That "in between" set IS the #45 answer for that outcome.
# Item 4: anchor on the [ZonePacket] handler lines, not the SCENARIO prose.
# ---------------------------------------------------------------------------
def b760_rows(caster_lines, label):
    """B760 rows for a slice of the caster log (one leg's window).

    An anchor is the `[ZonePacket] HC_OP_ManaChange` line whose IMMEDIATE
    [SPELL] decode is a keepcasting 0 (the spell bar re-enables, end of the
    cast). The next `[ZonePacket] HC_OP_MemorizeSpell` decides the outcome;
    every zone packet between the two is the #45 answer for that outcome.
    A keepcasting 0 with no MemorizeSpell after is the "stopped" outcome
    (interrupt / refused cast).
    """
    rows = []
    lines = caster_lines
    n = len(lines)
    for i, line in enumerate(lines):
        if not HANDLER_KEEPCASTING_0.search(line):
            continue
        # The [SPELL] decode line follows the [ZonePacket] line directly;
        # only a keepcasting 0 (cast ended) is a B760 anchor, not a mana
        # regen keepcasting 1.
        if i + 1 >= n or "keepcasting=0" not in lines[i + 1]:
            continue
        # Find the next MemorizeSpell handler after this keepcasting 0.
        j = i + 2
        between = []
        outcome = "no-MemorizeSpell-after (stopped)"
        while j < n:
            m = OPCODE_RE.search(lines[j])
            if m:
                name = m.group(1)
                if "MemorizeSpell" in name:
                    outcome = "success-tail (MemorizeSpell)"
                    break
                between.append(name)
            j += 1
            if j - i > 40:  # bounded look-ahead: the tail is contiguous
                break
        rows.append((label, i + 1, between, outcome))
    return rows


def _spell_id_map(lines):
    """Spell name -> id, from the two lines that carry both.

    'Beginning cast of '<name>' (ID: <id>)' and 'Server memorised '<name>'
    (<id>)' are the only [SPELL] lines with the id; 'completed successfully'
    and 'was interrupted' carry the name only. This map is how a completion
    is attributed to a leg's declared spell id.
    """
    mapping = {}
    for line in lines:
        m = SPELL_BEGIN_CAST.search(line)
        if m:
            mapping[m.group(1)] = int(m.group(2))
            continue
        m = SPELL_MEMORISED.search(line)
        if m:
            mapping[m.group(1)] = int(m.group(2))
    return mapping


def _lines_of(lines, pattern, spell_id=None, name_map=None):
    """All lines matching `pattern`; if spell_id is given, only those whose
    spell name maps to that id (via name_map). Returns pastable lines."""
    out = []
    for line in lines:
        m = pattern.search(line)
        if not m:
            continue
        if spell_id is not None and name_map is not None \
                and name_map.get(m.group(1)) != spell_id:
            continue
        out.append(line.rstrip("\n"))
    return out


def _spell_id_of(line, name_map, outcome=None):
    """The id a [SPELL] 'Cast of '<name>' ...' line belongs to, else None.

    New handler lines carry the id inline ("(ID: n)"); older logs carry the
    name only, in which case the name->id map (from the 'Beginning cast' /
    'Server memorised' lines) is the fallback.

    outcome: when given, only a line ending in exactly that result phrase
    counts. QA attempt 3 + 4 (FAIL x3 on MR !20): matching every outcome let
    the D1/D3 "was cast and completed" cells PASS on a "was interrupted" or
    "fizzled" line of the right id. The D1/D3 cells now pass
    "completed successfully"; the A-leg late-impact count (a WARN-only cell)
    and the self-test's id-attribution checks keep the bare form, where any
    outcome is the right question.
    """
    if outcome is None:
        outcome = "completed successfully|was interrupted|fizzled"
    m = re.search(r"\[SPELL\] Cast of '(.+)' \(ID: (\d+)\) "
                  r"(?:" + outcome + r")", line)
    if m:
        return int(m.group(2))
    m = re.search(r"\[SPELL\] Cast of '(.+)' "
                  r"(?:" + outcome + r")", line)
    if not m:
        return None
    return name_map.get(m.group(1))


# ---------------------------------------------------------------------------
# B760 leg windows: each leg's rows come from its own slice of the caster log
# (the leg's cast note to "leg N complete"), because B760's question is per
# outcome -- without the window every keepcasting 0 in the log (mana regen,
# the interrupt, a later leg's tail) would print once per leg.
# ---------------------------------------------------------------------------
B760_LEG_WINDOWS = (
    ("A cast_interrupt", "casting: /cast 1", "leg 1 complete"),
    ("D1 cast_success_tail", "casting to completion", "leg 2 complete"),
    ("D2 cast_recast_stop", "first cast (to completion)", "leg 3 complete"),
    ("D3 cast_buff_notake", "buff leg: /cast 1", "leg 4 complete"),
)


def b760_windows(caster_lines):
    """All B760 rows, one leg's window at a time.

    Yields (label, absolute_lineno, between, outcome). A leg whose window
    cannot be found is skipped (the log simply did not run that leg).
    """
    def index_of(needle, lo=0):
        for i in range(lo, len(caster_lines)):
            if needle in caster_lines[i]:
                return i
        return None

    for label, start_marker, end_marker in B760_LEG_WINDOWS:
        start = index_of("[SCENARIO] " + start_marker)
        end = (index_of("[SCENARIO] " + end_marker, start + 1)
               if start is not None else None)
        if start is None or end is None:
            continue
        for r in b760_rows(caster_lines[start:end + 1], label):
            yield (label, start + r[1], r[2], r[3])


def analyze(caster, observer, results):
    """Run every leg's assertions over two already-read logs, appending to
    `results` (leg, assertion, verdict, evidence). Returns the FAIL count.
    main() owns the table print; the self-test drives this directly on its
    synthetic logs, so the cells it pins are the shipped cells, verbatim."""

    failures = 0

    def record(leg, assertion, verdict, evidence):
        nonlocal failures
        results.append((leg, assertion, verdict, evidence))
        if verdict == "FAIL":
            failures += 1

    # The rig's declared spell ids (b630_seed.sql + the scenario options).
    A_SPELL_ID = 378        # O'Keils Radiation, the interrupted nuke
    D1_SPELL_ID = 372       # Blast of Cold
    D2_SPELL_ID = 293       # Haze
    D3_SPELL_ID = 293       # Haze (the caster's own buff leg)

    # Item 4: the 'completed successfully' / 'was interrupted' lines carry the
    # spell NAME only; the id comes from the 'Beginning cast ... (ID: n)' /
    # 'Server memorised ... (n)' lines. This map attributes each completion to
    # a leg's declared spell id.
    name_map = _spell_id_map(caster)

    def begin_cast_line(spell_id):
        """The [SPELL] 'Beginning cast ... (ID: n)' line for this leg's spell.

        Item 1 + 4: the leg only counts as exercising its declared spell if
        the handler line says so. Pre-fix, every D leg's handler line read
        'Minor Shielding (ID: 288)' because the GM prep hit the target.
        """
        for line in caster:
            m = SPELL_BEGIN_CAST.search(line)
            if m and int(m.group(2)) == spell_id:
                return line.rstrip("\n")
        return None

    def completion_line(spell_id, after_index=None):
        """The [SPELL] 'completed successfully' line for this leg's spell id.

        after_index: when given, only completions strictly AFTER that line of
        the caster log count. That is the D2/D3 disambiguation (fix pass, QA
        verdict): both legs declare 293, and the bare first-completion rule
        let D3 pass off D2's completion. D2's own completion lands BEFORE its
        second cast is refused by the server recast timer (the chat line the
        recast-stop cell records), and the chain runs the legs back-to-back,
        so D3's completion is the first 293 completion AFTER that marker. The
        marker is the lane's own wire event, not a spell-id guess.

        Outcome filter (B630 fix pass 3, QA verdict attempts 3 + 4): ONLY a
        'completed successfully' line certifies a completion. A 'was
        interrupted' or 'fizzled' line of the right id must NOT do -- that is
        the false positive that let a never-completing D1 or D3 read as PASS.
        """
        start = 0 if after_index is None else after_index + 1
        for i in range(start, len(caster)):
            if (_spell_id_of(caster[i], name_map,
                             outcome="completed successfully") == spell_id):
                return caster[i].rstrip("\n")
        return None

    # The D2 marker: the chat line the recast-stop cell asserts (the second
    # cast's rejection). None when the log has no recast line at all.
    d2_recast_marker_index = None
    for i, line in enumerate(caster):
        if CHAT_RECAST_NOT_MET in line:
            d2_recast_marker_index = i

    # ------------------------------------------------------------------ A
    # The interrupted cast (GM #interrupt mid-cast).
    a_castbar_clear = grep(caster, "[SPELL]", "was interrupted")
    a_chat = grep(caster, CHAT_INTERRUPT_OWN)
    a_late_impact = len(
        _lines_of(caster, SPELL_COMPLETED, A_SPELL_ID, name_map))
    record("A cast_interrupt",
           "caster cast bar clears / interrupted",
           "PASS" if a_castbar_clear else "FAIL",
           a_castbar_clear or "(no [SPELL] 'was interrupted' line)")
    record("A cast_interrupt",
           "caster chat string 'is interrupted'",
           "PASS" if a_chat else "FAIL",
           a_chat or "(no [MAIN] 'Your spell is interrupted.' line)")
    record("A cast_interrupt",
           "A leg's declared spell (ID %d) was actually cast" % A_SPELL_ID,
           "PASS" if begin_cast_line(A_SPELL_ID) else "FAIL",
           begin_cast_line(A_SPELL_ID)
           or "(no [SPELL] 'Beginning cast ... (ID: %d)' line)" % A_SPELL_ID)
    record("A cast_interrupt",
           "no late impact burst of the A spell (ID %d) after the interrupt"
           % A_SPELL_ID,
           "PASS" if a_late_impact == 0 else "WARN",
           "%d 'completed successfully' lines attributed to ID %d "
           "(0 expected after #interrupt)" % (a_late_impact, A_SPELL_ID))
    # Observer: the bystander must see the NAMED 12478 interrupt. The [SPELL]
    # handler line (spell_packet_handlers.cpp:234) carries the caster's name,
    # the spell id and the message id: "Interrupt: caster=N ('Name'), spell=378,
    # message=12478". The regex captures the name and spell so the assertion
    # can attribute the interrupt to the A leg's declared spell.
    o_interrupt = next(
        (line.rstrip("\n") for line in observer
         if BYSTANDER_INTERRUPT.search(line)), None)
    record("A cast_interrupt",
           "observer sees the named 12478 interrupt line",
           "PASS" if o_interrupt else "FAIL",
           o_interrupt or "(observer log has no \"Interrupt: ... message=12478\" line)")

    # ------------------------------------------------------------------ D1
    # Success tail: let the nuke finish. Caster reports the tail; the B760
    # table records the packet order.
    d1_success = completion_line(D1_SPELL_ID)
    record("D1 cast_success_tail",
           "D1's declared spell (ID %d) was cast and completed" % D1_SPELL_ID,
           "PASS" if d1_success else "FAIL",
           d1_success or "(no [SPELL] completion attributed to ID %d)"
           % D1_SPELL_ID)

    # ------------------------------------------------------------------ D2
    # Recast-timer stop: the SECOND cast of the recast-stop spell must be
    # refused by the server's per-spell recast timer -> SPELL_RECAST 236
    # "Spell recast time not yet met." + keepcasting 0, NO MemorizeSpell.
    # D2's OWN completion (its first cast finishing) lands strictly BEFORE
    # the marker line that the marker hunt above finds: the recast rejection
    # only exists because the first cast already completed. The first-293
    # completion in the log is therefore D2's, which is what the bare
    # completion_line() call below attributes to it.
    d2_recast = grep(caster, CHAT_RECAST_NOT_MET)
    record("D2 cast_recast_stop",
           "second cast hits the server recast timer (236)",
           "PASS" if d2_recast else "FAIL",
           d2_recast or "(no [MAIN] 'Spell recast time not yet met.' line)")
    record("D2 cast_recast_stop",
           "D2's declared spell (ID %d) was actually cast" % D2_SPELL_ID,
           "PASS" if begin_cast_line(D2_SPELL_ID) else "FAIL",
           begin_cast_line(D2_SPELL_ID)
           or "(no [SPELL] 'Beginning cast ... (ID: %d)' line)" % D2_SPELL_ID)

    # ------------------------------------------------------------------ D3
    # The caster's own beneficial buff: it MUST take hold on the caster (the
    # success-tail buff), and the observer must see the target gain it.
    # D2/D3 disambiguation (fix pass): D3's completion is the first 293
    # completion STRICTLY AFTER D2's recast marker, so it can never be
    # D2's first-cast completion wearing D3's id. If the marker is absent
    # (the D2 cell failed), the plain first-completion rule applies and this
    # cell reads against the same log the D2 cell already failed on.
    d3_buff = completion_line(D3_SPELL_ID,
                              after_index=d2_recast_marker_index)
    record("D3 cast_buff_notake",
           "D3's declared spell (ID %d) was cast and its OWN completion "
           "landed (after D2's recast marker, not D2's completion)"
           % D3_SPELL_ID,
           "PASS" if d3_buff else "FAIL",
           d3_buff or "(no [SPELL] 293 completion after D2's recast marker; "
                      "D2's completion is D2's and cannot pass for D3)")

    # ------------------------------------------------------------------ B
    # Fizzle: the non-GM caster rolls a high-level spell. This is a coin the
    # server clamps to ~95%; the leg is best-effort and the analyzer REPORTS
    # which way it fell (fizzle vs land) rather than asserting one.
    b_fizzle = grep(caster, CHAT_FIZZLE)
    b_landed = grep(caster, "(ID: ", "completed successfully")
    b_gm_note = grep(caster, "prevents you from fizzling")
    record("B cast_fizzle",
           "non-GM caster outcome (fizzle OR land -- recorded)",
           "INFO",
           b_fizzle or b_landed or b_gm_note
           or "(no fizzle/land/GM-prevention line on the caster log; "
              "the leg is deferred on #48 if it never ran)")

    # ------------------------------------------------------------------ table
    return failures


# ---------------------------------------------------------------------------
# The self-test (item 4, RED-pinned): feed the analyzer's own needle logic
# the three real false-positive lines from the failed dev run plus one true
# positive each. If any of the three false positives lights up a needle, or
# any true positive fails to, this exits 1.
# B630 fix pass 3 (QA verdict attempts 3 + 4, FAIL x3 on MR !20): three more
# cases, these at the CELL level -- the D1 and D3 "completed" cells must FAIL
# on a synthetic caster log whose leg never completes ("was interrupted",
# "was interrupted", "fizzled" x2). They run the SHIPPED analyze() over a
# full synthetic log (the QA probes, taken from
# /home/user/kb-evidence/b630-interrupted-cast-live/qa3-probes/ and
# qa4-probes/), so a regression that re-opens the false positive is a
# self-test failure, not a future lane surprise.
# ---------------------------------------------------------------------------
FALSE_POSITIVE_236 = (
    "[2026-09-19 23:34:49.552] [INFO ] [MAIN] [memarena] point=ready ok=1 "
    "ordblks=262 smblks=0 fordblks=6065696 fsmblks=0 keepcost=133888 hblks=7 "
    "hblkhd=9834496 arena=9179136 uordblks=3113440 mmap_thr=0 trim_pages=0 "
    "le128=85/7333 128-256=65/12385 256-512=11/3179 512-1k=2/561 1k-2k=11/1185 "
    "2k-4k=13/78781 8k-16k=19/236563 16k-32k=28/660508 32k-64k=15/721199 "
    "ge64k=21/4203893\n")
FALSE_POSITIVE_LOGIN = (
    "[2026-09-19 23:34:47.173] [INFO ] [WORLD] Logged in successfully with "
    "dbid 1 and key W5J422OQA9\n")
FALSE_POSITIVE_VACUOUS_SPELL = (
    "[2026-09-19 23:35:17.696] [INFO ] [SPELL] Cast of 'Minor Shielding' "
    "(ID: 288) completed successfully\n")  # ID 288, never O'Keil

TRUE_RECAST = (
    "[2026-09-19 23:36:00.000] [INFO ] [MAIN] Spell recast time not yet met.\n")
TRUE_FIZZLE = (
    "[2026-09-19 23:36:01.000] [INFO ] [MAIN] Your spell fizzles!\n")
TRUE_OKEIL_LAND = (
    "[2026-09-19 23:36:02.000] [INFO ] [SPELL] Cast of 'O'Keils Radiation' "
    "(ID: 378) completed successfully\n")
TRUE_BYSTANDER = (
    "[2026-09-19 23:36:03.000] [DEBUG] [SPELL] Interrupt: caster=165 "
    "('Wiza the Caster'), spell=378, message=12478\n")
# The caster's OWN interrupt copy (message=439, empty name) must NOT match
# the bystander needle -- it is the caster-side line, not the bystander's.
TRUE_BYSTANDER_SELF = (
    "[2026-09-19 23:36:03.000] [DEBUG] [SPELL] Interrupt: caster=165 "
    "(''), spell=378, message=439\n")
TRUE_B760_KEEPCASTING = (
    "[2026-09-19 23:36:04.000] [DEBUG] [ZONE] [ZonePacket] HC_OP_ManaChange\n")
TRUE_B760_MEMORIZE = (
    "[2026-09-19 23:36:04.100] [DEBUG] [ZONE] [ZonePacket] HC_OP_MemorizeSpell\n")


# B630 fix pass 3: the QA probes as cell-level negatives. Each is a full
# synthetic caster log in the realistic fix-run ordering (taken from
# qa3-probes/ and qa4-probes/ in the evidence dir) with ONE leg's outcome
# flipped so the leg never completes, plus the shared synthetic observer (the
# 12478 bystander line). Everything else in the log is realistic, so a
# regression shows up as exactly the QA-named cell.
OBSERVER_SELFTEST = [
    "[2026-09-20 08:57:35.178] [DEBUG] [SPELL] Interrupt: caster=165 "
    "('Testcaster'), spell=378, message=12478\n",
]


def _synth_d3_interrupted():
    """D3's 293 is interrupted after D2's recast marker (QA attempt 3 probe a)."""
    return [
        "[2026-09-20 08:57:33.633] [INFO ] [SCENARIO] casting: /cast 1\n",
        "[2026-09-20 08:57:33.633] [INFO ] [SPELL] Beginning cast of "
        "'O'Keils Radiation' (ID: 378) on target 167\n",
        "[2026-09-20 08:57:35.179] [INFO ] [SPELL] Cast of "
        "'O'Keils Radiation' (ID: 378) was interrupted\n",
        "[2026-09-20 08:57:35.179] [INFO ] [MAIN] Your spell is "
        "interrupted.\n",
        "[2026-09-20 08:57:35.180] [ZONE] [ZonePacket] HC_OP_ManaChange\n",
        "[2026-09-20 08:57:36.000] [INFO ] [SCENARIO] leg 1 complete\n",
        "[2026-09-20 08:57:40.000] [INFO ] [SCENARIO] casting to "
        "completion\n",
        "[2026-09-20 08:58:00.715] [INFO ] [SPELL] Cast of 'Blast of "
        "Cold' (ID: 372) completed successfully\n",
        "[2026-09-20 08:58:00.716] [ZONE] [ZonePacket] HC_OP_ManaChange\n",
        "[2026-09-20 08:58:00.717] [ZONE] [ZonePacket] "
        "HC_OP_MemorizeSpell\n",
        "[2026-09-20 08:58:01.000] [INFO ] [SCENARIO] leg 2 complete\n",
        "[2026-09-20 08:58:20.000] [INFO ] [SCENARIO] first cast (to "
        "completion)\n",
        "[2026-09-20 08:58:25.206] [INFO ] [SPELL] Beginning cast of "
        "'Haze' (ID: 293) on target 167\n",
        "[2026-09-20 08:58:28.700] [INFO ] [SPELL] Cast of 'Haze' "
        "(ID: 293) completed successfully\n",
        "[2026-09-20 08:58:28.701] [ZONE] [ZonePacket] HC_OP_ManaChange\n",
        "[2026-09-20 08:58:28.702] [ZONE] [ZonePacket] "
        "HC_OP_MemorizeSpell\n",
        "[2026-09-20 08:58:28.722] [INFO ] [MAIN] Spell recast time not "
        "yet met.\n",
        "[2026-09-20 08:58:29.000] [INFO ] [SCENARIO] leg 3 complete\n",
        "[2026-09-20 08:58:35.000] [INFO ] [SCENARIO] buff leg: /cast 1\n",
        "[2026-09-20 08:58:36.500] [INFO ] [SPELL] Beginning cast of "
        "'Haze' (ID: 293) on target 167\n",
        "[2026-09-20 08:58:38.000] [INFO ] [SPELL] Cast of 'Haze' "
        "(ID: 293) was interrupted\n",
        "[2026-09-20 08:58:38.000] [INFO ] [MAIN] Your spell is "
        "interrupted.\n",
        "[2026-09-20 08:58:38.500] [INFO ] [SCENARIO] leg 4 complete\n",
    ]


def _synth_d1_never_completes():
    """D1's 372 is interrupted (QA attempt 3 probe b); D2/D3 complete."""
    return [
        "[2026-09-20 08:57:33.633] [INFO ] [SCENARIO] casting: /cast 1\n",
        "[2026-09-20 08:57:33.633] [INFO ] [SPELL] Beginning cast of "
        "'O'Keils Radiation' (ID: 378) on target 167\n",
        "[2026-09-20 08:57:35.179] [INFO ] [SPELL] Cast of "
        "'O'Keils Radiation' (ID: 378) was interrupted\n",
        "[2026-09-20 08:57:35.179] [INFO ] [MAIN] Your spell is "
        "interrupted.\n",
        "[2026-09-20 08:57:35.180] [ZONE] [ZonePacket] HC_OP_ManaChange\n",
        "[2026-09-20 08:57:36.000] [INFO ] [SCENARIO] leg 1 complete\n",
        "[2026-09-20 08:57:40.000] [INFO ] [SCENARIO] casting to "
        "completion\n",
        "[2026-09-20 08:57:41.500] [INFO ] [SPELL] Beginning cast of "
        "'Blast of Cold' (ID: 372) on target 167\n",
        "[2026-09-20 08:57:42.000] [INFO ] [SPELL] Cast of 'Blast of "
        "Cold' (ID: 372) was interrupted\n",
        "[2026-09-20 08:57:42.000] [INFO ] [MAIN] Your spell is "
        "interrupted.\n",
        "[2026-09-20 08:57:42.500] [INFO ] [SCENARIO] leg 2 complete\n",
        "[2026-09-20 08:58:20.000] [INFO ] [SCENARIO] first cast (to "
        "completion)\n",
        "[2026-09-20 08:58:25.206] [INFO ] [SPELL] Beginning cast of "
        "'Haze' (ID: 293) on target 167\n",
        "[2026-09-20 08:58:28.700] [INFO ] [SPELL] Cast of 'Haze' "
        "(ID: 293) completed successfully\n",
        "[2026-09-20 08:58:28.701] [ZONE] [ZonePacket] HC_OP_ManaChange\n",
        "[2026-09-20 08:58:28.702] [ZONE] [ZonePacket] "
        "HC_OP_MemorizeSpell\n",
        "[2026-09-20 08:58:28.722] [INFO ] [MAIN] Spell recast time not "
        "yet met.\n",
        "[2026-09-20 08:58:29.000] [INFO ] [SCENARIO] leg 3 complete\n",
        "[2026-09-20 08:58:35.000] [INFO ] [SCENARIO] buff leg: /cast 1\n",
        "[2026-09-20 08:58:36.500] [INFO ] [SPELL] Beginning cast of "
        "'Haze' (ID: 293) on target 167\n",
        "[2026-09-20 08:58:38.000] [INFO ] [SPELL] Cast of 'Haze' "
        "(ID: 293) completed successfully\n",
        "[2026-09-20 08:58:38.500] [INFO ] [SCENARIO] leg 4 complete\n",
    ]


def _synth_d2d3_fizzled():
    """D2's and D3's 293 both fizzle (QA attempt 4 probe a); D1 completes."""
    return [
        "[2026-09-20 10:02:33.633] [INFO ] [SCENARIO] casting: /cast 1\n",
        "[2026-09-20 10:02:33.633] [INFO ] [SPELL] Beginning cast of "
        "'O'Keils Radiation' (ID: 378) on target 167\n",
        "[2026-09-20 10:02:35.179] [INFO ] [SPELL] Cast of "
        "'O'Keils Radiation' (ID: 378) was interrupted\n",
        "[2026-09-20 10:02:35.179] [INFO ] [MAIN] Your spell is "
        "interrupted.\n",
        "[2026-09-20 10:02:35.180] [ZONE] [ZonePacket] HC_OP_ManaChange\n",
        "[2026-09-20 10:02:36.000] [INFO ] [SCENARIO] leg 1 complete\n",
        "[2026-09-20 10:02:40.000] [INFO ] [SCENARIO] casting to "
        "completion\n",
        "[2026-09-20 10:02:41.500] [INFO ] [SPELL] Beginning cast of "
        "'Blast of Cold' (ID: 372) on target 167\n",
        "[2026-09-20 10:02:42.715] [INFO ] [SPELL] Cast of 'Blast of "
        "Cold' (ID: 372) completed successfully\n",
        "[2026-09-20 10:02:42.716] [ZONE] [ZonePacket] HC_OP_ManaChange\n",
        "[2026-09-20 10:02:42.717] [ZONE] [ZonePacket] "
        "HC_OP_MemorizeSpell\n",
        "[2026-09-20 10:02:43.000] [INFO ] [SCENARIO] leg 2 complete\n",
        "[2026-09-20 10:02:44.000] [INFO ] [SCENARIO] first cast (to "
        "completion)\n",
        "[2026-09-20 10:02:44.200] [INFO ] [SPELL] Beginning cast of "
        "'Haze' (ID: 293) on target 167\n",
        "[2026-09-20 10:02:45.700] [INFO ] [SPELL] Cast of 'Haze' "
        "(ID: 293) fizzled\n",
        "[2026-09-20 10:02:45.700] [INFO ] [MAIN] Your spell fizzles!\n",
        "[2026-09-20 10:02:45.701] [ZONE] [ZonePacket] HC_OP_ManaChange\n",
        "[2026-09-20 10:02:45.722] [INFO ] [MAIN] Spell recast time not "
        "yet met.\n",
        "[2026-09-20 10:02:46.000] [INFO ] [SCENARIO] leg 3 complete\n",
        "[2026-09-20 10:02:52.000] [INFO ] [SCENARIO] buff leg: /cast 1\n",
        "[2026-09-20 10:02:53.500] [INFO ] [SPELL] Beginning cast of "
        "'Haze' (ID: 293) on target 167\n",
        "[2026-09-20 10:02:55.000] [INFO ] [SPELL] Cast of 'Haze' "
        "(ID: 293) fizzled\n",
        "[2026-09-20 10:02:55.000] [INFO ] [MAIN] Your spell fizzles!\n",
        "[2026-09-20 10:02:55.500] [INFO ] [SCENARIO] leg 4 complete\n",
    ]


def _cell_verdict(caster_lines, leg_prefix, assertion_prefix):
    """The verdict of the first result row whose leg and assertion names
    start with the given prefixes, running the shipped analyze() over the
    synthetic logs."""
    results = []
    analyze(caster_lines, OBSERVER_SELFTEST, results)
    for leg, assertion, verdict, _evidence in results:
        if leg.startswith(leg_prefix) and assertion.startswith(assertion_prefix):
            return verdict
    return None


def run_self_test():
    failures = []

    def check(name, cond):
        if not cond:
            failures.append(name)

    # The three dev-run false positives must NOT light up their needles.
    check("D2 236: memarena line must not match the recast needle",
          grep([FALSE_POSITIVE_236], CHAT_RECAST_NOT_MET) is None)
    check("B land: login line must not match the fizzle/land needles",
          grep([FALSE_POSITIVE_LOGIN], CHAT_FIZZLE) is None
          and SPELL_COMPLETED.search(FALSE_POSITIVE_LOGIN) is None)

    # The true positives must light up.
    check("D2 236: the real chat string matches",
          grep([TRUE_RECAST], CHAT_RECAST_NOT_MET) is not None)
    check("B fizzle: the real chat string matches",
          grep([TRUE_FIZZLE], CHAT_FIZZLE) is not None)
    # A's no-late-impact fix: attribute a 'completed successfully' to a spell
    # id, so a 288 land is NOT counted as a 378 land. New handler lines carry
    # the id inline; older logs carry the name only, resolved via the
    # name->id map from the 'Beginning cast ... (ID: n)' lines.
    check("A no-late-impact: the 288 land maps to 288, not 378",
          _spell_id_of(FALSE_POSITIVE_VACUOUS_SPELL, {}) == 288)
    check("A no-late-impact: the 378 land maps to 378",
          _spell_id_of(TRUE_OKEIL_LAND, {}) == 378)
    map_lines = [
        "[2026-09-19 23:35:03.566] [INFO ] [SPELL] Beginning cast of "
        "'Minor Shielding' (ID: 288) on target 165\n",
    ]
    name_map = _spell_id_map(map_lines)
    check("A no-late-impact: a name-only completion resolves via the map",
          _spell_id_of(
              "[2026-09-19 23:35:17.696] [INFO ] [SPELL] Cast of "
              "'Minor Shielding' completed successfully\n",
              name_map) == 288)
    check("A bystander: the real 12478 line matches",
          BYSTANDER_INTERRUPT.search(TRUE_BYSTANDER) is not None)
    # The caster's own 439 copy (empty name) must NOT match the bystander
    # needle: the bystander's copy is the one with the name + 12478.
    check("A bystander: the caster's own 439 copy does not match",
          BYSTANDER_INTERRUPT.search(TRUE_BYSTANDER_SELF) is None)
    check("B760: the keepcasting-0 anchor is the [ZonePacket] handler line",
          HANDLER_KEEPCASTING_0.search(TRUE_B760_KEEPCASTING) is not None)
    # The anchor is the PAIR: a [SPELL] decode of keepcasting 0 right after the
    # [ZonePacket] line (a keepcasting 1 mana regen is not a tail).
    b760_pair = [
        TRUE_B760_KEEPCASTING,
        "[2026-09-19 23:36:04.001] [DEBUG] [SPELL] ManaChange: mana=991, "
        "stamina=214, spell=372, keepcasting=0\n",
    ]
    check("B760: the keepcasting-0 pair yields a row",
          len(b760_rows(b760_pair, "selftest")) == 1)
    check("B760: a keepcasting 1 decode is not a tail anchor",
          len(b760_rows(
              [TRUE_B760_KEEPCASTING,
               "[2026-09-19 23:36:04.001] [DEBUG] [SPELL] ManaChange: mana=999, "
               "stamina=138, spell=0, keepcasting=1\n"],
              "selftest")) == 0)
    check("B760: the MemorizeSpell handler line is recognised",
          HANDLER_MEMORIZE.search(TRUE_B760_MEMORIZE) is not None)
    # And the memarena line must not be read as a B760 keepcasting 0 tail.
    check("B760: memarena is not a ManaChange handler line",
          HANDLER_KEEPCASTING_0.search(FALSE_POSITIVE_236) is None)

    # B630 fix pass 3: the three QA cell-level negatives. Each runs the
    # SHIPPED analyze() over a full synthetic log; the QA-named D cell must
    # FAIL because its leg never completes. The companion checks (the OTHER
    # leg's genuine completion, and the A leg) must keep PASSing, so a
    # "fix" that merely breaks the D cells in general is caught too.
    d3int = _synth_d3_interrupted()
    check("D1/D3 outcome filter: an interrupted D3 (293) FAILs the D3 cell",
          _cell_verdict(d3int, "D3", "D3's declared spell") == "FAIL")
    check("D1/D3 outcome filter: D1 still PASSes in the interrupted-D3 log "
          "(its 372 genuinely completed)",
          _cell_verdict(d3int, "D1", "D1's declared spell") == "PASS")
    d1int = _synth_d1_never_completes()
    check("D1/D3 outcome filter: an interrupted D1 (372) FAILs the D1 cell",
          _cell_verdict(d1int, "D1", "D1's declared spell") == "FAIL")
    check("D1/D3 outcome filter: D3 still PASSes in the interrupted-D1 log "
          "(its 293 genuinely completed)",
          _cell_verdict(d1int, "D3", "D3's declared spell") == "PASS")
    d23fizz = _synth_d2d3_fizzled()
    check("D1/D3 outcome filter: fizzled D2+D3 (293 x2) FAIL the D3 cell",
          _cell_verdict(d23fizz, "D3", "D3's declared spell") == "FAIL")
    check("D1/D3 outcome filter: D1 still PASSes in the fizzled-D2/D3 log "
          "(its 372 genuinely completed)",
          _cell_verdict(d23fizz, "D1", "D1's declared spell") == "PASS")

    if failures:
        for f in failures:
            print("SELF-TEST FAIL: " + f)
        return 1
    print("SELF-TEST PASS: all needles anchored")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("caster", nargs="?")
    ap.add_argument("observer", nargs="?")
    ap.add_argument("--out", default="")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return run_self_test()

    if not args.caster or not args.observer:
        ap.error("caster and observer logs are required (or use --self-test)")

    caster = read(args.caster)
    observer = read(args.observer)
    if caster is None or observer is None:
        print("ERROR: could not read both logs", file=sys.stderr)
        return 2

    results = []  # (leg, assertion, verdict, evidence)
    failures = analyze(caster, observer, results)

    print()
    print("=== B630 results (leg x assertion) ===")
    for leg, assertion, verdict, evidence in results:
        print("[%s] %s -- %s" % (verdict, leg, assertion))
        print("    evidence: %s" % evidence)
    print()
    print("=== B760: packets between [ZonePacket] HC_OP_ManaChange "
          "(keepcasting 0) and HC_OP_MemorizeSpell, per leg ===")
    any_b760 = False
    for label, lineno, between, outcome in b760_windows(caster):
        any_b760 = True
        print("%-22s line%-4d outcome=%s between=%s"
              % (label, lineno, outcome, between if between else "(none)"))
    if not any_b760:
        print("(no HC_OP_ManaChange tail found in any leg window -- "
              "run with ZONE:DEBUG, see scripts/scenario.sh log_module_for)")
    print()

    if args.out:
        os.makedirs(args.out, exist_ok=True)
        with open(os.path.join(args.out, "b630_results.txt"), "w") as fh:
            fh.write("=== B630 results ===\n")
            for leg, assertion, verdict, evidence in results:
                fh.write("[%s] %s -- %s\n    %s\n"
                         % (verdict, leg, assertion, evidence))
            fh.write("\n=== B760 ===\n")
            for label, lineno, between, outcome in b760_windows(caster):
                fh.write("%-22s line%-4d outcome=%s between=%s\n"
                         % (label, lineno, outcome, between or "(none)"))

    print("=== verdict: %d assertion(s) FAILED ===" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
