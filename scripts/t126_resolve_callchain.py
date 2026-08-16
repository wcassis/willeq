#!/usr/bin/env python3
"""
scripts/t126_resolve_callchain.py — symbolise the T126 return-address chain
recorded on every TEXPARAMS ring entry (docker/irrlicht-drm/t122_texprobe.h)
against the unstripped probe binary and its matching core dump.

Usage:
    python3 scripts/t126_resolve_callchain.py <binary> <core> <ring.tsv> \
        [--findings findings.tsv] [--seq SEQ ...]

Replaces T124's post-mortem structural inference (resolving a material_ptr
through core-dump memory that may have been reused since the write) with the
call PATH recorded at write time. Each TEXPARAMS ring row now carries 8
raw return addresses (retaddr0..7, outermost-first) captured via
__builtin_return_address(0..7) at the cacheTexParams() call site itself
(frame 0 = cacheTexParams()'s immediate caller, i.e. wherever in
COpenGLES2Driver.cpp calls it; deeper frames walk further up the call stack
past the driver's own internal dispatch -- the scene-node/subsystem draw
call that ultimately reached it).

**Why gdb, not a standalone addr2line offset:** `willeq` is a PIE binary,
so the retaddr values recorded in the ring are RUNTIME (post-ASLR) addresses
-- they only mean something once you know that run's load bias. Rather than
recompute the bias by hand, this script does what
scripts/t122_extract_ring.py already does for the ring symbols themselves:
open the exact matching core via `gdb -batch -core-file` and let gdb resolve
`info symbol`/`info line` against the mapped regions the core itself
records. This requires the core to be from the SAME PROCESS INVOCATION as
the ring.tsv passed in (a fresh run has a different bias) -- always pass the
runN_core.willeq that produced runN_ring.tsv.

**All addresses for the whole run are resolved in ONE gdb session** (not one
gdb invocation per address) -- opening a ~1.4GB core is the expensive part
(seconds), and this script may need to resolve 100+ addresses per run; doing
that address-at-a-time reloads the core each time and is what made the first
version of this script time out on real runs.

A null (0x0) retaddr entry mid-chain means the frame walk ran out --
reported as "(no frame)", not an error.
"""
import argparse
import subprocess
import sys

# gdb's own textual output interleaves "[New LWP ...]" thread-attach chatter
# with our actual "info symbol"/"info line" answers when a core has many
# threads (this codebase spawns a big worker pool) -- these are filtered out
# before parsing, not meaningful to the symbolisation itself.
_NOISE_PREFIXES = (
    "[New LWP", "This GDB supports", "Enable debuginfod", "Debuginfod has",
    "To make this setting", "warning: could not find", "[Thread debugging",
    "Using host", "Core was generated", "Program terminated", "#0 ",
    "warning: ", "[Current thread",
)


def _is_noise(line):
    return any(line.startswith(p) for p in _NOISE_PREFIXES)


def gdb_batch_all(binary, core, addrs):
    """One gdb session: for each unique address, run 'info symbol' + 'info
    line *addr', each preceded by an echoed marker so the combined stdout can
    be split back apart deterministically (gdb has no other way to separate
    the output of N queries run via repeated -ex without spawning N processes).
    """
    args = ["gdb", "-q", "-batch", "-nx", "-ex", f"file {binary}", "-ex", f"core-file {core}"]
    for i, addr in enumerate(addrs):
        args += ["-ex", f'printf "@@T126MARK{i}@@\\n"',
                 "-ex", f"info symbol {hex(addr)}",
                 "-ex", f"info line *{hex(addr)}"]
    proc = subprocess.run(args, capture_output=True, text=True, timeout=180)
    out = proc.stdout
    chunks = {}
    parts = out.split("@@T126MARK")
    for part in parts[1:]:
        idx_str, _, rest = part.partition("@@\n")
        try:
            idx = int(idx_str)
        except ValueError:
            continue
        chunks[idx] = rest
    return chunks, proc.stderr


def _format_chunk(chunk_text):
    lines = [l for l in chunk_text.splitlines() if l.strip() and not _is_noise(l)]
    # Stop at the next marker's leftovers, if any slipped through (shouldn't,
    # since we split on the marker itself, but be defensive).
    sym_line = next((l for l in lines if not l.startswith("Line")), "(unresolved)")
    line_line = next((l for l in lines if l.startswith("Line")), "")
    combined = sym_line.strip()
    if line_line:
        combined += "  |  " + line_line.strip()
    return combined


def load_ring(path):
    rows = {}
    with open(path) as f:
        for line in f:
            if line.startswith("#") or line.startswith("seq"):
                continue
            fields = line.rstrip("\n").split("\t")
            if len(fields) < 13:
                continue  # pre-T126 (or old 4-deep T126) ring TSV, no/insufficient retaddr columns
            seq, kind, ptr, tid, value = fields[:5]
            retaddr = [int(x, 16) for x in fields[5:13]]
            rows[int(seq)] = {
                "kind": kind, "ptr": ptr, "tid": tid, "value": value,
                "retaddr": retaddr,
            }
    return rows


def load_findings(path):
    out = []
    with open(path) as f:
        for line in f:
            if line.startswith("#") or line.startswith("seq"):
                continue
            seq, ptr, cls = line.rstrip("\n").split("\t")
            out.append((int(seq), ptr, cls))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binary", help="the probe-build willeq binary the run executed (unstripped, -g)")
    ap.add_argument("core", help="the core dump from the SAME run that produced ring_tsv (load bias must match)")
    ap.add_argument("ring_tsv", help="scripts/t122_extract_ring.py output (must include T126 retaddr columns)")
    ap.add_argument("--findings", default=None,
                     help="scripts/t122_classify_ring.py --findings-out output; symbolise every row's seq")
    ap.add_argument("--seq", type=int, nargs="*", default=None, help="symbolise these specific seq numbers")
    args = ap.parse_args()

    ring = load_ring(args.ring_tsv)

    seqs = list(args.seq) if args.seq else []
    findings_meta = {}
    if args.findings:
        for seq, ptr, cls in load_findings(args.findings):
            seqs.append(seq)
            findings_meta[seq] = (ptr, cls)

    if not seqs:
        print("No --seq or --findings given; nothing to symbolise.", file=sys.stderr)
        sys.exit(1)

    # Collect all distinct nonzero addresses across all requested seqs, symbolise once.
    addr_list = []
    addr_index = {}
    for seq in seqs:
        entry = ring.get(seq)
        if entry is None:
            continue
        for ra in entry["retaddr"]:
            if ra != 0 and ra not in addr_index:
                addr_index[ra] = len(addr_list)
                addr_list.append(ra)

    chunks, stderr = gdb_batch_all(args.binary, args.core, addr_list)
    resolved = {}
    for ra, idx in addr_index.items():
        chunk = chunks.get(idx, "")
        resolved[ra] = _format_chunk(chunk) if chunk else "(gdb produced no output)"

    for seq in seqs:
        entry = ring.get(seq)
        if entry is None:
            print(f"seq={seq}: NOT FOUND in {args.ring_tsv} (ring wrapped past it, or wrong TSV)")
            continue
        meta = findings_meta.get(seq)
        header = f"seq={seq} ptr={entry['ptr']} value={entry['value']} tid={entry['tid']}"
        if meta:
            header += f" class={meta[1]}"
        print(header)
        for i, ra in enumerate(entry["retaddr"]):
            sym = "(no frame)" if ra == 0 else resolved.get(ra, "(unresolved)")
            print(f"  frame[{i}] = 0x{ra:x}  {sym}")
        print()


if __name__ == "__main__":
    main()
