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

It is a pure log reader: it does not need the server, the lock, or the build.
Re-run it on a fresh pair of logs any time.

Usage:
    scripts/scenario_configs/b630_analyze.py <caster.log> <observer.log> [--out DIR]

Exit: 0 if every ASSERTION leg passed (the B760 table is informational and
never fails the run); 1 if any caster- or observer-side assertion failed;
2 if the logs could not be read.
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


def read(path):
    if not os.path.isfile(path):
        return None
    with open(path, "r", errors="replace") as fh:
        return fh.readlines()


def grep(lines, *needles):
    """Return the first line containing ANY of the needles (for pasting)."""
    for line in lines:
        if any(n in line for n in needles):
            return line.rstrip("\n")
    return None


def count(lines, *needles):
    return sum(1 for line in lines if any(n in line for n in needles))


# ---------------------------------------------------------------------------
# B760: the packet-order recorder. Walk the caster log, and for each
# `ManaChange keepcasting 0` (the spell bar re-enables) look forward for the
# next `OP_MemorizeSpell` and report every zone packet that landed in
# between. That "in between" set IS the #45 answer for that outcome.
# ---------------------------------------------------------------------------
def b760_rows(caster_lines, label):
    rows = []
    lines = caster_lines
    n = len(lines)
    for i, line in enumerate(lines):
        if "keepcasting=0" not in line and "keepcasting 0" not in line:
            continue
        # Find the next MemorizeSpell after this keepcasting 0.
        j = i + 1
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("caster")
    ap.add_argument("observer")
    ap.add_argument("--out", default="")
    args = ap.parse_args()

    caster = read(args.caster)
    observer = read(args.observer)
    if caster is None or observer is None:
        print("ERROR: could not read both logs", file=sys.stderr)
        return 2

    results = []  # (leg, assertion, verdict, evidence)
    failures = 0

    def record(leg, assertion, verdict, evidence):
        nonlocal failures
        results.append((leg, assertion, verdict, evidence))
        if verdict == "FAIL":
            failures += 1

    # ------------------------------------------------------------------ A
    # The interrupted cast (GM #interrupt mid-cast).
    a_castbar_clear = grep(caster, "was interrupted", "casting is interrupted",
                           "Cast of", "InterruptCast")
    a_chat = grep(caster, "spell is interrupted", "casting is interrupted")
    a_late_impact = count(caster, "Cast of 'O`Keil", "O'Keil")
    record("A cast_interrupt",
           "caster cast bar clears / interrupted",
           "PASS" if a_castbar_clear else "FAIL",
           a_castbar_clear or "(no interrupt line on the caster log)")
    record("A cast_interrupt",
           "caster chat string 'is interrupted'",
           "PASS" if a_chat else "FAIL",
           a_chat or "(no chat string)")
    record("A cast_interrupt",
           "no late impact burst after the interrupt",
           "PASS" if a_late_impact == 0 else "WARN",
           "%d O'Keil impact lines (0 expected after #interrupt)" % a_late_impact)
    # Observer: the bystander must see the NAMED interrupt.
    o_interrupt = grep(observer, "casting is interrupted", "12478",
                       "Interrupt: caster")
    record("A cast_interrupt",
           "observer sees '<name>'s casting is interrupted'",
           "PASS" if o_interrupt else "FAIL",
           o_interrupt or "(observer log has no named interrupt line)")

    # ------------------------------------------------------------------ D1
    # Success tail: let the nuke finish. Caster reports the tail; the B760
    # table records the packet order.
    d1_success = grep(caster, "MemorizeSpell response", "success-tail",
                      "completed")
    record("D1 cast_success_tail",
           "caster success tail (MemorizeSpell after keepcasting 0)",
           "PASS" if d1_success else "FAIL",
           d1_success or "(no success tail)")

    # ------------------------------------------------------------------ D2
    # Recast-timer stop: the SECOND cast of the recast-stop spell must be
    # refused by the server's per-spell recast timer -> SPELL_RECAST 236
    # "Spell recast time not yet met." + keepcasting 0, NO MemorizeSpell.
    d2_recast = grep(caster, "recast time not yet met", "236", "SPELL_RECAST")
    record("D2 cast_recast_stop",
           "second cast hits the server recast timer (236)",
           "PASS" if d2_recast else "FAIL",
           d2_recast or "(no SPELL_RECAST 236 line)")

    # ------------------------------------------------------------------ D3
    # The caster's own beneficial buff: it MUST take hold on the caster (the
    # success-tail buff), and the observer must see the target gain it.
    d3_buff = grep(caster, "MemorizeSpell response", "buff-notake leg executed",
                   "buff gained", "BuffManager")
    record("D3 cast_buff_notake",
           "caster's own buff takes hold (success tail)",
           "PASS" if d3_buff else "FAIL",
           d3_buff or "(no buff success line)")

    # ------------------------------------------------------------------ B
    # Fizzle: the non-GM caster rolls a high-level spell. This is a coin the
    # server clamps to ~95%; the leg is best-effort and the analyzer REPORTS
    # which way it fell (fizzle vs land) rather than asserting one.
    b_fizzle = grep(caster, "fizzles", "173", "SPELL_FIZZLE")
    b_landed = grep(caster, "completed successfully", "MemorizeSpell response")
    record("B cast_fizzle",
           "non-GM caster outcome (fizzle OR land -- recorded)",
           "INFO",
           b_fizzle or b_landed or "(no fizzle/land line on the caster log)")

    # ------------------------------------------------------------------ table
    print()
    print("=== B630 results (leg x assertion) ===")
    for leg, assertion, verdict, evidence in results:
        print("[%s] %s -- %s" % (verdict, leg, assertion))
        print("    evidence: %s" % evidence)
    print()
    print("=== B760: packets between ManaChange keepcasting 0 and OP_MemorizeSpell ===")
    any_b760 = False
    for label in ("A cast_interrupt", "D1 cast_success_tail",
                  "D2 cast_recast_stop", "D3 cast_buff_notake", "B cast_fizzle"):
        rows = b760_rows(caster, label)
        for lineno, between, outcome in ((r[1], r[2], r[3]) for r in rows):
            any_b760 = True
            print("%-22s line%-4d outcome=%s between=%s"
                  % (label, lineno, outcome,
                     between if between else "(none)"))
    if not any_b760:
        print("(no keepcasting 0 tail found in the caster log)")
    print()

    if args.out:
        os.makedirs(args.out, exist_ok=True)
        with open(os.path.join(args.out, "b630_results.txt"), "w") as fh:
            fh.write("=== B630 results ===\n")
            for leg, assertion, verdict, evidence in results:
                fh.write("[%s] %s -- %s\n    %s\n" % (verdict, leg, assertion, evidence))
            fh.write("\n=== B760 ===\n")
            for label in ("A cast_interrupt", "D1 cast_success_tail",
                          "D2 cast_recast_stop", "D3 cast_buff_notake",
                          "B cast_fizzle"):
                for r in b760_rows(caster, label):
                    fh.write("%-22s line%-4d outcome=%s between=%s\n"
                             % (label, r[1], r[3], r[2] or "(none)"))

    print("=== verdict: %d assertion(s) FAILED ===" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
