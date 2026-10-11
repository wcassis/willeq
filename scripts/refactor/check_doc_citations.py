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

  3. B1020: an ANCHORED citation, `path/to/file.ext:NNN (anchor)` or
     `path/to/file.ext:NNN-MMM (anchor)` -- the parenthesis must follow the
     line number (or its closing backtick) after exactly one space. On top of
     rule 1, FAIL unless `anchor` (one pair of surrounding backticks or
     quotes stripped) occurs verbatim on line NNN, or on some line of
     NNN..MMM. Rule 1 alone was hygiene-green on wrong line numbers (wave-7
     retrospective ids 5 and 37, /mnt/projects/fable/review/RETRO-WAVE7/):
     a line that exists is not the line the prose means. A parenthesis right
     after a citation therefore IS an anchor; put prose elsewhere.
     Rule 3 also runs over the unit write-ups under refactor/B<NNN>*/ (where
     the wave-7 misses were), checking ONLY anchored citations there -- the
     path rules 1 and 2 are not applied to those historical files.

Citations are resolved relative to the repo root, with two exceptions
(K00 Leg C):
  - A citation whose path starts with `review/` is resolved against
    `/mnt/projects/fable/review/` instead — the sanctioned outside-repo
    media directory (`refactor/EXECUTOR_PROTOCOL.md`). OK if it exists
    there, FAIL otherwise: `review/` citations are still real pointers that
    can go stale, just not against this checkout. T502: that check is only
    authoritative for a checkout that lives ALONGSIDE the media — i.e. under
    `/mnt/projects/fable/`, which every canonical checkout and executor
    worktree does. Checked from anywhere else (the GitLab runner builds under
    `/home/user/builds/…`), `review/` citations are reported SKIP instead.
    The runner host happens to carry a *stale* rsync of the archive, which is
    the reason the rule is "is this checkout a sibling of the media" and not
    "does the directory exist": resolving against a stale copy would report a
    host's sync lag as 19 stale citations, which only teaches CI to ignore
    this test.
  - A citation whose path is absolute (starts with `/`) is a deliberate
    cross-project reference (e.g. a sibling repo's source file, cited for
    context) that this checker has no tree to resolve against. Reported as
    SKIP, not FAIL or OK — recorded and counted, but not a pass/fail signal.

Usage:
    check_doc_citations.py [--repo REPO_ROOT]
    check_doc_citations.py --self-test   # rule 3's probes, no tree scanned

Exit 0 if every citation found resolves (including the case of zero live
docs found, or zero citations found — always printed, never a silent pass,
same T37 rule check_file_sizes.sh follows); exit 1 if any citation FAILs.
SKIPs never affect the exit code.
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


def find_unit_docs(repo_root):
    """B1020: every *.md under refactor/B<digits>*/ -- the per-unit READMEs
    the kanban workers write. Only rule 3 (anchored citations) is applied to
    them; see the module docstring."""
    found = []
    refactor_dir = os.path.join(repo_root, "refactor")
    if not os.path.isdir(refactor_dir):
        return found
    for name in sorted(os.listdir(refactor_dir)):
        full = os.path.join(refactor_dir, name)
        if not (os.path.isdir(full) and re.match(r"B[0-9]", name)):
            continue
        for dirpath, dirnames, filenames in os.walk(full):
            dirnames.sort()
            for fn in sorted(filenames):
                if fn.endswith(".md"):
                    found.append(os.path.join(dirpath, fn))
    return found


# Path charset: letters, digits, underscore, dot, dash, slash. Extension set
# for the WITH-line form is intentionally a little wider than the bare-path
# FAIL rule (which the plan pins to .cpp|.h|.py|.sh|.md) — .hpp and .cmake
# citations with an explicit line number are just as checkable. The leading
# "/?" admits an absolute path (SKIP, cross-project — see module docstring)
# on top of the normal repo-relative form.
#
# The negative lookbehind before "/?" matters: without it, a placeholder
# like "<slot>/scripts/foo.sh" (EXECUTOR_PROTOCOL.md's own convention for
# "some worktree slot") would match starting at the "/" right after "<slot>"
# and get misread as the absolute path "/scripts/foo.sh" instead of the
# correctly-relative "scripts/foo.sh". Requiring the character before an
# optional leading "/" NOT be a path-token character (alnum/._-/>) means a
# leading "/" only starts a match when it truly opens a token (start of
# string, whitespace, a backtick, a bracket, ":" etc).
_EXT_WITH_LINE = r"cpp|h|hpp|py|sh|md|cmake"
_EXT_BARE = r"cpp|h|py|sh|md"
_NOT_PATH_TOKEN_BEFORE = r"(?<![A-Za-z0-9_.\->])"

CITE_WITH_LINE = re.compile(
    r"%s(?P<path>/?[A-Za-z0-9_.\-]+(?:/[A-Za-z0-9_.\-]+)+\.(?:%s)):(?P<line>[0-9]+)"
    % (_NOT_PATH_TOKEN_BEFORE, _EXT_WITH_LINE)
)
# B1020: the anchored form. Same path/line shape as CITE_WITH_LINE plus an
# optional "-END" range, an optional closing backtick (the usual
# "`path:12` (anchor)" markdown), ONE space, and a parenthesis with no nested
# parentheses.
CITE_ANCHORED = re.compile(
    r"%s(?P<path>/?[A-Za-z0-9_.\-]+(?:/[A-Za-z0-9_.\-]+)+\.(?:%s)):(?P<line>[0-9]+)"
    r"(?:-(?P<end>[0-9]+))?`? \((?P<anchor>[^()]*)\)"
    % (_NOT_PATH_TOKEN_BEFORE, _EXT_WITH_LINE)
)


def normalize_anchor(raw):
    """The token to look for: whitespace trimmed, then ONE pair of matching
    surrounding backticks or quotes removed."""
    a = raw.strip()
    if len(a) >= 2 and a[0] == a[-1] and a[0] in "`\"'":
        a = a[1:-1]
    return a


# T398: the bare form needs a TRAILING guard too. Without one,
# "data/willeq.eqe1.sha256" matched as the path "data/willeq.eqe1.sh" (the
# ".sh" extension, with "a256" simply left unmatched) and FAILed as a
# nonexistent file. The guard excludes a following alnum/underscore/dash but
# deliberately NOT a following ".", so a sentence-ending citation
# ("... see src/foo.cpp.") still matches. The WITH-line form is already safe
# because its ":" anchors the end of the path.
_NOT_PATH_TOKEN_AFTER = r"(?![A-Za-z0-9_\-])"

CITE_BARE = re.compile(
    r"%s(?P<path>/?[A-Za-z0-9_.\-]+(?:/[A-Za-z0-9_.\-]+)+\.(?:%s))%s"
    % (_NOT_PATH_TOKEN_BEFORE, _EXT_BARE, _NOT_PATH_TOKEN_AFTER)
)

# K00 Leg C: review/ is the sanctioned outside-repo media directory
# (refactor/EXECUTOR_PROTOCOL.md) -- citations into it resolve against this
# instead of the checkout.
REVIEW_DIR = "/mnt/projects/fable/review"


def _review_dir_is_authoritative(repo_root):
    """True when this checkout sits alongside the review media -- any checkout
    under the workspace root that REVIEW_DIR belongs to (/mnt/projects/fable:
    `main`, `wt/*`). Elsewhere (the CI runner's own builds directory) the
    media either is not there or is an out-of-date rsync of it, and a
    review/ citation cannot be judged from here at all."""
    workspace = os.path.dirname(REVIEW_DIR.rstrip("/"))
    root = os.path.realpath(repo_root)
    return os.path.isdir(REVIEW_DIR) and (
        root == workspace or root.startswith(workspace + os.sep))


def _skip_reason(cited_path):
    """Why this citation was not resolved -- an outside-repo media path on a
    host that has no review/ directory, or a cross-project absolute path."""
    if cited_path.startswith("review/"):
        return "%s not authoritative for this checkout" % REVIEW_DIR
    return "cross-project citation"


def resolve_target(repo_root, cited_path):
    """Returns (target_path, status) where status is None (resolve normally),
    or "SKIP" for an absolute cross-project path. review/ paths resolve
    against REVIEW_DIR rather than SKIPping -- they are still real pointers
    this host can check, EXCEPT on a host with no REVIEW_DIR at all (T502:
    the CI runner), where they SKIP."""
    if cited_path.startswith("review/"):
        if not _review_dir_is_authoritative(repo_root):
            return cited_path, "SKIP"
        return os.path.join(REVIEW_DIR, cited_path[len("review/"):]), None
    if cited_path.startswith("/"):
        return cited_path, "SKIP"
    return os.path.join(repo_root, cited_path), None


def _anchored_by_start(line):
    """{match start: anchored match} for every rule-3 citation on a line."""
    return {m.start("path"): m for m in CITE_ANCHORED.finditer(line)}


def _check_anchored(doc_path, lineno, repo_root, m):
    end = int(m.group("end")) if m.group("end") else None
    return check_with_line(doc_path, lineno, repo_root, m.group("path"),
                           int(m.group("line")), end_line=end,
                           anchor=normalize_anchor(m.group("anchor")))


def scan_unit_doc(doc_path, repo_root, results):
    """Rule 3 only (B1020): anchored citations in a refactor/B<NNN>*/ doc."""
    with open(doc_path, "r", encoding="utf-8", errors="replace") as fh:
        for lineno, line in enumerate(fh, start=1):
            for m in CITE_ANCHORED.finditer(line):
                results.append(_check_anchored(doc_path, lineno, repo_root, m))


def scan_doc(doc_path, repo_root, results):
    with open(doc_path, "r", encoding="utf-8", errors="replace") as fh:
        for lineno, line in enumerate(fh, start=1):
            claimed_spans = []
            anchored = _anchored_by_start(line)
            for m in CITE_WITH_LINE.finditer(line):
                claimed_spans.append(m.span())
                am = anchored.get(m.start("path"))
                if am is not None:
                    results.append(_check_anchored(doc_path, lineno, repo_root, am))
                    continue
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


def check_with_line(doc_path, doc_lineno, repo_root, cited_path, cited_line,
                    end_line=None, anchor=None):
    label = "%s:%d" % (cited_path, cited_line)
    if end_line is not None:
        label += "-%d" % end_line
    if anchor is not None:
        label += " (%s)" % anchor
    target, status = resolve_target(repo_root, cited_path)
    if status == "SKIP":
        return ("SKIP", doc_path, doc_lineno, label, _skip_reason(cited_path))
    if not os.path.isfile(target):
        return ("FAIL", doc_path, doc_lineno, label, "file does not exist")
    with open(target, "r", encoding="utf-8", errors="replace") as fh:
        lines = fh.read().splitlines()
    n_lines = len(lines)
    last = end_line if end_line is not None else cited_line
    if cited_line > n_lines or last > n_lines:
        return ("FAIL", doc_path, doc_lineno, label,
                "line %d beyond file length (%d lines)" % (last, n_lines))
    if end_line is not None and end_line < cited_line:
        return ("FAIL", doc_path, doc_lineno, label,
                "range %d-%d runs backwards" % (cited_line, end_line))
    if anchor is not None:
        if not anchor:
            return ("FAIL", doc_path, doc_lineno, label, "empty anchor")
        if not any(anchor in l for l in lines[max(cited_line - 1, 0):last]):
            where = ("line %d" % cited_line if end_line is None
                     else "lines %d-%d" % (cited_line, end_line))
            return ("FAIL", doc_path, doc_lineno, label,
                    "anchor not on %s (re-measure: git grep -n the anchor at HEAD)"
                    % where)
    return ("OK", doc_path, doc_lineno, label, "")


def check_bare(doc_path, doc_lineno, repo_root, cited_path):
    target, status = resolve_target(repo_root, cited_path)
    if status == "SKIP":
        return ("SKIP", doc_path, doc_lineno, cited_path, _skip_reason(cited_path))
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
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run rule 3's (anchored citation) probes on a temp tree and exit",
    )
    args = parser.parse_args()
    if args.self_test:
        return self_test()

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
        scan_doc(doc, repo_root, results)
    unit_docs = [d for d in find_unit_docs(repo_root) if d not in set(docs)]
    print("checking anchored citations only in %d unit doc(s) under refactor/B*/"
          % len(unit_docs))
    for doc in unit_docs:
        scan_unit_doc(doc, repo_root, results)

    n_ok = sum(1 for r in results if r[0] == "OK")
    n_fail = sum(1 for r in results if r[0] == "FAIL")
    n_skip = sum(1 for r in results if r[0] == "SKIP")

    for status, doc_path, doc_lineno, cite, reason in results:
        rel_doc = os.path.relpath(doc_path, repo_root)
        if status == "OK":
            print("OK    %s:%d cites %s" % (rel_doc, doc_lineno, cite))
        elif status == "SKIP":
            print("SKIP  %s:%d cites %s — %s" % (rel_doc, doc_lineno, cite, reason))
        else:
            print("FAIL  %s:%d cites %s — %s" % (rel_doc, doc_lineno, cite, reason))

    if not results:
        print("checked %d live doc(s), 0 citations found — nothing was checked, this is not a pass" % len(docs))
        return 0

    print(
        "checked %d live doc(s) + %d unit doc(s) (anchored only), %d citation(s): "
        "%d ok, %d fail, %d skip"
        % (len(docs), len(unit_docs), len(results), n_ok, n_fail, n_skip)
    )
    return 1 if n_fail else 0


# B1020 self-test: (doc line, expected status) probes against a two-file
# temp tree. Every shape rule 3 must accept, and every one it must refuse.
_SELF_TEST_SOURCE = (
    "int first();\n"            # 1
    "void anchorTarget(int x);\n"  # 2
    "// \"quoted token\" here\n"  # 3
    "int last();\n"             # 4
)
_SELF_TEST_PROBES = [
    # anchored, correct
    ("`src/a.cpp:2` (`anchorTarget`)", "OK"),
    ("src/a.cpp:2 (anchorTarget)", "OK"),
    ("`src/a.cpp:3` (\"quoted token\")", "OK"),
    ("`src/a.cpp:1-3` (`anchorTarget`)", "OK"),
    # anchored, wrong line: the wave-7 shape (ids 5, 37)
    ("`src/a.cpp:1` (`anchorTarget`)", "FAIL"),
    ("`src/a.cpp:3-4` (`anchorTarget`)", "FAIL"),
    # prose in the parenthesis is not on the line either
    ("`src/a.cpp:2` (the declaration)", "FAIL"),
    ("`src/a.cpp:2` ()", "FAIL"),
    # rule 1 still applies to an anchored citation
    ("`src/a.cpp:9` (`last`)", "FAIL"),
    ("`src/a.cpp:3-9` (`last`)", "FAIL"),
    ("`src/missing.cpp:1` (`first`)", "FAIL"),
    # not anchored: no space / two spaces / prose after a comma
    ("`src/a.cpp:1`(`anchorTarget`)", "OK"),
    ("`src/a.cpp:1`  (`anchorTarget`)", "OK"),
    ("`src/a.cpp:1`, the first declaration (`anchorTarget` is on :2)", "OK"),
    # absolute cross-project paths still SKIP
    ("`/opt/x/y.cpp:3` (`anything`)", "SKIP"),
]


def self_test():
    import tempfile
    failed = 0
    with tempfile.TemporaryDirectory() as root:
        os.makedirs(os.path.join(root, "src"))
        os.makedirs(os.path.join(root, "refactor", "B1"))
        with open(os.path.join(root, "src", "a.cpp"), "w") as fh:
            fh.write(_SELF_TEST_SOURCE)
        for scanner, doc_rel in ((scan_doc, "CLAUDE.md"),
                                 (scan_unit_doc, "refactor/B1/README.md")):
            for text, want in _SELF_TEST_PROBES:
                doc = os.path.join(root, doc_rel)
                with open(doc, "w") as fh:
                    fh.write(text + "\n")
                results = []
                scanner(doc, root, results)
                if scanner is scan_unit_doc and not CITE_ANCHORED.search(text):
                    # Rule 3 only: a non-anchored line yields nothing there.
                    got = "OK" if not results else results[0][0]
                else:
                    got = results[0][0] if results else "NONE"
                ok = got == want
                failed += 0 if ok else 1
                print("%s  [%s] %s -> %s (want %s)"
                      % ("ok  " if ok else "FAIL", doc_rel, text, got, want))
    print("anchored-citation self-test: %d probes, %d failed"
          % (2 * len(_SELF_TEST_PROBES), failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
