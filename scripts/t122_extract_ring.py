#!/usr/bin/env python3
"""
scripts/t122_extract_ring.py — offline post-mortem extraction of the T122
lock-free texture-lifecycle probe ring
(docker/irrlicht-drm/t122_texprobe.h) from a core dump.

Usage:
    python3 scripts/t122_extract_ring.py <binary> <core> [--out ring.tsv]

Requires: gdb, and a probe build — willeq compiled with
`scripts/build-gles2-x86.sh --t122-texprobe` (which defines
WILLEQ_T122_TEXPROBE and keeps -g so the ring symbols and their addresses
are resolvable from the ELF symbol table / DWARF).

This performs NO live process inspection — it opens the binary and the core
file on disk via `gdb -batch -core-file`, which is how the plan's "resolved
entirely offline" requirement is satisfied: no runtime writes to disk, no
signal-handler dumping, nothing but a post-mortem read of memory gdb
recovers from the core's PT_LOAD segments (gdb handles PIE load-bias
computation itself; this script does no manual ELF/vaddr arithmetic).

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
#   const void* ptr;             // 8 bytes on x86_64
#   const void* retaddr[8];      // T126: 64 bytes -- TEXPARAMS-only call chain,
#                                // zero-filled for CREATE/DESTROY/MATERIAL
#   uint32_t seq;
#   uint32_t tid;
#   uint32_t value;
#   uint8_t kind;
#   uint8_t pad[3];
# Total: 88 bytes on x86_64 (this script targets the x86 probe build only —
# T122/T126 are x86-only, no Pi run).
ENTRY_FMT_64 = "<Q8QIIIB3x"
ENTRY_SIZE_64 = struct.calcsize(ENTRY_FMT_64)
assert ENTRY_SIZE_64 == 88, ENTRY_SIZE_64

RING_CAPACITY = 1 << 18  # must match t122_texprobe.h's kRingCapacity

KIND_NAMES = {1: "CREATE", 2: "DESTROY", 3: "TEXPARAMS", 4: "MATERIAL"}

RING_SYMBOL = "irr::video::t122::g_t122Ring"
IDX_SYMBOL = "irr::video::t122::g_t122RingIndex"


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
        "Was the binary built with scripts/build-gles2-x86.sh --t122-texprobe?"
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


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("binary", help="path to the probe-build willeq binary")
    ap.add_argument("core", help="path to the core dump")
    ap.add_argument("--out", default=None, help="TSV output path (default: stdout)")
    args = ap.parse_args()

    ring_addr = get_symbol_addr(args.binary, args.core, RING_SYMBOL)
    idx_addr = get_symbol_addr(args.binary, args.core, IDX_SYMBOL)

    with tempfile.TemporaryDirectory() as td:
        idx_path = os.path.join(td, "idx.bin")
        dump_memory(args.binary, args.core, idx_addr, 4, idx_path)
        with open(idx_path, "rb") as f:
            (total_written,) = struct.unpack("<I", f.read(4))

        ring_path = os.path.join(td, "ring.bin")
        dump_memory(args.binary, args.core, ring_addr, ENTRY_SIZE_64 * RING_CAPACITY, ring_path)
        with open(ring_path, "rb") as f:
            raw = f.read()

    entries = []
    for i in range(RING_CAPACITY):
        chunk = raw[i * ENTRY_SIZE_64 : (i + 1) * ENTRY_SIZE_64]
        unpacked = struct.unpack(ENTRY_FMT_64, chunk)
        ptr, ra0, ra1, ra2, ra3, ra4, ra5, ra6, ra7, seq, tid, value, kind = unpacked
        if seq == 0 and ptr == 0 and value == 0 and kind == 0 and tid == 0:
            continue  # slot never written (ring hasn't wrapped that far yet)
        entries.append((seq, kind, ptr, tid, value, (ra0, ra1, ra2, ra3, ra4, ra5, ra6, ra7)))

    entries.sort(key=lambda e: e[0])

    out = open(args.out, "w") if args.out else sys.stdout
    try:
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
