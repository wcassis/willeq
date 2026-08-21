#!/usr/bin/env python3
"""Fail if any EverQuest::Send*/Request* method has ZERO production callers.

T168. This makes the zero-caller-sender arc's result permanent instead of a
fact someone has to rediscover by sweeping.

WHY IT MATTERS, in the arc's own words: a zero-caller sender is not "correct
but unreached", it is UNVERIFIED -- nothing ever exercised it, so nothing ever
contradicted it. Across T148-T168 roughly one in two of them turned out to be
broken on the wire, several in ways that could never have worked at all (wrong
body, wrong direction, wrong size). 28 of 97 senders were unreachable when the
arc opened; this script is what keeps that number at zero.

WHAT IT DOES NOT CATCH -- read this before trusting a green run:
  * It counts CALL SITES, not reachability. A caller that is itself dead still
    counts (that was reachability pattern #10: SkillManager::activateSkill had
    a caller, and no reachable one).
  * It only sees named Send*/Request* methods. SkillManager::sendSkillUsePacket
    reaches 17 skill opcodes with no named sender at all -- T151's blind spot.
    A new opcode route that does not go through a Send* method is invisible
    here.

Exit 0 clean, 1 if an unlisted sender has no production caller, 2 on a usage
or environment error (never a silent pass -- see check_file_sizes.sh's T37
note for why that distinction is spelled out).
"""

import os
import re
import sys

# Senders deliberately kept with no caller. Each entry needs a reason and the
# unit that decided it. Empty is the intended steady state: the T168 triage
# deleted the last five rather than parking them here, because a sender nobody
# calls and nobody is allowed to call is exactly the trap refactor/T147 names.
ALLOWLIST = {
    # "SendFoo": "why, and the unit that decided it",
}


def main() -> int:
    repo = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    header = os.path.join(repo, "include", "client", "eq.h")
    if not os.path.isfile(header):
        print(f"ERROR {header} not found -- nothing was checked")
        return 2

    decl = re.compile(r"^\s*(?:void|bool)\s+((?:Send|Request)\w+)\s*\(", re.M)
    senders = sorted(set(decl.findall(open(header, errors="ignore").read())))
    if not senders:
        print("ERROR no Send*/Request* declarations found -- nothing was checked")
        return 2

    sources = []
    for root, _, files in os.walk(os.path.join(repo, "src")):
        for f in files:
            if f.endswith((".cpp", ".h")):
                sources.append(os.path.join(root, f))
    blob = "\n".join(open(p, errors="ignore").read() for p in sources)

    orphans = []
    for s in senders:
        # every mention that is not the out-of-line definition itself
        calls = len(re.findall(r"\b" + s + r"\s*\(", blob)) - \
                len(re.findall(r"EverQuest::" + s + r"\s*\(", blob))
        if calls <= 0 and s not in ALLOWLIST:
            orphans.append(s)

    print(f"checked {len(senders)} Send*/Request* methods in {len(sources)} "
          f"production sources; {len(ALLOWLIST)} allowlisted")
    if orphans:
        print("")
        print("FAIL -- these senders have no production caller:")
        for s in orphans:
            print(f"  {s}")
        print("")
        print("A sender nothing calls is UNVERIFIED, not merely unreached.")
        print("Wire it to a command/UI trigger, delete it, or add it to")
        print("ALLOWLIST in this script with a reason and a unit number.")
        return 1

    print("PASS -- every sender has at least one production call site.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
