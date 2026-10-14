#!/usr/bin/env python3
"""B929 (#247): unit test for scripts/ci/zone_census_pack_selfheal.sh.

The pack self-heal is the census job's first step: it builds a LOCAL EQE1
pack tree the sweep reads, because the runner's NFS pack tree is
READ-ONLY for the runner (measured, MR !182 pipeline 1174 job 6273:
eqe1_builder "Cannot open output" on the NFS path after a successful 90 s
ETC1 encode). The sweep runs WITHOUT --composite-entities, so the
benchmark opens packs only by the loader's fixed names -- every swept
zone's three packs (zone, zone_obj, zone_chr) plus the fixed pack list
(zone_bench_loader.cpp's eqe1Names array minus its zone-derived entries,
re-read from the source). That MANDATORY set is what the local tree must
carry: each pack is hard-linked (a second name for the same inode -- the
source is never written), and a STALE mandatory pack (B930/B1017's
31-char v1/v2 signature) is rebuilt with eqe1_builder from <name>.s3d
into the local tree. A mandatory pack that is missing, or stale with no
.s3d, fails the script; the other ~1,100 packs in the NFS tree are not
linked (the runner's out dir is another filesystem -- copying them for
packs the sweep never opens would waste gigabytes).

This test drives the script with STUB verify/builder binaries, so it needs
no build tree and no asset tree:

  * a clean run: every mandatory pack (the zone's three + the fixed
    list) linked into the local tree, the non-mandatory pack NOT linked,
    the source untouched, exit 0;
  * a STALE mandatory zone (with its .s3d) is rebuilt (the stub builder
    marks the tree's copy) and re-verified, exit 0;
  * a mandatory pack whose rebuild still fails verify fails the script
    (exit 1);
  * a mandatory STALE pack with no .s3d fails the script (exit 1);
  * a MANDATORY pack (a fixed-list entry) missing from the source tree
    fails the script (exit 1) -- the sweep would die on it;
  * a MANDATORY zone missing from the source tree fails the script
    (exit 1);
  * a NON-mandatory pack missing from the source tree is skipped
    (exit 0, and it does not appear in the local tree);
  * a stale NON-mandatory pack is left alone (exit 0, the builder was
    not invoked for it, nothing linked for it);
  * the zone list's header row is skipped.

The fixed list is parsed from zone_bench_loader.cpp exactly as the script
does (the eqe1Names array's string literals minus its _obj/_chr suffix
entries), so a loader change that adds a pack the sweep would FATAL on
lands in this test's mandatory set with no edit here.

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
FIXED_LIST_FILE = os.path.join(REPO, "tools", "hwcaps", "zone_bench_loader.cpp")


def fixed_list():
    """The eqe1Names array's string literals minus its zone-derived
    entries -- the same awk/grep extraction the script runs."""
    p = subprocess.run(
        ["bash", "-c",
         "awk '/eqe1Names = {/,/};/' '" + FIXED_LIST_FILE + "' "
         "| grep -o '\"[a-zA-Z0-9_]*\"' | tr -d '\"' "
         "| grep -v -x -e '_obj' -e '_chr' | sort -u || true"],
        capture_output=True, text=True)
    lines = [l for l in p.stdout.splitlines() if l]
    if "global_chr" not in lines:
        print("FATAL: fixed-list extraction from zone_bench_loader.cpp "
              f"broke (no global_chr): {lines!r}")
        sys.exit(3)
    return lines


FIXED = fixed_list()
assert "_obj" not in FIXED and "_chr" not in FIXED, \
    f"zone-derived entries must not be in the fixed list: {FIXED}"


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


def build_stubs(d, stale, still_stale_after):
    verify = os.path.join(d, "verify")
    builder = os.path.join(d, "builder")
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
        # The self-heal greps the STALE marker on stdout; the real
        # verify_eqe1 --require-v3 prints it there too (the trailing line
        # on either stream is just noise the script does not read).
        "    echo \"$pack: STALE (stub, still)\"; exit 1",
        "fi",
        "if in_set \"$base\" \"${stale[@]}\" && [ ! -f \"$pack.healed\" ]; then",
        "    echo \"$pack: STALE (stub)\"; exit 1",
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
        "touch \"$out/$z.eqe1\" \"$out/$z.eqe1.healed\"",
        "exit 0",
    ]))
    return verify, builder


def zone_packs(zones):
    """Every swept zone's mandatory packs: zone, zone_obj, zone_chr."""
    out = []
    for z in zones:
        out.extend([z, z + "_obj", z + "_chr"])
    return out


def seed(base, zones, zones_without_s3d, missing=()):
    """A source pack tree + EQ root: every mandatory name (the zone's
    three packs + the fixed list) gets a pack (and an .s3d), except the
    ones named in missing / zones_without_s3d, plus one NON-mandatory
    pack."""
    packs = os.path.join(base, "packs")
    eq = os.path.join(base, "eq")
    os.makedirs(packs)
    os.makedirs(eq)
    for n in sorted(set(zone_packs(zones)) | set(FIXED)):
        if n in missing:
            continue
        open(os.path.join(packs, n + ".eqe1"), "w").write("pack")
        if n in zones_without_s3d:
            continue
        open(os.path.join(eq, n + ".s3d"), "w").write("s3d")
    open(os.path.join(packs, "zz_not_required.eqe1"), "w").write("pack")
    open(os.path.join(eq, "zz_not_required.s3d"), "w").write("s3d")
    return packs, eq


def run_script(src_packs, src_eq, out_dir, zones, stale, still_stale_after=()):
    d = tempfile.mkdtemp(prefix="b929_selfheal_")
    verify, builder = build_stubs(d, stale, still_stale_after)
    plist = os.path.join(d, "zones.tsv")
    write_list(plist, zones)
    p = subprocess.run(
        ["bash", SELFHEAL, "--verify", verify, "--builder", builder,
         "--eq-path", src_eq, "--pack-dir", src_packs,
         "--out-dir", out_dir, "--list", plist,
         "--fixed-list", "\n".join(FIXED)],
        capture_output=True, text=True)
    shutil.rmtree(d, ignore_errors=True)
    return p


def main():
    if not os.path.isfile(SELFHEAL):
        print(f"SKIP (B929 pack self-heal): {SELFHEAL} not found")
        return 125

    failures = []
    base = tempfile.mkdtemp(prefix="b929_selfheal_")

    # 1. Clean run: every mandatory pack linked into the local tree, the
    #    non-mandatory pack NOT linked, the source untouched.
    src = os.path.join(base, "case1_src")
    packs, eq = seed(src, ["good"], [])
    out = os.path.join(base, "case1_out")
    p = run_script(packs, eq, out, ["good"], stale=set())
    if p.returncode != 0:
        failures.append(f"clean run should pass, got {p.returncode}: {p.stderr}")
    for n in zone_packs(["good"]) + FIXED:
        if not os.path.isfile(os.path.join(out, n + ".eqe1")):
            failures.append(f"clean run must link {n}.eqe1 into the tree")
    if os.path.isfile(os.path.join(out, "zz_not_required.eqe1")):
        failures.append("the non-mandatory pack must NOT be linked")
    if not os.path.isfile(os.path.join(packs, "good.eqe1")):
        failures.append("the source tree must be left untouched")
    if "0 rebuilt, 0 failed" not in p.stdout:
        failures.append(f"clean run should report 0 rebuilt: {p.stdout}")

    # 2. One stale mandatory zone: rebuilt into the local tree, re-verified,
    #    exit 0.
    src = os.path.join(base, "case2_src")
    packs, eq = seed(src, ["good", "bad"], [])
    out = os.path.join(base, "case2_out")
    p = run_script(packs, eq, out, ["good", "bad"], stale={"bad"})
    if p.returncode != 0:
        failures.append(f"healed run should pass, got {p.returncode}: {p.stderr}")
    if not os.path.isfile(os.path.join(out, "bad.eqe1.healed")):
        failures.append("the builder was not invoked for the stale pack")
    if "1 rebuilt, 0 failed" not in p.stdout:
        failures.append(f"healed run should report 1 rebuilt: {p.stdout}")

    # 3. A mandatory pack whose rebuild still fails verify: exit 1.
    src = os.path.join(base, "case3_src")
    packs, eq = seed(src, ["bad"], [])
    out = os.path.join(base, "case3_out")
    p = run_script(packs, eq, out, ["bad"], stale={"bad"},
                   still_stale_after={"bad"})
    if p.returncode != 1:
        failures.append(f"still-stale run should exit 1, got {p.returncode}")

    # 4. A mandatory STALE pack with no .s3d: exit 1.
    src = os.path.join(base, "case4_src")
    packs, eq = seed(src, ["nostale"], zones_without_s3d={"nostale"})
    out = os.path.join(base, "case4_out")
    p = run_script(packs, eq, out, ["nostale"], stale={"nostale"})
    if p.returncode != 1:
        failures.append(f"no-s3d run should exit 1, got {p.returncode}")

    # 5. A MANDATORY pack (a fixed-list entry) missing from the source:
    #    exit 1 -- the sweep would die on it.
    src = os.path.join(base, "case5_src")
    packs, eq = seed(src, ["good"], [], missing={"gequip"})
    out = os.path.join(base, "case5_out")
    p = run_script(packs, eq, out, ["good"], stale=set())
    if p.returncode != 1:
        failures.append(f"missing mandatory pack should exit 1, got {p.returncode}")

    # 6. A MANDATORY zone missing from the source: exit 1.
    src = os.path.join(base, "case6_src")
    packs, eq = seed(src, ["good", "gone"], [], missing={"gone"})
    out = os.path.join(base, "case6_out")
    p = run_script(packs, eq, out, ["good", "gone"], stale=set())
    if p.returncode != 1:
        failures.append(f"missing zone pack should exit 1, got {p.returncode}")

    # 7. A NON-mandatory pack missing from the source: skipped, exit 0,
    #    and it does not appear in the local tree.
    src = os.path.join(base, "case7_src")
    packs, eq = seed(src, ["good"], [])
    os.remove(os.path.join(packs, "zz_not_required.eqe1"))
    out = os.path.join(base, "case7_out")
    p = run_script(packs, eq, out, ["good"], stale=set())
    if p.returncode != 0:
        failures.append(
            f"missing non-mandatory pack should pass, got {p.returncode}: "
            f"{p.stderr}")
    if os.path.isfile(os.path.join(out, "zz_not_required.eqe1")):
        failures.append("the missing non-mandatory pack must not appear")

    # 8. A stale NON-mandatory pack (with a .s3d): left alone -- never
    #    rebuilt, never linked (the builder is for mandatory packs only).
    src = os.path.join(base, "case8_src")
    packs, eq = seed(src, ["good"], [])
    out = os.path.join(base, "case8_out")
    p = run_script(packs, eq, out, ["good"], stale={"zz_not_required"})
    if p.returncode != 0:
        failures.append(
            f"stale non-mandatory run should pass, got {p.returncode}: "
            f"{p.stderr}")
    if os.path.isfile(os.path.join(out, "zz_not_required.eqe1.healed")):
        failures.append(
            "the builder must not be invoked for a non-mandatory pack")
    if os.path.isfile(os.path.join(out, "zz_not_required.eqe1")):
        failures.append("a non-mandatory pack must not be linked")
    if "0 rebuilt, 0 failed" not in p.stdout:
        failures.append(f"non-mandatory run should report 0 rebuilt: {p.stdout}")

    # 9. The header row is skipped: a list whose only data row is clean.
    src = os.path.join(base, "case9_src")
    packs, eq = seed(src, ["good"], [])
    out = os.path.join(base, "case9_out")
    p = run_script(packs, eq, out, ["good"], stale={"zone"})
    if p.returncode != 0:
        failures.append(
            f"the list's header row ('zone') must be skipped, got "
            f"{p.returncode}: {p.stderr}")

    shutil.rmtree(base, ignore_errors=True)

    if failures:
        print("test_zone_census_pack_selfheal: FAIL")
        for f in failures:
            print("  " + f)
        return 1
    print("test_zone_census_pack_selfheal: PASS "
          f"({len(FIXED)} fixed-list packs re-read from zone_bench_loader.cpp; "
          "each zone's three packs + the fixed list linked / STALE rebuilt; "
          "still-stale, no-s3d, missing-mandatory and missing-zone fail "
          "loudly; non-mandatory packs never linked or rebuilt; header row "
          "skipped)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
