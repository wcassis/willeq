#!/usr/bin/env python3
"""
T407 -- the written-never-read struct/field census.

Why this exists: T404 (board B254 work) found `MixChannel::sampleRate` written
by every producer and read by nobody, for years, two lines below a TSAN banner
that discussed the atomicity of its neighbours.  A field nothing reads is the
same reachability defect class as an uncalled function, and nothing in the tree
looked for it.

What it does:
  1. Parses every struct/class body under include/client/ and src/client/ and
     collects its DATA MEMBERS (declaration lines: no parentheses, ending in
     `;`, at exactly one brace level inside the type).
  2. Sweeps src/ include/ tests/ tools/ benchmarks/ for every occurrence of
     those names, comment- and string-stripped, classifying each as a WRITE
     (`.name =`, `->name =`, `name +=`, a `: name(...)` / `, name{...}`
     constructor initialiser) or a READ (anything else).
  3. Reports the fields with writes and zero reads, and the fields with no
     occurrence at all outside their own declaration.

Accuracy note -- deliberately asymmetric.  Member names are matched by
IDENTIFIER, not by receiver type, so a name shared with another class (`name`,
`id`, `x`) collects that class's occurrences too.  Every such collision can
only ADD reads, so it can only HIDE a write-only field, never invent one.  The
report is therefore a floor: everything it names is worth looking at, and it
misses some.  Sort by how distinctive the name is when triaging.

Usage:
    python3 scripts/refactor/check_write_only_fields.py [--repo ROOT]
                [--min-writes N] [--include-unused] [--json OUT]
"""

import argparse
import os
import re
import sys
from collections import defaultdict

DECL_DIRS = ("include/client", "src/client")
SWEEP_DIRS = ("src", "include", "tests", "tools", "benchmarks")
EXTS = (".h", ".hpp", ".cpp", ".cc", ".inl")

# Names that are almost always something else as well; they are still reported,
# but the collision makes their read counts meaningless.
NOISE = {"x", "y", "z", "w", "r", "g", "b", "a", "i", "j", "k", "n", "m",
         "id", "name", "type", "size", "count", "data", "value", "index",
         "first", "second", "begin", "end", "length", "width", "height"}

BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
LINE_COMMENT = re.compile(r"//[^\n]*")
STRINGS = re.compile(r'"(?:\\.|[^"\\])*"' r"|'(?:\\.|[^'\\])*'")

TYPE_OPEN = re.compile(r"^\s*(?:template\s*<[^>]*>\s*)?"
                       r"(struct|class|union)\s+(?:\w+_API\s+)?(\w+)\b")

DECL = re.compile(
    r"^\s*(?:mutable\s+|static\s+|constexpr\s+|inline\s+|volatile\s+|"
    r"thread_local\s+|const\s+)*"          # storage/cv prefixes
    r"[A-Za-z_][\w:<>,\s\*&]*?"            # the type
    r"[\s\*&]"                             # separator before the name
    r"(\w+)"                               # the member name
    r"\s*(?:\[[^\]]*\])*"                  # optional array extents
    r"\s*(?:=[^;]*|\{[^;]*\}|:\s*\d+)?"    # optional initialiser / bitfield
    r"\s*;\s*$")

SKIP_DECL_PREFIX = ("typedef", "using", "friend", "return", "static_assert",
                    "public:", "private:", "protected:", "enum", "namespace",
                    "template", "operator", "delete", "throw", "case", "goto")


def strip_noncode(text):
    """Blank out block comments, line comments and string/char literals."""
    text = BLOCK_COMMENT.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    text = LINE_COMMENT.sub("", text)
    text = STRINGS.sub('""', text)
    return text


def walk(root, dirs):
    for d in dirs:
        base = os.path.join(root, d)
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [x for x in dirnames
                           if x not in ("third_party", "build", ".git")]
            for fn in filenames:
                if fn.endswith(EXTS):
                    yield os.path.join(dirpath, fn)


def collect_members(root):
    """{(TypeName, member): (relpath, lineno)} plus a member -> types index."""
    members = {}
    for path in walk(root, DECL_DIRS):
        rel = os.path.relpath(path, root)
        try:
            src = strip_noncode(open(path, errors="replace").read())
        except OSError:
            continue
        depth = 0
        stack = []          # (type_name, depth_at_open)
        pending = None      # a type header seen, brace not yet opened
        for lineno, line in enumerate(src.split("\n"), 1):
            code = line.split("//")[0]
            m = TYPE_OPEN.match(code)
            if m and not code.rstrip().endswith(";") and "(" not in code:
                pending = m.group(2)
            if stack and depth == stack[-1][1] + 1:
                stripped = code.strip()
                if (stripped and "(" not in stripped and ")" not in stripped
                        and not stripped.startswith(SKIP_DECL_PREFIX)):
                    d = DECL.match(code)
                    if d:
                        nm = d.group(1)
                        if nm not in ("struct", "class", "union", "const",
                                      "static", "unsigned", "signed"):
                            members.setdefault((stack[-1][0], nm), (rel, lineno))
            opens = code.count("{")
            closes = code.count("}")
            for _ in range(opens):
                depth += 1
                if pending is not None:
                    stack.append((pending, depth - 1))
                    pending = None
            for _ in range(closes):
                if stack and stack[-1][1] == depth - 1:
                    stack.pop()
                depth = max(0, depth - 1)
        # end file
    return members


ASSIGN = re.compile(r"^\s*(?:\+|-|\*|/|%|\||&|\^|<<|>>)?=(?!=)")
CTOR_INIT_PREFIX = re.compile(r"[:,]\s*$")


def sweep(root, names):
    """name -> {'w': [(rel, lineno)], 'r': [(rel, lineno)]}"""
    hits = defaultdict(lambda: {"w": [], "r": []})
    pat = re.compile(r"\b(" + "|".join(sorted(map(re.escape, names),
                                              key=len, reverse=True)) + r")\b")
    for path in walk(root, SWEEP_DIRS):
        rel = os.path.relpath(path, root)
        try:
            src = strip_noncode(open(path, errors="replace").read())
        except OSError:
            continue
        for lineno, line in enumerate(src.split("\n"), 1):
            for m in pat.finditer(line):
                nm = m.group(1)
                before = line[:m.start()]
                after = line[m.end():]
                is_write = False
                if ASSIGN.match(after):
                    is_write = True
                elif (after[:1] in ("(", "{")
                      and CTOR_INIT_PREFIX.search(before)):
                    is_write = True
                hits[nm]["w" if is_write else "r"].append((rel, lineno))
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    ap.add_argument("--min-writes", type=int, default=1)
    ap.add_argument("--include-unused", action="store_true",
                    help="also list members with no occurrence at all")
    ap.add_argument("--json")
    args = ap.parse_args()
    root = os.path.abspath(args.repo)

    members = collect_members(root)
    names = {nm for (_t, nm) in members}
    sys.stderr.write("declared members: %d (%d distinct names) in %s\n"
                     % (len(members), len(names), ", ".join(DECL_DIRS)))
    hits = sweep(root, names)

    write_only, unused = [], []
    for (tname, nm), (rel, lineno) in sorted(members.items()):
        h = hits.get(nm, {"w": [], "r": []})
        # the declaration itself, if it carried an initialiser, is not a use
        w = [p for p in h["w"] if not (p[0] == rel and p[1] == lineno)]
        r = [p for p in h["r"] if not (p[0] == rel and p[1] == lineno)]
        if not r and len(w) >= args.min_writes:
            write_only.append((tname, nm, rel, lineno, w))
        elif not r and not w:
            unused.append((tname, nm, rel, lineno))

    print("=" * 78)
    print("WRITE-ONLY FIELDS (written somewhere, read nowhere)")
    print("=" * 78)
    for tname, nm, rel, lineno, w in write_only:
        flag = "  [common name -- read count unreliable]" if nm in NOISE else ""
        print("\n%s::%s   %s:%d%s" % (tname, nm, rel, lineno, flag))
        for p, l in w[:12]:
            print("    W  %s:%d" % (p, l))
        if len(w) > 12:
            print("    ... %d more writes" % (len(w) - 12))
    print("\n%d write-only fields." % len(write_only))

    if args.include_unused:
        print("\n" + "=" * 78)
        print("FIELDS WITH NO OCCURRENCE OUTSIDE THEIR DECLARATION")
        print("=" * 78)
        for tname, nm, rel, lineno in unused:
            print("%s::%s   %s:%d" % (tname, nm, rel, lineno))
        print("\n%d unused fields." % len(unused))

    if args.json:
        import json
        with open(args.json, "w") as f:
            json.dump({"write_only": [
                {"type": t, "field": n, "file": rel, "line": ln,
                 "writes": [{"file": a, "line": b} for a, b in w]}
                for t, n, rel, ln, w in write_only],
                "unused": [{"type": t, "field": n, "file": rel, "line": ln}
                           for t, n, rel, ln in unused]}, f, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
