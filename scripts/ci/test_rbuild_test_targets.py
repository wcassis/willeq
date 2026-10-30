#!/usr/bin/env python3
"""B1208 (#527) -- pin scripts/ci/rbuild_test_targets.py, the `rbuild` CI job's
`RBUILD_TARGET=auto` resolver (`rbuild --via ci <wt> test -R <re>` with no
--target): a fake CMake file-API reply with three executables, and the regex ->
target answers the job relies on, including the negatives (a non-executable
target and a header never count; no match exits 3)."""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rbuild_test_targets as rtt  # noqa: E402


def make_tree(root):
    src, build = root / "src", root / "build"
    (src / "tests").mkdir(parents=True)
    (src / "tests" / "test_combat.cpp").write_text(
        "TEST(SpellSlotRules, SourceNeedsBeginCast) {}\nTEST_F(CombatFixture, Swing) {}\n")
    (src / "tests" / "test_packet.cpp").write_text("TEST(PacketTest, RoundTrip) {}\n")
    (src / "tests" / "test_param.cpp").write_text("TEST_P(SpellSlotParam,\n  Each) {}\n")
    (src / "tests" / "decl.h").write_text("TEST(SpellSlotHeader, Never) {}\n")
    (src / "lib.cpp").write_text("TEST(SpellSlotLib, NotAnExe) {}\n")
    reply = build / ".cmake" / "api" / "v1" / "reply"
    reply.mkdir(parents=True)
    targets = [("test_combat", "EXECUTABLE", ["tests/test_combat.cpp", "tests/decl.h"]),
               ("test_packet", "EXECUTABLE", ["tests/test_packet.cpp"]),
               ("test_param", "EXECUTABLE", ["tests/test_param.cpp"]),
               ("eqlib", "STATIC_LIBRARY", ["lib.cpp"])]
    refs = []
    for name, ttype, srcs in targets:
        f = f"target-{name}.json"
        (reply / f).write_text(json.dumps({"name": name, "type": ttype,
                                           "sources": [{"path": s} for s in srcs],
                                           "artifacts": [{"path": f"bin/{name}"}]}))
        refs.append({"name": name, "jsonFile": f})
    (reply / "codemodel-v2-x.json").write_text(json.dumps({
        "paths": {"source": str(src), "build": str(build)},
        "configurations": [{"name": "Release", "targets": refs}]}))
    (reply / "index-1.json").write_text(json.dumps({"objects": [{"kind": "codemodel", "jsonFile": "codemodel-v2-x.json"}]}))
    return build


def main():
    fails = []
    with tempfile.TemporaryDirectory() as d:
        build = make_tree(Path(d))
        import re
        tg = rtt.load_codemodel(build)
        for rx, want in (("SpellSlot", ["test_combat", "test_param"]), ("^PacketTest\\.", ["test_packet"]),
                         ("CombatFixture.Swing", ["test_combat"]), ("SpellSlotParam.Each", ["test_param"]),
                         ("SpellSlotLib|SpellSlotHeader", []), ("NoSuchSuite", [])):
            got = sorted(rtt.targets_from_sources(tg, re.compile(rx)))
            if got != want:
                fails.append(f"-R {rx!r}: got {got}, want {want}")
        rc = rtt.main(["--tree", str(build), "--regex", "NoSuchSuite"])
        if rc != 3:
            fails.append(f"no match must exit 3, got {rc}")
        q = rtt.write_query(Path(d) / "fresh")
        if not q.is_file() or q.name != "codemodel-v2":
            fails.append("--write-query must create .cmake/api/v1/query/codemodel-v2")
    for f in fails:
        print("FAIL:", f)
    if fails:
        return 1
    print("OK: rbuild_test_targets maps -R regexes to the executables that declare them")
    return 0


if __name__ == "__main__":
    sys.exit(main())
