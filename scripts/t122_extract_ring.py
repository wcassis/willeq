#!/usr/bin/env python3
"""
scripts/t122_extract_ring.py — offline post-mortem extraction of the T122
lock-free texture-lifecycle probe ring
(docker/irrlicht-drm/t122_texprobe.h) from a core dump.

Usage:
    python3 scripts/t122_extract_ring.py <binary> <core> [--out ring.tsv]

Requires: gdb, and a probe build — willeq compiled with either
`scripts/build-gles2-x86.sh --t122-texprobe` (x86_64) or
`scripts/build-arm-noble.sh --t122-texprobe` (armhf, T137) — both define
WILLEQ_T122_TEXPROBE and keep -g so the ring symbols and their addresses
are resolvable from the ELF symbol table / DWARF. Pointer width (32 vs. 64)
is detected automatically from the binary's own ELF header (see
detect_pointer_width() below) — there is no width flag to pass or get
wrong. Parsing an armhf core requires a gdb built for (or multiarch-capable
of) the arm-linux-gnueabihf target; T137 verified the 32-bit parsing logic
against a synthesised ring only (scripts/tests/test_t122_extract_ring.py) —
an end-to-end armhf core has not been parsed by this script, because that
needs a real Pi capture, out of scope for T137 (see its Review).

This performs NO live process inspection — it opens the binary and the core
file on disk via `gdb -batch -core-file`, which is how the plan's "resolved
entirely offline" requirement is satisfied: no runtime writes to disk, no
signal-handler dumping, nothing but a post-mortem read of memory gdb
recovers from the core's PT_LOAD segments (gdb handles PIE load-bias
computation itself; this script does no manual ELF/vaddr arithmetic).

T137: this originally targeted the x86_64 probe build only ("T122/T126 are
x86-only, no Pi run" -- see the stale comment this replaced). T137 threads
WILLEQ_T122_TEXPROBE through the ARM cross-compile (scripts/build-arm-noble.sh
--t122-texprobe), so this script now also parses a 32-bit (armhf) ring. The
pointer width is DETECTED FROM THE BINARY'S OWN ELF HEADER (EI_CLASS byte, a
fact the file already states) rather than taken as a command-line flag --
per the plan's design point, a flag the caller can get wrong is a worse
interface than a fact already present in the input, and a mis-parsed ring
produces plausible garbage, the worst failure mode for a diagnostic.

Output: a TSV with one row per recovered ring entry, in `seq` order
(monotonic per-process fetch_add value — NOT ring-slot order, which wraps).
Each row is (seq, kind, ptr, tid, value). A companion header interpretation
lives in refactor/T122_texture_lifecycle_probe.md's Review section — this
script only recovers raw data, it does not itself decide UAF vs. type
confusion vs. exoneration.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

# Must match docker/irrlicht-drm/t122_texprobe.h's Entry layout exactly:
#   const void* ptr;             // 8 bytes (x86_64) / 4 bytes (armhf)
#   const void* retaddr[8];      // T126: TEXPARAMS-only call chain,
#                                // zero-filled for CREATE/DESTROY/MATERIAL
#   uint32_t seq;
#   uint32_t tid;
#   uint32_t value;
#   uint8_t kind;
#   uint8_t pad[3];
# Two widths, one per pointer size. Both formats and sizes are also asserted
# in the header itself (t122_texprobe.h's static_asserts, T137) so a future
# Entry change breaks loudly on both the C++ and Python sides rather than
# silently shifting every field.
#
# 64-bit (x86_64 probe build): 8-byte ptr + 8x8-byte retaddr + 12 bytes of
# uint32_t fields + 1 kind byte + 3 pad = 88.
ENTRY_FMT_64 = "<Q8QIIIB3x"
ENTRY_SIZE_64 = struct.calcsize(ENTRY_FMT_64)
assert ENTRY_SIZE_64 == 88, ENTRY_SIZE_64

# 32-bit (armhf probe build, T137): 4-byte ptr + 8x4-byte retaddr + 12 bytes
# of uint32_t fields + 1 kind byte + 3 pad = 4 + 32 + 12 + 4 = 52. Derived by
# hand against t122_texprobe.h's Entry, then independently verified via
# struct.calcsize("<I8IIIIB3x") == 52 before this was trusted -- see
# refactor/T137_arm_probe_infra.md's Review for the worked derivation and a
# synthesised-ring round-trip test.
ENTRY_FMT_32 = "<I8IIIIB3x"
ENTRY_SIZE_32 = struct.calcsize(ENTRY_FMT_32)
assert ENTRY_SIZE_32 == 52, ENTRY_SIZE_32

RING_CAPACITY = 1 << 18  # must match t122_texprobe.h's kRingCapacity

KIND_NAMES = {1: "CREATE", 2: "DESTROY", 3: "TEXPARAMS", 4: "MATERIAL"}

RING_SYMBOL = "irr::video::t122::g_t122Ring"
IDX_SYMBOL = "irr::video::t122::g_t122RingIndex"

# ELF e_ident[EI_CLASS] values (elf.h) -- the byte at offset 4 of any ELF
# file, always present regardless of PIE/non-PIE, stripped/unstripped,
# architecture. This is what "detect the width from the ELF" means here: no
# gdb needed for this part, no --32/--64 flag for the caller to get wrong.
ELFCLASS32 = 1
ELFCLASS64 = 2


class ElfWidthError(RuntimeError):
    pass


def detect_pointer_width(binary_path):
    """Return 32 or 64 by reading the target's own ELF class byte.

    Deliberately does not shell out to gdb/readelf for this -- the ELF
    header is 16 fixed bytes at the start of the file, trivial to read
    directly, and doing so keeps width detection independent of (and thus a
    cross-check against) whatever gdb later resolves the ring symbols
    against.
    """
    with open(binary_path, "rb") as f:
        ident = f.read(16)
    if len(ident) < 5 or ident[:4] != b"\x7fELF":
        raise ElfWidthError(
            f"{binary_path} does not have an ELF magic number (got {ident[:4]!r}) "
            "-- is this really the probe-build binary?"
        )
    ei_class = ident[4]
    if ei_class == ELFCLASS64:
        return 64
    if ei_class == ELFCLASS32:
        return 32
    raise ElfWidthError(
        f"{binary_path}: unrecognized EI_CLASS byte {ei_class!r} "
        "(expected 1=ELFCLASS32 or 2=ELFCLASS64)"
    )


def entry_format_for_width(width):
    if width == 64:
        return ENTRY_FMT_64, ENTRY_SIZE_64
    if width == 32:
        return ENTRY_FMT_32, ENTRY_SIZE_32
    raise ElfWidthError(f"unsupported pointer width {width} (expected 32 or 64)")


def gdb_batch(binary, core, commands):
    args = ["gdb", "-q", "-batch", "-nx", "-ex", f"file {binary}", "-ex", f"core-file {core}"]
    for c in commands:
        args += ["-ex", c]
    proc = subprocess.run(args, capture_output=True, text=True, timeout=180)
    return proc.stdout, proc.stderr


def get_symbol_addr(binary, core, symbol):
    # Try unquoted first (works for plain namespaced globals); fall back to
    # single-quoted form (needed if gdb's expression parser trips on the name
    # for any reason, e.g. an unexpected mangling).
    for expr in (f"&{symbol}", f"&'{symbol}'"):
        stdout, stderr = gdb_batch(binary, core, [f'printf "ADDR=%p\\n", {expr}'])
        for line in stdout.splitlines():
            if line.startswith("ADDR="):
                return int(line.split("=", 1)[1], 16)
    raise RuntimeError(
        f"could not resolve symbol {symbol} in {binary} against {core}\n"
        f"stdout={stdout!r}\nstderr={stderr!r}\n"
        "Was the binary built with --t122-texprobe "
        "(scripts/build-gles2-x86.sh or scripts/build-arm-noble.sh)?"
    )


def dump_memory(binary, core, addr, size, out_path):
    stdout, stderr = gdb_batch(
        binary, core, [f"dump binary memory {out_path} {hex(addr)} {hex(addr + size)}"]
    )
    if not os.path.exists(out_path) or os.path.getsize(out_path) != size:
        raise RuntimeError(
            f"gdb memory dump failed or wrong size (expected {size}): "
            f"stdout={stdout!r} stderr={stderr!r}"
        )


def parse_ring_bytes(raw, entry_fmt, entry_size, ring_capacity):
    """Parse raw ring bytes into a seq-sorted list of entries.

    Pulled out of main() (T137) so it can be exercised directly against a
    synthesised buffer of known values in
    scripts/tests/test_t122_extract_ring.py, independent of gdb/a binary/a
    core file -- the width-dependent struct format is the only thing under
    test here, not the offline-extraction plumbing around it.

    Returns a list of (seq, kind, ptr, tid, value, retaddr_tuple), sorted by
    seq (NOT ring-slot order, which wraps).
    """
    entries = []
    for i in range(ring_capacity):
        chunk = raw[i * entry_size : (i + 1) * entry_size]
        unpacked = struct.unpack(entry_fmt, chunk)
        ptr, ra0, ra1, ra2, ra3, ra4, ra5, ra6, ra7, seq, tid, value, kind = unpacked
        if seq == 0 and ptr == 0 and value == 0 and kind == 0 and tid == 0:
            continue  # slot never written (ring hasn't wrapped that far yet)
        entries.append((seq, kind, ptr, tid, value, (ra0, ra1, ra2, ra3, ra4, ra5, ra6, ra7)))
    entries.sort(key=lambda e: e[0])
    return entries


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("binary", help="path to the probe-build willeq binary")
    ap.add_argument("core", help="path to the core dump")
    ap.add_argument("--out", default=None, help="TSV output path (default: stdout)")
    args = ap.parse_args()

    pointer_width = detect_pointer_width(args.binary)
    entry_fmt, entry_size = entry_format_for_width(pointer_width)
    print(f"# detected {pointer_width}-bit ELF ({args.binary}) -> Entry size {entry_size} bytes",
          file=sys.stderr)

    ring_addr = get_symbol_addr(args.binary, args.core, RING_SYMBOL)
    idx_addr = get_symbol_addr(args.binary, args.core, IDX_SYMBOL)

    with tempfile.TemporaryDirectory() as td:
        idx_path = os.path.join(td, "idx.bin")
        dump_memory(args.binary, args.core, idx_addr, 4, idx_path)
        with open(idx_path, "rb") as f:
            (total_written,) = struct.unpack("<I", f.read(4))

        ring_path = os.path.join(td, "ring.bin")
        dump_memory(args.binary, args.core, ring_addr, entry_size * RING_CAPACITY, ring_path)
        with open(ring_path, "rb") as f:
            raw = f.read()

    entries = parse_ring_bytes(raw, entry_fmt, entry_size, RING_CAPACITY)

    out = open(args.out, "w") if args.out else sys.stdout
    try:
        out.write(f"# pointer width = {pointer_width}-bit, Entry size = {entry_size} bytes\n")
        out.write(f"# g_t122RingIndex (total fetch_add calls, pre-wrap) = {total_written}\n")
        out.write(f"# ring capacity = {RING_CAPACITY}, wrapped = {total_written > RING_CAPACITY}\n")
        out.write(f"# entries recovered from core = {len(entries)}\n")
        out.write("seq\tkind\tptr\ttid\tvalue\t" + "\t".join(f"retaddr{i}" for i in range(8)) + "\n")
        for seq, kind, ptr, tid, value, retaddr in entries:
            kind_name = KIND_NAMES.get(kind, f"UNKNOWN({kind})")
            ra_str = "\t".join(f"0x{ra:x}" for ra in retaddr)
            out.write(f"{seq}\t{kind_name}\t0x{ptr:x}\t0x{tid:x}\t0x{value:x}\t{ra_str}\n")
    finally:
        if args.out:
            out.close()


if __name__ == "__main__":
    main()
