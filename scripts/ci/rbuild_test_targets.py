#!/usr/bin/env python3
"""B1208 (#527) -- which targets must be built before `ctest -R <regex>` can run.

The CI `rbuild` job (.gitlab-ci.yml) runs `rbuild --via ci <wt> test -R <re>`
on whichever x86 runner takes it, often in a tree that has never built the
test's executable. gtest discovery is PRE_TEST (tests/CMakeLists.txt), so an
unbuilt executable lists only `<target>_NOT_BUILT` and `ctest -N -R <re>`
cannot name the target. This helper answers from the SOURCES instead:

  --write-query <tree>   drop a CMake file-API codemodel query into <tree>
                         (before the configure; the configure writes the reply)
  --tree <tree> --regex <re>
                         print the executable targets (space separated) whose
                         sources declare a gtest case `Suite.Name` the regex
                         matches (TEST / TEST_F / TEST_P / TYPED_TEST /
                         TYPED_TEST_P; ctest's -R is a search, so a
                         parameterised `Prefix/Suite.Name/0` matches whenever
                         `Suite.Name` does), plus the target behind every
                         already-listed non-NOT_BUILT test the regex matches
                         (`ctest --show-only=json-v1`: a python test whose
                         command is a script maps to no target). Exit 3 and a
                         message when nothing matches: pass --target instead.

The regex is applied with Python's `re.search`; ctest's own regex dialect is a
subset for the patterns rbuild callers use (`-R '^PacketTest\\.'`,
`-R SpellSlot`).
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

GTEST_RE = re.compile(
    r"\b(?:TEST|TEST_F|TEST_P|TYPED_TEST|TYPED_TEST_P)\s*\(\s*([A-Za-z_]\w*)\s*,\s*([A-Za-z_]\w*)\s*[,)]")


def write_query(tree):
    q = Path(tree) / ".cmake" / "api" / "v1" / "query" / "codemodel-v2"
    q.parent.mkdir(parents=True, exist_ok=True)
    q.touch()
    return q


def load_codemodel(tree):
    """[(name, type, [source paths], [artifact paths])] from the file-API reply."""
    reply = Path(tree) / ".cmake" / "api" / "v1" / "reply"
    idx = sorted(reply.glob("index-*.json"))
    if not idx:
        raise SystemExit(f"rbuild_test_targets: no file-API reply in {reply} (was --write-query run before the configure?)")
    index = json.loads(idx[-1].read_text())
    cm = None
    for obj in index.get("objects", []):
        if obj.get("kind") == "codemodel":
            cm = json.loads((reply / obj["jsonFile"]).read_text())
            break
    if cm is None:
        raise SystemExit("rbuild_test_targets: the file-API reply has no codemodel")
    src_root = Path(cm["paths"]["source"])
    build_root = Path(cm["paths"]["build"])
    out = []
    seen = set()
    for conf in cm.get("configurations", [])[:1]:
        for t in conf.get("targets", []):
            if t["name"] in seen:
                continue
            seen.add(t["name"])
            tj = json.loads((reply / t["jsonFile"]).read_text())
            srcs = [src_root / s["path"] if not Path(s["path"]).is_absolute() else Path(s["path"])
                    for s in tj.get("sources", [])]
            arts = [build_root / a["path"] if not Path(a["path"]).is_absolute() else Path(a["path"])
                    for a in tj.get("artifacts", [])]
            out.append((tj["name"], tj.get("type", ""), srcs, arts))
    return out


def gtest_names(text):
    return {f"{a}.{b}" for a, b in GTEST_RE.findall(text)}


def targets_from_sources(targets, rx, read=lambda p: p.read_text(errors="replace")):
    hit = []
    for name, ttype, srcs, _ in targets:
        if ttype != "EXECUTABLE":
            continue
        for s in srcs:
            if s.suffix not in (".cpp", ".cc", ".cxx"):
                continue
            try:
                names = gtest_names(read(s))
            except OSError:
                continue
            if any(rx.search(n) for n in names):
                hit.append(name)
                break
    return hit


def targets_from_ctest(tree, regex, targets):
    try:
        out = subprocess.run(["ctest", "--test-dir", str(tree), "--show-only=json-v1", "-R", regex],
                             capture_output=True, text=True, timeout=300).stdout
        tests = json.loads(out).get("tests", [])
    except (OSError, ValueError, subprocess.SubprocessError):
        return []
    by_art = {}
    for name, ttype, _, arts in targets:
        if ttype == "EXECUTABLE":
            for a in arts:
                by_art[str(a)] = name
    hit = []
    for t in tests:
        if t.get("name", "").endswith("_NOT_BUILT"):
            continue
        cmd = t.get("command") or []
        if cmd and cmd[0] in by_art:
            hit.append(by_art[cmd[0]])
    return hit


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write-query", metavar="TREE")
    ap.add_argument("--tree")
    ap.add_argument("--regex")
    a = ap.parse_args(argv)
    if a.write_query:
        print(f"rbuild_test_targets: codemodel query at {write_query(a.write_query)}", file=sys.stderr)
        return 0
    if not (a.tree and a.regex):
        ap.error("--tree and --regex are required (or --write-query)")
    try:
        rx = re.compile(a.regex)
    except re.error as e:
        print(f"rbuild_test_targets: bad regex {a.regex!r}: {e}", file=sys.stderr)
        return 2
    targets = load_codemodel(a.tree)
    found = []
    for n in targets_from_sources(targets, rx) + targets_from_ctest(a.tree, a.regex, targets):
        if n not in found:
            found.append(n)
    if not found:
        print(f"rbuild_test_targets: no executable target declares a test matching {a.regex!r}; "
              "pass --target <t> to rbuild", file=sys.stderr)
        return 3
    print(f"rbuild_test_targets: -R {a.regex!r} -> {' '.join(found)}", file=sys.stderr)
    print(" ".join(found))
    return 0


if __name__ == "__main__":
    sys.exit(main())
