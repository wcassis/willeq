#!/usr/bin/env python3
"""Check that a refactor/B###/ directory's number is not ALSO the number of
an unrelated refactor/BOARD.md entry (B908, GitLab #228).

B908's own case: `refactor/B100/` held the soldunga EQE1 name-truncation
census, because a worker used the GitLab ISSUE number (#100) as a B number.
`refactor/BOARD.md` already had a DIFFERENT, unrelated struck entry headed
**B100** (T368's consumer-less `InputAction` enumerators) since 2026-09-05.
Nothing checked that a directory's number and a board header naming the same
number were describing the same thing, so "B100" silently named two different
items for weeks.

This is deliberately narrow: it does not try to detect every possible number
collision in the tree (docs/comments citing a B number in passing are noisy
and any keyword heuristic over free text produces false positives). It checks
exactly the shape B908 found: a `refactor/B<N>/` directory (which is durable,
committed evidence for one specific item) against every BOARD.md bullet
HEADER (`- **B<N>**` or `- ~~**B<N>**`, struck or live) naming that same N. If
a directory exists for N, at least one header for N must share a distinctive
keyword with the directory's own declared subject (its README.md or
SUMMARY.md title line) -- otherwise the two are almost certainly unrelated
items that collided on one number.

Usage:
    check_board_dir_numbers.py [--repo REPO_ROOT]

Exit 0 if every numbered directory's subject overlaps at least one BOARD.md
header naming the same number (always printing what was checked); exit 1 if
a directory's number heads a BOARD.md entry with no shared keyword.
"""
import argparse
import os
import re
import sys

BOARD_REL = os.path.join("refactor", "BOARD.md")

DIR_RE = re.compile(r"^B(\d+)$")
HEADER_RE = re.compile(r"^-\s+(?:~~)?\*\*B(\d+)\*\*")
TITLE_RE = re.compile(r"^#\s*B\d+\s*[-—]\s*(.+)$")

# Words too generic to count as a "shared keyword" between a directory's
# subject and a board header -- both halves of nearly every card use these.
STOPWORDS = {
    "the", "and", "for", "with", "from", "that", "this", "have", "has",
    "not", "are", "was", "were", "its", "into", "over", "than", "then",
    "board", "class", "closed", "issue", "card", "unit", "fixed", "fix",
    "entry", "file", "files", "code", "test", "tests", "line", "lines",
    "declared", "consumer", "producer", "declared", "table", "census",
    "commit", "committed", "holds", "which",
}


def keywords(text):
    words = re.findall(r"[A-Za-z][A-Za-z0-9_]{3,}", text.lower())
    return {w for w in words if w not in STOPWORDS}


def find_numbered_dirs(repo):
    refactor_dir = os.path.join(repo, "refactor")
    out = []
    if not os.path.isdir(refactor_dir):
        return out
    for name in sorted(os.listdir(refactor_dir)):
        m = DIR_RE.match(name)
        if not m:
            continue
        out.append((int(m.group(1)), os.path.join(refactor_dir, name)))
    return out


def directory_subject(dir_path):
    """Return (subject_text, source_rel_path) for a refactor/B<N>/ directory,
    or (None, None) if it declares no title we can read."""
    for candidate in ("README.md", "SUMMARY.md"):
        p = os.path.join(dir_path, candidate)
        if not os.path.isfile(p):
            continue
        try:
            with open(p, "r", encoding="utf-8", errors="replace") as fh:
                for line in fh:
                    line = line.strip()
                    if not line:
                        continue
                    m = TITLE_RE.match(line)
                    if m:
                        return m.group(1), candidate
                    # No "# B<N> -- subject" heading found on the first
                    # non-blank line; fall back to the whole line as text.
                    return line, candidate
        except OSError:
            continue
    return None, None


def board_headers(board_path):
    """Return {number: [(line_no, entry_text), ...]} for every top-level
    BOARD.md bullet headed **B<N>** (struck or live). entry_text spans the
    header bullet's own line plus its continuation lines (indented, no new
    top-level bullet) so headers with a short subject on the next line still
    contribute keywords."""
    with open(board_path, "r", encoding="utf-8", errors="replace") as fh:
        lines = fh.readlines()

    result = {}
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        m = HEADER_RE.match(line)
        if not m:
            i += 1
            continue
        number = int(m.group(1))
        start = i
        text_parts = [line]
        j = i + 1
        while j < n:
            nxt = lines[j]
            if nxt.startswith("- ") or nxt.startswith("## ") or nxt.startswith("# "):
                break
            if nxt.strip() == "":
                break
            text_parts.append(nxt)
            j += 1
        entry_text = "".join(text_parts)
        result.setdefault(number, []).append((start + 1, entry_text))
        i = j
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    args = ap.parse_args()
    repo = os.path.abspath(args.repo)

    board_path = os.path.join(repo, BOARD_REL)
    numbered_dirs = find_numbered_dirs(repo)
    headers = board_headers(board_path) if os.path.isfile(board_path) else {}

    checked = 0
    no_subject = []
    no_header = []
    failures = []

    for number, dir_path in numbered_dirs:
        rel_dir = os.path.relpath(dir_path, repo)
        subject, source = directory_subject(dir_path)
        if subject is None:
            no_subject.append(rel_dir)
            continue
        dir_kw = keywords(subject)
        checked += 1

        entries = headers.get(number, [])
        if not entries:
            # No BOARD.md header claims this number at all -- nothing to
            # collide with.
            no_header.append(rel_dir)
            continue

        matched = False
        mismatches = []
        for line_no, entry_text in entries:
            entry_kw = keywords(entry_text)
            if dir_kw & entry_kw:
                matched = True
            else:
                mismatches.append((line_no, entry_text.strip().splitlines()[0][:100]))

        if not matched:
            failures.append((rel_dir, subject, number, mismatches))

    print("BoardDirNumbers: %d refactor/B###/ director%s found, %d checked "
          "against %s (%d with no readable title, %d with no matching "
          "BOARD.md header at all)"
          % (len(numbered_dirs), "y" if len(numbered_dirs) == 1 else "ies",
             checked, BOARD_REL, len(no_subject), len(no_header)))

    if not failures:
        print("OK: every numbered directory shares a keyword with every "
              "BOARD.md header naming its number.")
        return 0

    for rel_dir, subject, number, mismatches in failures:
        print("FAIL  %s (subject: %r) shares no keyword with BOARD.md's "
              "**B%d** header(s):" % (rel_dir, subject, number))
        for line_no, first_line in mismatches:
            print("      %s:%d: %s" % (BOARD_REL, line_no, first_line))
        print("      -> B%d likely names two different items; renumber the "
              "directory (or the board entry) so each item has its own "
              "number." % number)
    print("BoardDirNumbers: %d failure(s)." % len(failures))
    return 1


if __name__ == "__main__":
    sys.exit(main())
