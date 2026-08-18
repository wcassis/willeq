#!/usr/bin/env python3
"""
scripts/tests/test_t122_extract_ring.py — T137: standalone tests for
scripts/t122_extract_ring.py's width-detection and ring-parsing logic.

Deliberately NOT wired into the ctest suite: this project's ctest suite
builds and runs C++ binaries (see EXECUTOR_PROTOCOL.md's verification bar);
adding a Python test runner to that CMake-driven suite for one offline
diagnostic script would be disproportionate to what it's testing. This
mirrors t122_extract_ring.py's own precedent (an invocable-from-the-Review
standalone script, not a ctest target). Run directly:

    python3 scripts/tests/test_t122_extract_ring.py

Exits 0 and prints "ALL TESTS PASSED" on success, non-zero with an assertion
traceback on failure -- suitable for a CI step or manual invocation alike.

Covers, all offline (no gdb, no binary-under-test's contents needed except
for the two "real ELF" spot checks, which are skipped gracefully if the
referenced binaries are not present):

1. detect_pointer_width() against synthesised ELF headers (crafted magic +
   EI_CLASS bytes) for both widths, and its error paths (bad magic, unknown
   class byte).
2. detect_pointer_width() against whatever real binaries this checkout
   happens to have on hand (build-gles2/bin/willeq for 64-bit,
   build-arm-noble/bin/willeq for 32-bit) -- skipped, not failed, if absent,
   since a fresh checkout may not have built either yet.
3. entry_format_for_width() returns the right (format, size) pair for both
   widths and rejects anything else.
4. parse_ring_bytes() round-tripped against a SYNTHESISED ring (hand-packed
   bytes with known values) for BOTH widths -- the core "does the 32-bit
   layout actually parse" claim, and a regression guard that refactoring the
   64-bit path (T137 pulled it out of main() into this function) did not
   change its behavior.
"""
import importlib.util
import os
import struct
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
EXTRACTOR_PATH = os.path.join(PROJECT_DIR, "t122_extract_ring.py")

_spec = importlib.util.spec_from_file_location("t122_extract_ring", EXTRACTOR_PATH)
extractor = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(extractor)

FAILURES = []


def check(label, cond):
    status = "PASS" if cond else "FAIL"
    print(f"[{status}] {label}")
    if not cond:
        FAILURES.append(label)


def skip(label, reason):
    print(f"[SKIP] {label} ({reason})")


# ---------------------------------------------------------------------------
# 1. detect_pointer_width() against synthesised ELF headers
# ---------------------------------------------------------------------------
def make_elf_header(ei_class):
    # e_ident: magic(4) + EI_CLASS(1) + EI_DATA(1) + EI_VERSION(1) + pad(9)
    return b"\x7fELF" + bytes([ei_class, 1, 1]) + b"\x00" * 9


def test_synthesised_elf_headers(tmpdir):
    p64 = os.path.join(tmpdir, "fake64")
    with open(p64, "wb") as f:
        f.write(make_elf_header(extractor.ELFCLASS64))
    check("detect_pointer_width: synthesised ELFCLASS64 header -> 64",
          extractor.detect_pointer_width(p64) == 64)

    p32 = os.path.join(tmpdir, "fake32")
    with open(p32, "wb") as f:
        f.write(make_elf_header(extractor.ELFCLASS32))
    check("detect_pointer_width: synthesised ELFCLASS32 header -> 32",
          extractor.detect_pointer_width(p32) == 32)

    pbad_magic = os.path.join(tmpdir, "fakebadmagic")
    with open(pbad_magic, "wb") as f:
        f.write(b"NOTELF\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00")
    try:
        extractor.detect_pointer_width(pbad_magic)
        check("detect_pointer_width: bad magic raises ElfWidthError", False)
    except extractor.ElfWidthError:
        check("detect_pointer_width: bad magic raises ElfWidthError", True)

    pbad_class = os.path.join(tmpdir, "fakebadclass")
    with open(pbad_class, "wb") as f:
        f.write(make_elf_header(99))
    try:
        extractor.detect_pointer_width(pbad_class)
        check("detect_pointer_width: unknown EI_CLASS raises ElfWidthError", False)
    except extractor.ElfWidthError:
        check("detect_pointer_width: unknown EI_CLASS raises ElfWidthError", True)


# ---------------------------------------------------------------------------
# 2. detect_pointer_width() against real binaries, if present (best-effort)
# ---------------------------------------------------------------------------
def test_real_binaries():
    x86_bin = os.path.join(PROJECT_DIR, "..", "build-gles2", "bin", "willeq")
    x86_bin = os.path.normpath(x86_bin)
    if os.path.isfile(x86_bin):
        w = extractor.detect_pointer_width(x86_bin)
        check(f"detect_pointer_width: real x86 build ({x86_bin}) -> 64", w == 64)
    else:
        skip("detect_pointer_width: real x86 build", f"{x86_bin} not present")

    arm_bin = os.path.join(PROJECT_DIR, "..", "build-arm-noble", "bin", "willeq")
    arm_bin = os.path.normpath(arm_bin)
    if os.path.isfile(arm_bin):
        w = extractor.detect_pointer_width(arm_bin)
        check(f"detect_pointer_width: real ARM cross-build ({arm_bin}) -> 32", w == 32)
    else:
        skip("detect_pointer_width: real ARM cross-build", f"{arm_bin} not present")


# ---------------------------------------------------------------------------
# 3. entry_format_for_width()
# ---------------------------------------------------------------------------
def test_entry_format_for_width():
    fmt64, size64 = extractor.entry_format_for_width(64)
    check("entry_format_for_width(64) == (ENTRY_FMT_64, 88)",
          fmt64 == extractor.ENTRY_FMT_64 and size64 == 88)

    fmt32, size32 = extractor.entry_format_for_width(32)
    check("entry_format_for_width(32) == (ENTRY_FMT_32, 52)",
          fmt32 == extractor.ENTRY_FMT_32 and size32 == 52)

    try:
        extractor.entry_format_for_width(16)
        check("entry_format_for_width(16) raises ElfWidthError", False)
    except extractor.ElfWidthError:
        check("entry_format_for_width(16) raises ElfWidthError", True)


# ---------------------------------------------------------------------------
# 4. parse_ring_bytes() round-trip against a synthesised ring, both widths
# ---------------------------------------------------------------------------
def build_synthetic_ring(entry_fmt, entry_size, ring_capacity, known_entries):
    """known_entries: dict slot_index -> (ptr, ra0..ra7, seq, tid, value, kind).
    Unlisted slots are left all-zero (never-written, must be skipped by the
    parser)."""
    raw = bytearray(entry_size * ring_capacity)
    for slot, fields in known_entries.items():
        packed = struct.pack(entry_fmt, *fields)
        assert len(packed) == entry_size
        raw[slot * entry_size:(slot + 1) * entry_size] = packed
    return bytes(raw)


def test_parse_ring_bytes_64bit():
    ring_capacity = 8
    known = {
        # slot 0: CREATE, seq=5 (deliberately out of slot order, to prove
        # the parser sorts by seq, not by slot index)
        0: (0xdeadbeef1234, *(0,) * 8, 5, 0xaaaa, 0xeb, 1),
        # slot 3: TEXPARAMS with a real (non-zero) retaddr chain, seq=1
        3: (0xdeadbeef1234, 0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000, 0x7000, 0x8000,
            1, 0xbbbb, 0x2601, 3),
        # slot 6: DESTROY, seq=9
        6: (0xdeadbeef1234, *(0,) * 8, 9, 0xcccc, 0xeb, 2),
        # slots 1,2,4,5,7 left all-zero -- must be skipped
    }
    raw = build_synthetic_ring(extractor.ENTRY_FMT_64, extractor.ENTRY_SIZE_64, ring_capacity, known)
    entries = extractor.parse_ring_bytes(raw, extractor.ENTRY_FMT_64, extractor.ENTRY_SIZE_64, ring_capacity)

    check("64-bit: recovers exactly 3 non-zero entries (5 all-zero slots skipped)",
          len(entries) == 3)
    check("64-bit: entries are seq-sorted (1, 5, 9), not slot-order (0, 3, 6)",
          [e[0] for e in entries] == [1, 5, 9])

    seq1 = entries[0]
    check("64-bit: seq=1 entry is TEXPARAMS (kind=3)", seq1[1] == 3)
    check("64-bit: seq=1 entry ptr round-trips exactly", seq1[2] == 0xdeadbeef1234)
    check("64-bit: seq=1 entry tid round-trips exactly", seq1[3] == 0xbbbb)
    check("64-bit: seq=1 entry value round-trips exactly (0x2601 GL_LINEAR)", seq1[4] == 0x2601)
    check("64-bit: seq=1 entry retaddr chain round-trips exactly",
          seq1[5] == (0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000, 0x7000, 0x8000))

    seq5 = entries[1]
    check("64-bit: seq=5 entry is CREATE (kind=1) with GL name 0xeb", seq5[1] == 1 and seq5[4] == 0xeb)

    seq9 = entries[2]
    check("64-bit: seq=9 entry is DESTROY (kind=2)", seq9[1] == 2)


def test_parse_ring_bytes_32bit():
    ring_capacity = 8
    known = {
        0: (0x12345678, *(0,) * 8, 5, 0xaaaa, 0xeb, 1),
        3: (0x12345678, 0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000, 0x7000, 0x8000,
            1, 0xbbbb, 0x2601, 3),
        6: (0x12345678, *(0,) * 8, 9, 0xcccc, 0xeb, 2),
    }
    raw = build_synthetic_ring(extractor.ENTRY_FMT_32, extractor.ENTRY_SIZE_32, ring_capacity, known)
    check("32-bit: synthesised ring byte length matches capacity*52",
          len(raw) == ring_capacity * 52)

    entries = extractor.parse_ring_bytes(raw, extractor.ENTRY_FMT_32, extractor.ENTRY_SIZE_32, ring_capacity)

    check("32-bit: recovers exactly 3 non-zero entries (5 all-zero slots skipped)",
          len(entries) == 3)
    check("32-bit: entries are seq-sorted (1, 5, 9), not slot-order (0, 3, 6)",
          [e[0] for e in entries] == [1, 5, 9])

    seq1 = entries[0]
    check("32-bit: seq=1 entry is TEXPARAMS (kind=3)", seq1[1] == 3)
    check("32-bit: seq=1 entry ptr round-trips exactly (32-bit pointer value)", seq1[2] == 0x12345678)
    check("32-bit: seq=1 entry tid round-trips exactly", seq1[3] == 0xbbbb)
    check("32-bit: seq=1 entry value round-trips exactly (0x2601 GL_LINEAR)", seq1[4] == 0x2601)
    check("32-bit: seq=1 entry retaddr chain round-trips exactly (4-byte-pointer retaddrs)",
          seq1[5] == (0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000, 0x7000, 0x8000))

    seq5 = entries[1]
    check("32-bit: seq=5 entry is CREATE (kind=1) with GL name 0xeb", seq5[1] == 1 and seq5[4] == 0xeb)

    seq9 = entries[2]
    check("32-bit: seq=9 entry is DESTROY (kind=2)", seq9[1] == 2)


def main():
    import tempfile
    print("=== t122_extract_ring.py: width detection + ring parsing tests (T137) ===\n")

    check("ENTRY_SIZE_64 == 88 (module-level assert already enforced this on import)",
          extractor.ENTRY_SIZE_64 == 88)
    check("ENTRY_SIZE_32 == 52 (module-level assert already enforced this on import)",
          extractor.ENTRY_SIZE_32 == 52)

    with tempfile.TemporaryDirectory() as td:
        test_synthesised_elf_headers(td)
    test_real_binaries()
    test_entry_format_for_width()
    test_parse_ring_bytes_64bit()
    test_parse_ring_bytes_32bit()

    print()
    if FAILURES:
        print(f"=== {len(FAILURES)} FAILURE(S) ===")
        for f in FAILURES:
            print(f"  - {f}")
        sys.exit(1)
    print("ALL TESTS PASSED")
    sys.exit(0)


if __name__ == "__main__":
    main()
