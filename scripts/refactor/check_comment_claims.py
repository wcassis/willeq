#!/usr/bin/env python3
"""Check that C++ comments do not restate a retired measured constant (T445).

THE HOLE THIS FILLS. `DocClaims`, `DocCounts` and `DocCitations` all stop at
`*.md`. T444 wrote that out loud and B457 is what it cost: T428 solved the
night ambient live against the oracle and moved it, and three comments in
`include/client/graphics/vision_ambient.h` -- lines 35, 177 and 195 -- went on
quoting the pre-T428 triple `0.08/0.08/0.15` while `kNightAmbient` five lines
away said `{0.26244, 0.31147, 0.24003}`. Nothing in the tree could see it,
because no doc net reads a comment.

That header is precisely the place a future unit reads to sanity-check a
vision floor, and solving a floor against a stale base is the mistake T428
found in T405's own floors (off by 2-3x). So a comment that restates a
measured constant is not cosmetic rot; it is a trap with a demonstrated
victim.

THE RULE. A measured constant is stated ONCE, at its definition. A comment
names the constant, never the number. This script is the ratchet: a table of
(file, retired pattern, why, unit) rows, matched against COMMENT TEXT ONLY --
`//` to end of line and `/* ... */`, with string literals and code left alone,
so the constant's own definition is never a hit.

A comment may still quote a retired value deliberately (a tombstone: "the old
Infravision floor was (0.147, 0.081, 0.059)"). Such a line carries the marker
RETIRED-OK, exactly as check_doc_claims.py does. Keep those rare.

Usage:
    check_comment_claims.py [--repo REPO_ROOT]

Exit 0 if no scanned comment restates a retired constant (always printing
what was checked -- never a silent pass, the T37 rule); exit 1 on any hit,
naming file, line, the offending text and what to write instead. It is also
an error for a table row to name a file that does not exist: an exemption
must not outlive its file.
"""
import argparse
import os
import re
import sys

EXEMPT_MARKER = "RETIRED-OK"

# (repo-relative file, regex over comment text, what is true now, unit).
RETIRED = [
    (os.path.join("include", "client", "graphics", "vision_ambient.h"),
     r"0\.08\s*[/,]\s*0\.08\s*[/,]\s*0\.15",
     "the night ambient is kNightAmbient = {0.26244, 0.31147, 0.24003}, "
     "solved live by T428; name the constant, not the triple",
     "T445 (B457)"),
]

# NOTE ON SCOPE. The table deliberately holds one row. It is a RATCHET, not a
# sweep: a row goes in when a constant has actually MOVED and a comment has
# actually been caught restating the old value, which is the only case where a
# future reader is misled. Pinning every literal that happens to appear in a
# comment would fail on legitimate tombstones -- vision_ambient.h itself has
# two, the pre-T428 dawn/dusk step and the pre-T405 Infravision floor -- and
# turn the check into noise nobody reads.


def comment_spans(text):
    """Yield (start, end) of every C++ comment in `text`.

    A hand-rolled scanner rather than a regex, because the point is to be
    blind to code: a string literal that happens to contain `//` must not
    open a comment, and a comment's own contents must not close a string.
    """
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            quote = c
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n:
            if text[i + 1] == "/":
                end = text.find("\n", i)
                end = n if end == -1 else end
                yield (i, end)
                i = end
                continue
            if text[i + 1] == "*":
                end = text.find("*/", i + 2)
                end = n if end == -1 else end + 2
                yield (i, end)
                i = end
                continue
        i += 1


def comment_lines(path):
    """(lineno, comment text, full source line) for every comment in `path`."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    src_lines = text.split("\n")
    out = []
    for start, end in comment_spans(text):
        first = text.count("\n", 0, start)
        chunk = text[start:end]
        for offset, piece in enumerate(chunk.split("\n")):
            lineno = first + offset + 1
            full = src_lines[lineno - 1] if lineno - 1 < len(src_lines) else ""
            out.append((lineno, piece, full))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    args = ap.parse_args()
    repo = os.path.abspath(args.repo)

    failures = []
    exempted = 0
    files = sorted({row[0] for row in RETIRED})

    cache = {}
    for rel in files:
        full = os.path.join(repo, rel)
        if not os.path.isfile(full):
            print("FAIL  %s: table row names a file that does not exist. "
                  "Delete the row with the file." % rel)
            failures.append(rel)
            continue
        cache[rel] = comment_lines(full)

    for rel, pattern, truth, unit in RETIRED:
        if rel not in cache:
            continue
        rx = re.compile(pattern)
        for lineno, piece, full in cache[rel]:
            m = rx.search(piece)
            if not m:
                continue
            if EXEMPT_MARKER in full:
                exempted += 1
                continue
            failures.append((rel, lineno, m.group(0), truth, unit))

    total_comment_lines = sum(len(v) for v in cache.values())
    print("CommentClaims: %d file(s) scanned, %d comment line(s), %d retired "
          "pattern(s) in the table, %d exempted line hit(s) (%s)"
          % (len(cache), total_comment_lines, len(RETIRED), exempted,
             EXEMPT_MARKER))

    if not failures:
        print("OK: no scanned comment restates a retired constant.")
        return 0

    for f in failures:
        if isinstance(f, tuple):
            rel, lineno, hit, truth, unit = f
            print("FAIL  %s:%d comment restates the retired '%s' -- %s (%s). "
                  "Name the constant, or mark the line %s if it is a "
                  "deliberate tombstone."
                  % (rel, lineno, hit, truth, unit, EXEMPT_MARKER))
    print("CommentClaims: %d failure(s)." % len(failures))
    return 1


if __name__ == "__main__":
    sys.exit(main())
