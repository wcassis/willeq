#!/usr/bin/env python3
"""Verify a commit range only MOVES code between C++ files, without editing it.

Compares the multiset of normalized source lines across all changed .cpp/.h
files between two revisions. A pure move (splitting a TU) preserves the
multiset except for boilerplate (includes, guards, comments, blank lines),
which is allowlisted.

Usage:
    verify_move_only.py BASE..HEAD [--repo DIR]
    verify_move_only.py BASE HEAD  [--repo DIR]

Exit 0: move-only (allowing boilerplate). Exit 1: substantive lines were
added or removed — listed in the output for review. CMakeLists.txt changes
are reported but never counted as violations.
"""

import argparse
import re
import subprocess
import sys
from collections import Counter

# Lines that may legitimately appear/disappear during a TU split.
BOILERPLATE = re.compile(
    r"^(#include\b|#pragma\b|#ifndef\b|#ifdef\b|#define\b|#endif\b"
    r"|//|/\*|\*|\*/|namespace\s+[\w:]*\s*\{?$|\}\s*(//.*)?$)"
)


def git(repo, *args):
    r = subprocess.run(["git", "-C", repo] + list(args),
                       capture_output=True, text=True)
    if r.returncode != 0 and "does not exist" not in r.stderr:
        # git show on a file absent at a rev returns nonzero; treat as empty
        if args[0] != "show":
            sys.exit(f"git {' '.join(args)} failed: {r.stderr.strip()}")
    return r.stdout


def normalized_lines(text):
    out = []
    for line in text.splitlines():
        s = line.strip()
        if not s:
            continue
        out.append(s)
    return out


def collect(repo, rev, paths):
    counter = Counter()
    for p in paths:
        content = git(repo, "show", f"{rev}:{p}")
        counter.update(normalized_lines(content))
    return counter


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("range", nargs="+",
                    help="BASE..HEAD or two revisions BASE HEAD")
    ap.add_argument("--repo", default=".")
    args = ap.parse_args()

    if len(args.range) == 1 and ".." in args.range[0]:
        base, head = args.range[0].split("..", 1)
        head = head.lstrip(".") or "HEAD"
    elif len(args.range) == 2:
        base, head = args.range
    else:
        ap.error("provide BASE..HEAD or BASE HEAD")

    changed = git(args.repo, "diff", "--name-only", base, head).split()
    cxx = [p for p in changed if p.endswith((".cpp", ".h", ".hpp", ".cc"))]
    other = [p for p in changed if p not in cxx]

    if other:
        print("Non-C++ files changed (not verified, review separately):")
        for p in other:
            print(f"  {p}")

    if not cxx:
        print("No C++ files changed.")
        return 0

    old = collect(args.repo, base, cxx)
    new = collect(args.repo, head, cxx)

    added = new - old
    removed = old - new

    def split_boiler(counter):
        boiler, subst = Counter(), Counter()
        for line, n in counter.items():
            (boiler if BOILERPLATE.match(line) else subst)[line] = n
        return boiler, subst

    add_boiler, add_subst = split_boiler(added)
    rem_boiler, rem_subst = split_boiler(removed)

    print(f"\nFiles verified: {len(cxx)}")
    print(f"Boilerplate: +{sum(add_boiler.values())} / -{sum(rem_boiler.values())} lines (allowed)")

    ok = True
    if add_subst:
        ok = False
        print(f"\nSUBSTANTIVE ADDED lines ({sum(add_subst.values())}):")
        for line, n in sorted(add_subst.items()):
            print(f"  +{n}x  {line[:120]}")
    if rem_subst:
        ok = False
        print(f"\nSUBSTANTIVE REMOVED lines ({sum(rem_subst.values())}):")
        for line, n in sorted(rem_subst.items()):
            print(f"  -{n}x  {line[:120]}")

    if ok:
        print("\nPASS: move-only (modulo boilerplate).")
        return 0
    print("\nFAIL: substantive line changes detected. If intentional "
          "(e.g. de-static-ing a helper), document in the unit review "
          "section for orchestrator sign-off.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
