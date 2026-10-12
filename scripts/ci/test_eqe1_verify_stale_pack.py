#!/usr/bin/env python3
"""B930 + B1017: the verify_eqe1 stale-pack audit leg.

B930: tools/verify_eqe1 prints the pack's header version on every line.
B1017 (#338): --require-v3 flags ONLY the truncation candidates -- a v1/v2
pack holding a 31-char entry name (the inline field's max; the signature of
a pre-B910 builder that silently strncpy'd a longer original). A
structurally-clean v1/v2 pack with no 31-char name is LEGITIMATE (every name
fits the field; the builder keeps it v1/v2 by design -- willeq.eqe1 is one)
and PASSES. On origin/fable the audit flagged EVERY v1/v2 pack (1506/1506 on
the NFS tree, including the legitimate v2 willeq.eqe1), so this test is RED
there on the legitimate-pack case.

This writes four tiny synthetic packs in python (header + one entry, exactly
the layout tools/eqe1_format.h and the test_eqe1_loader.cpp B910 idiom
define) and runs the BUILT verify_eqe1:

  * v1, clean name (10 chars):            OK (v1)   exit 0
  * v1, 31-char name (soldunga class):    STALE, the name quoted, exit 1
  * v2, clean name (the willeq.eqe1 class): OK (v2) exit 0
  * v3, 33-char name in the name table:   OK (v3)   exit 0
  * --require-v3 over all four:           the clean v1/v2 pass, the
                                          31-char v1 fails, v3 passes.

On origin/fable (RED) the audit flags the clean v1 and v2 packs too (exit 1
for them), so cases 1/3/6 fail.
"""
import os
import struct
import subprocess
import sys
import tempfile

REPO = os.environ.get("WILLEQ_SOURCE_DIR",
                      os.path.dirname(os.path.dirname(os.path.dirname(
                          os.path.abspath(__file__)))))
# The tool lives in the build tree under test (eqe1.cmake passes both).
# CI (B1012) builds per-ref trees at build/ref/<ref-slug>, so the default
# <repo>/build/bin would never find it there — WILLEQ_BUILD_DIR (set by the
# test's ENVIRONMENT to CMAKE_BINARY_DIR) is the tree ctest runs against.
_BUILD_DIR = os.environ.get("WILLEQ_BUILD_DIR")
if _BUILD_DIR:
    TOOL = os.path.join(_BUILD_DIR, "bin", "verify_eqe1")
else:
    # Local runs configured by hand: the recipe tree <repo>/build.
    TOOL = os.path.join(REPO, os.path.join("build", "bin", "verify_eqe1"))

FULL_NAME = b"gnome_stronghold_floor_transition"  # 33 chars, the soldunga class
CLEAN_NAME = b"stone_floor"                        # 11 chars, fits the field
SHORT31 = b"a" * 31                               # 31 chars, the truncation class
assert len(FULL_NAME) == 33 and len(CLEAN_NAME) == 11 and len(SHORT31) == 31


def etc1_mip_chain(w, h, mips):
    total = 0
    for _ in range(mips):
        total += ((w + 3) // 4) * ((h + 3) // 4) * 8
        w = max(1, w // 2)
        h = max(1, h // 2)
    return total


def align16(x):
    return (x + 15) & ~15


def write_pack(path, version, name):
    """One 4x4 3-mip ETC1 texture with entry name `name` (bytes).

    version 1: the pre-B910 builder's write — the name is NUL-padded into
    the 32-byte inline field (a 31-char name is formally valid: the
    silence that hid soldunga).
    version 2: same layout, header version 2.
    version 3: full name in the name table between index and data.
    """
    assert len(name) <= 31 or version == 3, \
        f"a {len(name)}-char name only fits a v3 name table"
    sz = etc1_mip_chain(4, 4, 3)
    entry = 32 + 2 + 2 + 4 + 4 + 4  # name + w + h + 4x u8 + offset + size
    assert entry == 48
    if version in (1, 2):
        index_end = 16 + 48
        off = align16(index_end)
        with open(path, "wb") as f:
            f.write(struct.pack("<IHHII", 0x31455145, version, 1, 0, 0))
            f.write(name[:31].ljust(32, b"\0"))  # the (silent) truncation, if any
            f.write(struct.pack("<HHBBBBII", 4, 4, 3, 0, 0, 0, off, sz))
            f.write(b"\x5a" * (off + sz - index_end))
    else:
        name_off = 16 + 56
        off = align16(name_off + len(name))
        with open(path, "wb") as f:
            f.write(struct.pack("<IHHII", 0x31455145, 3, 1, 0, 0))
            f.write(b"\0" * 32)  # name field zeroed — table-resident
            f.write(struct.pack("<HHBBBBII", 4, 4, 3, 0, 0, 0, off, sz))
            f.write(struct.pack("<II", name_off, len(name)))
            f.write(name)
            f.write(b"\x5a" * (off + sz - (name_off + len(name))))


def run(tool, args):
    p = subprocess.run([tool, *args], capture_output=True, text=True)
    return p.returncode, (p.stdout + p.stderr)


def main():
    if not os.path.isfile(TOOL):
        print(f"SKIP (B930 verify audit): verify_eqe1 not built at {TOOL} "
              "(cmake --build build --target verify_eqe1)")
        return 125

    tmp = tempfile.mkdtemp(prefix="b1017_verify_audit_")
    v1_clean = os.path.join(tmp, "clean_v1.eqe1")
    v1_stale = os.path.join(tmp, "stale_v1.eqe1")
    v2_clean = os.path.join(tmp, "clean_v2.eqe1")
    v3 = os.path.join(tmp, "fresh_v3.eqe1")
    write_pack(v1_clean, 1, CLEAN_NAME)
    write_pack(v1_stale, 1, SHORT31)
    write_pack(v2_clean, 2, CLEAN_NAME)
    write_pack(v3, 3, FULL_NAME)

    failures = []

    # 1. The version is printed on every line (with or without --require-v3).
    rc, out = run(TOOL, [v3])
    if rc != 0:
        failures.append(f"v3 pack should verify clean, got exit {rc}:\n{out}")
    if "OK (v3" not in out:
        failures.append(f"v3 line must print the version:\n{out}")

    # 2. B1017: a structurally-clean v1 pack (no 31-char name) is LEGITIMATE:
    #    --require-v3 must PASS it. RED on origin/fable (flagged STALE).
    rc, out = run(TOOL, ["--require-v3", v1_clean])
    if rc != 0:
        failures.append(
            f"legitimate v1 pack (no 31-char name) must pass --require-v3 "
            f"(B1017: the audit cannot tell stale from fine by VERSION "
            f"alone), got exit {rc}:\n{out}")
    if "OK (v1" not in out:
        failures.append(f"clean v1 line must print OK (v1...):\n{out}")

    # 3. B1017: a v1 pack holding a 31-char name is the truncation
    #    candidate: STALE, the name printed by name, exit 1.
    rc, out = run(TOOL, ["--require-v3", v1_stale])
    if rc != 1:
        failures.append(f"v1 pack with a 31-char name must exit 1, got {rc}:\n{out}")
    if "STALE" not in out or "v1" not in out:
        failures.append(f"31-char v1 line must carry the version and STALE:\n{out}")
    if "'aaaaa" not in out and "'a' * 31" not in out:
        # quote the candidate name by its 5-char prefix (it is all a's)
        if f"'{'a' * 31}'" not in out:
            failures.append(f"31-char v1 line must name the candidate:\n{out}")

    # 4. B1017: a structurally-clean v2 pack (the willeq.eqe1 class — the
    #    client's OWN pack, deliberately v2) is LEGITIMATE too.
    rc, out = run(TOOL, ["--require-v3", v2_clean])
    if rc != 0:
        failures.append(
            f"legitimate v2 pack (no 31-char name) must pass --require-v3, "
            f"got exit {rc}:\n{out}")
    if "OK (v2" not in out:
        failures.append(f"clean v2 line must print OK (v2...):\n{out}")

    # 5. --require-v3: the clean v3 pack still passes the audit.
    rc, out = run(TOOL, ["--require-v3", v3])
    if rc != 0 or "OK (v3" not in out:
        failures.append(f"v3 pack must pass --require-v3, got exit {rc}:\n{out}")

    # 6. Without --require-v3 the 31-char v1 pack is still structurally
    #    clean: the audit is an OPT-IN flag, not a structural verdict.
    rc, out = run(TOOL, [v1_stale])
    if rc != 0 or "OK (v1" not in out:
        failures.append(
            f"a 31-char v1 pack with no --require-v3 must verify structurally "
            f"clean (exit 0, OK (v1...)), got exit {rc}:\n{out}")

    if failures:
        print("test_eqe1_verify_stale_pack: FAIL")
        for f in failures:
            print("  " + f)
        return 1
    print("test_eqe1_verify_stale_pack: PASS "
          "(clean v1/v2 pass; 31-char v1 is STALE by name; v3 passes; "
          "version printed on every line)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
