#!/usr/bin/env python3
"""B930: the verify_eqe1 stale-pack audit leg.

Criterion 2 of the card: tools/verify_eqe1 prints the pack's header version
and reports "stale: below v3" (exit non-zero) for a v1/v2 pack, so a host's
pack directory can be audited with one loop. This test writes two tiny
synthetic packs in python (header + one entry, exactly the layout
tools/eqe1_format.h and the test_eqe1_loader.cpp B910 idiom define) — a
structurally-clean v1 pack and a structurally-clean v3 pack — and runs the
BUILT verify_eqe1 over each with --require-v3:

  * v1: exit 1, a line carrying "v1" and "STALE"
  * v3: exit 0, a line carrying "OK (v3"

On origin/fable (RED) the tool prints no version and has no --require-v3,
so the v1 case exits 0 and both assertions fail.
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


def etc1_mip_chain(w, h, mips):
    total = 0
    for _ in range(mips):
        total += ((w + 3) // 4) * ((h + 3) // 4) * 8
        w = max(1, w // 2)
        h = max(1, h // 2)
    return total


def align16(x):
    return (x + 15) & ~15


def write_pack(path, version):
    """One 4x4 3-mip ETC1 texture named FULL_NAME.

    version 1: the pre-B910 builder's write — 31-char truncation, NUL-padded
    (formally valid, the silence that hid soldunga).
    version 3: full name in the name table between index and data.
    """
    sz = etc1_mip_chain(4, 4, 3)
    entry = 32 + 2 + 2 + 4 + 4 + 4  # name + w + h + 4x u8 + offset + size
    assert entry == 48
    if version == 1:
        index_end = 16 + 48
        off = align16(index_end)
        with open(path, "wb") as f:
            f.write(struct.pack("<IHHII", 0x31455145, 1, 1, 0, 0))
            f.write(FULL_NAME[:31].ljust(32, b"\0"))  # the truncation
            f.write(struct.pack("<HHBBBBII", 4, 4, 3, 0, 0, 0, off, sz))
            f.write(b"\x5a" * (off + sz - index_end))
    else:
        name_off = 16 + 56
        off = align16(name_off + len(FULL_NAME))
        with open(path, "wb") as f:
            f.write(struct.pack("<IHHII", 0x31455145, 3, 1, 0, 0))
            f.write(b"\0" * 32)  # name field zeroed — table-resident
            f.write(struct.pack("<HHBBBBII", 4, 4, 3, 0, 0, 0, off, sz))
            f.write(struct.pack("<II", name_off, len(FULL_NAME)))
            f.write(FULL_NAME)
            f.write(b"\x5a" * (off + sz - (name_off + len(FULL_NAME))))


def run(tool, args):
    p = subprocess.run([tool, *args], capture_output=True, text=True)
    return p.returncode, (p.stdout + p.stderr)


def main():
    if not os.path.isfile(TOOL):
        print(f"SKIP (B930 verify audit): verify_eqe1 not built at {TOOL} "
              "(cmake --build build --target verify_eqe1)")
        return 125

    tmp = tempfile.mkdtemp(prefix="b930_verify_audit_")
    v1 = os.path.join(tmp, "stale.eqe1")
    v3 = os.path.join(tmp, "fresh.eqe1")
    write_pack(v1, 1)
    write_pack(v3, 3)

    failures = []

    # 1. The version is printed on every line (with or without --require-v3).
    rc, out = run(TOOL, [v3])
    if rc != 0:
        failures.append(f"v3 pack should verify clean, got exit {rc}:\n{out}")
    if "OK (v3" not in out:
        failures.append(f"v3 line must print the version (RED on pre-B930):\n{out}")

    # 2. --require-v3: a structurally-clean v1 pack is STALE and fails.
    rc, out = run(TOOL, ["--require-v3", v1])
    if rc != 1:
        failures.append(f"v1 pack under --require-v3 must exit 1, got {rc} "
                        f"(RED on pre-B930):\n{out}")
    if "v1" not in out or "STALE" not in out:
        failures.append(f"v1 line must carry the version and STALE:\n{out}")

    # 3. --require-v3: the clean v3 pack still passes the audit.
    rc, out = run(TOOL, ["--require-v3", v3])
    if rc != 0 or "OK (v3" not in out:
        failures.append(f"v3 pack must pass --require-v3, got exit {rc}:\n{out}")

    if failures:
        print("test_eqe1_verify_stale_pack: FAIL")
        for f in failures:
            print("  " + f)
        return 1
    print("test_eqe1_verify_stale_pack: PASS "
          "(v1 prints v1 + STALE + exit 1; v3 prints OK (v3 + exit 0)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
