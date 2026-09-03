#!/usr/bin/env python3
"""Check that file citations in the live docs resolve against the tree (K00 Leg B).

A live doc drifts from the tree it describes the moment a cited file moves,
is deleted, or shrinks below a cited line number — and nothing used to catch
that. This walks only the LIVE docs (not refactor/completed/, not the T/J/P
plan files, which are historical by design) and checks every citation it
finds against the checkout on disk.

Two citation shapes are checked:
  1. `path/to/file.ext:NNN` — a path with a line number. FAIL if the path
     does not exist, or if NNN is beyond the file's line count.
  2. A bare `path/to/file.ext` (no line number, but the path contains a "/"
     and ends in one of .cpp/.h/.py/.sh/.md) — FAIL if the path does not
     exist. This catches a citation that dropped its line number but is
     still pointing at a moved/deleted file.

Citations are resolved relative to the repo root. A citation to a path
outside this repo (e.g. a sibling project's source file, cited for context)
correctly FAILs here — that is a real finding for Leg A to triage, not a
bug in this checker; this script does not fix docs, only reports.

Usage:
    check_doc_citations.py [--repo REPO_ROOT]

Exit 0 if every citation found resolves (including the case of zero live
docs found, or zero citations found — always printed, never a silent pass,
same T37 rule check_file_sizes.sh follows); exit 1 if any citation FAILs.
"""
import argparse
import os
import re
import sys

# Live-doc set (K00_truth_reset.md B2): root CLAUDE.md, every */CLAUDE.md,
# docs/*.md (not docs/reference/), refactor/BOARD.md,
# refactor/EXECUTOR_PROTOCOL.md, refactor/K_*.md. NOT refactor/completed/,
# NOT the T/J/P plan files.
PRUNE_DIRS = {"third_party", ".git"}


def is_build_dir(name):
    return name.startswith("build")


def find_claude_md_files(repo_root):
    found = []
    for dirpath, dirnames, filenames in os.walk(repo_root):
        dirnames[:] = [
            d for d in dirnames if d not in PRUNE_DIRS and not is_build_dir(d)
        ]
        if "CLAUDE.md" in filenames:
            found.append(os.path.join(dirpath, "CLAUDE.md"))
    return sorted(found)


def find_live_docs(repo_root):
    docs = []
    notes = []

    docs.extend(find_claude_md_files(repo_root))

    docs_dir = os.path.join(repo_root, "docs")
    if os.path.isdir(docs_dir):
        for name in sorted(os.listdir(docs_dir)):
            full = os.path.join(docs_dir, name)
            if os.path.isfile(full) and name.endswith(".md"):
                docs.append(full)
    else:
        notes.append("docs/ not found — skipped")

    refactor_dir = os.path.join(repo_root, "refactor")
    for rel in ("BOARD.md", "EXECUTOR_PROTOCOL.md"):
        full = os.path.join(refactor_dir, rel)
        if os.path.isfile(full):
            docs.append(full)
        else:
            notes.append(
                "refactor/%s not found — skipped (Leg A may not have merged yet)"
                % rel
            )

    if os.path.isdir(refactor_dir):
        for name in sorted(os.listdir(refactor_dir)):
            if name.startswith("K_") and name.endswith(".md"):
                docs.append(os.path.join(refactor_dir, name))
    else:
        notes.append("refactor/ not found — skipped")

    # De-dupe while keeping order (root/docs/refactor CLAUDE.md files can't
    # collide with the docs/refactor globs above, but be defensive).
    seen = set()
    deduped = []
    for d in docs:
        if d not in seen:
            seen.add(d)
            deduped.append(d)
    return deduped, notes


# Path charset: letters, digits, underscore, dot, dash, slash. Extension set
# for the WITH-line form is intentionally a little wider than the bare-path
# FAIL rule (which the plan pins to .cpp|.h|.py|.sh|.md) — .hpp and .cmake
# citations with an explicit line number are just as checkable.
_EXT_WITH_LINE = r"cpp|h|hpp|py|sh|md|cmake"
_EXT_BARE = r"cpp|h|py|sh|md"

CITE_WITH_LINE = re.compile(
    r"(?P<path>[A-Za-z0-9_.\-]+(?:/[A-Za-z0-9_.\-]+)+\.(?:%s)):(?P<line>[0-9]+)"
    % _EXT_WITH_LINE
)
CITE_BARE = re.compile(
    r"(?P<path>[A-Za-z0-9_.\-]+(?:/[A-Za-z0-9_.\-]+)+\.(?:%s))" % _EXT_BARE
)


def scan_doc(doc_path, repo_root, results):
    with open(doc_path, "r", encoding="utf-8", errors="replace") as fh:
        for lineno, line in enumerate(fh, start=1):
            claimed_spans = []
            for m in CITE_WITH_LINE.finditer(line):
                claimed_spans.append(m.span())
                results.append(
                    check_with_line(doc_path, lineno, repo_root, m.group("path"), int(m.group("line")))
                )
            for m in CITE_BARE.finditer(line):
                span = m.span()
                # Skip a bare match that is really the path portion of a
                # with-line citation already recorded above.
                if any(span[0] >= s[0] and span[0] < s[1] for s in claimed_spans):
                    continue
                results.append(
                    check_bare(doc_path, lineno, repo_root, m.group("path"))
                )


def check_with_line(doc_path, doc_lineno, repo_root, cited_path, cited_line):
    target = os.path.join(repo_root, cited_path)
    if not os.path.isfile(target):
        return ("FAIL", doc_path, doc_lineno, "%s:%d" % (cited_path, cited_line),
                "file does not exist")
    with open(target, "r", encoding="utf-8", errors="replace") as fh:
        n_lines = sum(1 for _ in fh)
    if cited_line > n_lines:
        return ("FAIL", doc_path, doc_lineno, "%s:%d" % (cited_path, cited_line),
                "line %d beyond file length (%d lines)" % (cited_line, n_lines))
    return ("OK", doc_path, doc_lineno, "%s:%d" % (cited_path, cited_line), "")


def check_bare(doc_path, doc_lineno, repo_root, cited_path):
    target = os.path.join(repo_root, cited_path)
    if not os.path.isfile(target):
        return ("FAIL", doc_path, doc_lineno, cited_path, "file does not exist")
    return ("OK", doc_path, doc_lineno, cited_path, "")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo",
        default=None,
        help="repo root to check (default: this script's own checkout)",
    )
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    default_repo_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
    repo_root = os.path.abspath(args.repo) if args.repo else default_repo_root

    if not os.path.isdir(repo_root):
        print("ERROR repo root %s does not exist — nothing was checked" % repo_root)
        return 2

    docs, notes = find_live_docs(repo_root)
    for note in notes:
        print("NOTE %s" % note)

    if not docs:
        print("no live docs found in %s — nothing was checked, this is not a pass" % repo_root)
        return 0

    print("checking %d live doc(s) in %s" % (len(docs), repo_root))

    results = []
    for doc in docs:
        rel_doc = os.path.relpath(doc, repo_root)
        scan_doc(doc, repo_root, results)

    n_ok = sum(1 for r in results if r[0] == "OK")
    n_fail = sum(1 for r in results if r[0] == "FAIL")

    for status, doc_path, doc_lineno, cite, reason in results:
        rel_doc = os.path.relpath(doc_path, repo_root)
        if status == "OK":
            print("OK    %s:%d cites %s" % (rel_doc, doc_lineno, cite))
        else:
            print("FAIL  %s:%d cites %s — %s" % (rel_doc, doc_lineno, cite, reason))

    if not results:
        print("checked %d live doc(s), 0 citations found — nothing was checked, this is not a pass" % len(docs))
        return 0

    print(
        "checked %d live doc(s), %d citation(s): %d ok, %d fail"
        % (len(docs), len(results), n_ok, n_fail)
    )
    return 1 if n_fail else 0


if __name__ == "__main__":
    sys.exit(main())
