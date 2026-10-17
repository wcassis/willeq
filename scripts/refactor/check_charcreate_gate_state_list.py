#!/usr/bin/env python3
"""B888-CC4 (GitLab #204) hygiene check: the charcreate gate's state list is ONE
list, kept in three places that must not drift apart. This nets all three:

  1. scripts/ci/charcreate_gate_controls.py  GATE_STATES [...]
  2. tools/hwcaps/charcreate_capture.cpp     the kStates[] array
  3. refactor/B888-CC4/README.md             the per-state scores table

A gate state that the capture does not emit (or that the README does not score)
is a silent coverage hole: the gate would "pass" on a state the capture never
rendered, and the README would claim scores for a state the gate never checked.
Pure text scan (no build artefact, no display) -- runs in every configuration,
hygiene label, exit 0 on agreement.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
CONTROLS = ROOT / "scripts" / "ci" / "charcreate_gate_controls.py"
CAPTURE = ROOT / "tools" / "hwcaps" / "charcreate_capture.cpp"
README = ROOT / "refactor" / "B888-CC4" / "README.md"


def gate_states():
    src = CONTROLS.read_text()
    m = re.search(r"GATE_STATES\s*=\s*\[(.*?)\]", src, re.S)
    if not m:
        sys.exit(f"FAIL: GATE_STATES not found in {CONTROLS}")
    return [x.strip().strip('"\'') for x in m.group(1).split(",") if x.strip()]


def capture_states():
    src = CAPTURE.read_text()
    # the kStates[] array: {\"name\"},  one per line
    m = re.search(r"kStates\[\s*\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        sys.exit(f"FAIL: kStates[] not found in {CAPTURE}")
    names = re.findall(r'\{\\?"([^"\\]+)\\?"\}', m.group(1))
    return names


def readme_states():
    src = README.read_text()
    # the per-state table: rows like `| select_empty | 1.000 | ... |`
    states = []
    for line in src.splitlines():
        m = re.match(r"\|\s*`?([a-z0-9_]+)`?\s*\|", line)
        if not m:
            continue
        s = m.group(1)
        # a score-table row starts with a state name (snake_case, no spaces);
        # skip the header row itself
        if s == "state":
            continue
        if re.fullmatch(r"[a-z0-9_]+", s) and " " not in s:
            states.append(s)
    return states


def main():
    g, cp, rd = gate_states(), capture_states(), readme_states()
    print(f"GATE_STATES ({len(g)}): {g}")
    print(f"capture kStates ({len(cp)}): {cp}")
    print(f"README table ({len(rd)}): {rd}")
    ok = True
    if sorted(g) != sorted(cp):
        print(f"FAIL: GATE_STATES != capture kStates; only in gate: "
              f"{sorted(set(g) - set(cp))}; only in capture: "
              f"{sorted(set(cp) - set(g))}")
        ok = False
    if sorted(g) != sorted(rd):
        print(f"FAIL: GATE_STATES != README table; only in gate: "
              f"{sorted(set(g) - set(rd))}; only in README: "
              f"{sorted(set(rd) - set(g))}")
        ok = False
    if not ok:
        sys.exit(1)
    print("OK: the gate's state list, the capture's kStates and the README table "
          "all name the same states.")


if __name__ == "__main__":
    main()
