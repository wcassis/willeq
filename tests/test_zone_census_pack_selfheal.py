#!/usr/bin/env python3
"""B929 (#247): unit test for scripts/ci/zone_census_pack_selfheal.sh.

The pack self-heal is the census job's first step: before the 118-zone sweep
it verifies every swept zone's EQE1 pack with verify_eqe1 --require-v3 (the
B930/B1017 truncation-signature audit) and rebuilds the STALE ones with
eqe1_builder from the zone's .s3d. This test drives the script with STUB
verify/builder binaries, so it needs no build tree and no asset tree:

  * a clean pack (stub verify exits 0) is left alone;
  * a STALE pack (stub verify exits 1, names STALE) is rebuilt (the stub
    builder marks the pack) and re-verified;
  * a stale pack whose rebuild still fails verify fails the script (exit 1)
    -- the census job must not sweep on a pack that FATALs the benchmark;
  * a stale pack with no .s3d to rebuild from fails the script (exit 1);
  * a missing pack is skipped, not failed (the sweep's own row names it);
  * the zone list's header row is skipped.

Usage: python3 test_zone_census_pack_selfheal.py [--selfheal <path>]
"""
import os
import shutil
import stat
import subprocess
import sys
import tempfile

REPO = os.environ.get(
    "WILLEQ_SOURCE_DIR",
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SELFHEAL = os.environ.get(
    "WILLEQ_ZONE_CENSUS_PACK_SELFHEAL",
    os.path.join(REPO, "scripts", "ci", "zone_census_pack_selfheal.sh"))


def make_stub(path, body):
    with open(path, "w") as f:
        f.write("#!/bin/bash\n" + body)
    os.chmod(path, os.stat(path).st_mode | stat.S_IEXEC)


def write_list(path, zones):
    # The real list's shape: header row, then zone<TAB>expansion<TAB>bytes<TAB>notes.
    with open(path, "w") as f:
        f.write("zone\texpansion\ts3d_bytes\tnotes\n")
        for z in zones:
            f.write(f"{z}\tclassic\t123\t\n")


def run_script(packs, eq, zones, stale, still_stale_after=()):
    d = tempfile.mkdtemp(prefix="b929_selfheal_")
    verify = os.path.join(d, "verify")
    builder = os.path.join(d, "builder")
    plist = os.path.join(d, "zones.tsv")
    stale_set = " ".join("'" + s + "'" for s in sorted(stale))
    still_set = " ".join("'" + s + "'" for s in sorted(still_stale_after))
    make_stub(verify, "\n".join([
        "pack=\"${@: -1}\"",
        "base=\"$(basename \"$pack\" .eqe1)\"",
        "still=( %s )" % still_set,
        "stale=( %s )" % stale_set,
        "in_set() {",
        "    local want=\"$1\"; shift",
        "    for s in \"$@\"; do [ \"$s\" = \"$want\" ] && return 0; done",
        "    return 1",
        "}",
        "if in_set \"$base\" \"${still[@]}\" && [ -f \"$pack.healed\" ]; then",
        "    echo \"$pack: STALE (stub, still)\" >&2; exit 1",
        "fi",
        "if in_set \"$base\" \"${stale[@]}\" && [ ! -f \"$pack.healed\" ]; then",
        "    echo \"$pack: STALE (stub)\" >&2; exit 1",
        "fi",
        "exit 0",
    ]))
    make_stub(builder, "\n".join([
        "out=\"\"",
        "arch=\"\"",
        "prev=\"\"",
        "for a in \"$@\"; do",
        "    if [ \"$prev\" = \"--output\" ]; then out=\"$a\"; fi",
        "    if [ \"$prev\" = \"--archive\" ]; then arch=\"$a\"; fi",
        "    prev=\"$a\"",
        "done",
        "z=\"${arch%.s3d}\"",
        "touch \"$out/$z.eqe1.healed\"",
        "exit 0",
    ]))
    write_list(plist, zones)
    p = subprocess.run(
        ["bash", SELFHEAL, "--verify", verify, "--builder", builder,
         "--eq-path", eq, "--pack-dir", packs, "--list", plist],
        capture_output=True, text=True)
    shutil.rmtree(d, ignore_errors=True)
    return p


def main():
    if not os.path.isfile(SELFHEAL):
        print(f"SKIP (B929 pack self-heal): {SELFHEAL} not found")
        return 125

    failures = []
    d = tempfile.mkdtemp(prefix="b929_selfheal_")
    packs = os.path.join(d, "packs")
    eq = os.path.join(d, "eq")
    os.makedirs(packs)
    os.makedirs(eq)
    # good: clean pack + s3d; bad: pack + s3d (rebuildable); nostale: pack
    # but NO s3d (a stale rebuild is impossible); absent: no pack at all
    # (the sweep's own row names it).
    for z in ("good", "bad", "nostale"):
        open(os.path.join(packs, z + ".eqe1"), "w").write("x")
    for z in ("good", "bad"):
        open(os.path.join(eq, z + ".s3d"), "w").write("s3d")

    # 1. clean packs: exit 0, nothing rebuilt.
    p = run_script(packs, eq, ["good"], stale=set())
    if p.returncode != 0:
        failures.append(f"clean run should pass, got {p.returncode}: {p.stderr}")
    if "0 rebuilt, 0 failed" not in p.stdout:
        failures.append(f"clean run should report 0 rebuilt: {p.stdout}")

    # 2. one stale pack with a rebuildable .s3d: rebuilt, re-verified, exit 0.
    p = run_script(packs, eq, ["good", "bad"], stale={"bad"})
    if p.returncode != 0:
        failures.append(f"healed run should pass, got {p.returncode}: {p.stderr}")
    if not os.path.isfile(os.path.join(packs, "bad.eqe1.healed")):
        failures.append("the builder was not invoked for the stale pack")
    if "1 rebuilt, 0 failed" not in p.stdout:
        failures.append(f"healed run should report 1 rebuilt: {p.stdout}")

    # 3. a stale pack whose rebuild still fails verify: exit 1 (no sweep on it).
    p = run_script(packs, eq, ["bad"], stale={"bad"}, still_stale_after={"bad"})
    if p.returncode != 1:
        failures.append(f"still-stale run should exit 1, got {p.returncode}")

    # 4. a stale pack with no .s3d: exit 1.
    p = run_script(packs, eq, ["nostale"], stale={"nostale"})
    if p.returncode != 1:
        failures.append(f"no-s3d run should exit 1, got {p.returncode}")

    # 5. a missing pack is skipped (exit 0): the sweep's row names it.
    p = run_script(packs, eq, ["absent"], stale=set())
    if p.returncode != 0:
        failures.append(f"absent-pack run should pass, got {p.returncode}: {p.stderr}")

    # 6. the header row is skipped: a list whose only data row is clean.
    p = run_script(packs, eq, ["good"], stale={"zone"})
    if p.returncode != 0:
        failures.append(
            f"the list's header row ('zone') must be skipped, got {p.returncode}: "
            f"{p.stderr}")

    shutil.rmtree(d, ignore_errors=True)

    if failures:
        print("test_zone_census_pack_selfheal: FAIL")
        for f in failures:
            print("  " + f)
        return 1
    print("test_zone_census_pack_selfheal: PASS "
          "(clean pass-through; STALE rebuilt + re-verified; still-stale and "
          "no-s3d fail loudly; absent pack skipped; header row skipped)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
