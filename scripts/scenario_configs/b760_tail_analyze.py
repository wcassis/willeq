#!/usr/bin/env python3
"""B760 (issue #45): the per-cast tail table for one caster log.

Reads a caster client log written at ZONE:DEBUG,SPELL:DEBUG (the
cast_tail_repeat_* lane, scripts/scenario.sh) and prints one row per
`ManaChange ... keepcasting=0` of the caster's own cast:

  * every zone packet the client dispatched after it, up to the first RELIABLE
    one that is not OP_LinkedReuse (the `[ZonePacket] <op> rel=<0|1>` lines of
    ZoneComponent::DispatchZonePacket) -- "inside the tail";
  * the TRUTH: eqemu queues the success tail (ManaChange keepcasting 0,
    [OP_LinkedReuse], OP_MemorizeSpell scribing 3; zone/spells.cpp:1817-1824)
    contiguously on the reliable stream, so the cast succeeded iff that first
    reliable packet is OP_MemorizeSpell;
  * what SpellManager settled ("Cast of '<n>' (ID: <id>) completed
    successfully" / "was stopped by the server", or "was interrupted" when the
    server's OP_InterruptCast ended the cast BEFORE the keepcasting 0), and the
    settle latency from the keepcasting-0 line.

Exit 0 when every settled outcome matches the truth, 1 otherwise (a B760
wrong-result reproduction), 2 on an unreadable log.

Usage: b760_tail_analyze.py <caster.log> [--tsv FILE]
"""

import argparse
import datetime
import re
import sys

TS_RE = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})\]")
ZONE_RE = re.compile(r"\[ZonePacket\] (\S+)(?: rel=([01]))?")
KC0_RE = re.compile(r"ManaChange: mana=\d+, stamina=\d+, spell=(\d+), keepcasting=0\b")
SETTLE_RE = re.compile(
    r"Cast of '([^']*)' \(ID: (\d+)\) (completed successfully|"
    r"was stopped by the server[^\n]*|was interrupted|fizzled)")
CYCLE_RE = re.compile(r"cycle (\d+): (SUCCESS|INTERRUPT)")

LINKED = ("HC_OP_LinkedReuse", "OP_LinkedReuse")
MEMORIZE = ("HC_OP_MemorizeSpell", "OP_MemorizeSpell")
# How long after a keepcasting 0 the walk keeps looking for a settle line.
WINDOW_S = 20.0


def ts_of(line):
    m = TS_RE.match(line)
    if not m:
        return None
    return datetime.datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f")


def analyze(lines):
    rows = []
    cycle = None
    last_interrupt_settle = None  # (ts, spell) of a "was interrupted" line
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        cm = CYCLE_RE.search(line)
        if cm:
            cycle = (int(cm.group(1)), cm.group(2))
        sm = SETTLE_RE.search(line)
        if sm and sm.group(3) == "was interrupted":
            last_interrupt_settle = (ts_of(line), int(sm.group(2)))
        km = KC0_RE.search(line)
        if not km or int(km.group(1)) == 0:
            i += 1
            continue
        t0 = ts_of(line)
        spell = int(km.group(1))
        row = {
            "cycle": cycle, "spell": spell, "t0": t0, "inside": [],
            "first_reliable": None, "settled": None, "settle_ms": None,
            "line": i + 1,
        }
        # Interrupted by OP_InterruptCast before this keepcasting 0?
        if last_interrupt_settle and last_interrupt_settle[1] == spell and t0 and \
                last_interrupt_settle[0] and (t0 - last_interrupt_settle[0]).total_seconds() < 2.0:
            row["settled"] = "Interrupted(OP_InterruptCast)"
            row["settle_ms"] = (last_interrupt_settle[0] - t0).total_seconds() * 1000.0
        last_interrupt_settle = None
        j = i + 1
        while j < n:
            l2 = lines[j]
            t2 = ts_of(l2)
            if t2 and t0 and (t2 - t0).total_seconds() > WINDOW_S:
                break
            if KC0_RE.search(l2):
                break
            zm = ZONE_RE.search(l2)
            if zm and row["first_reliable"] is None:
                op, rel = zm.group(1), zm.group(2)
                rel = None if rel is None else rel == "1"
                if rel is not False and op not in LINKED:
                    row["first_reliable"] = op
                    row["first_reliable_ms"] = (t2 - t0).total_seconds() * 1000.0 if t2 else None
                elif op not in LINKED:
                    row["inside"].append(op)
            s2 = SETTLE_RE.search(l2)
            if s2 and row["settled"] is None and int(s2.group(2)) == spell:
                kind = s2.group(3)
                row["settled"] = ("Success" if kind.startswith("completed") else
                                  "Interrupted" if kind.startswith("was stopped") else kind)
                row["settle_ms"] = (t2 - t0).total_seconds() * 1000.0 if t2 else None
            if row["settled"] is not None and row["first_reliable"] is not None:
                break
            j += 1
        fr = row["first_reliable"]
        if row["settled"] == "Interrupted(OP_InterruptCast)":
            row["truth"] = "Interrupted(OP_InterruptCast)"
        else:
            row["truth"] = ("Success" if fr in MEMORIZE else
                            "Interrupted" if fr is not None else "unknown")
        row["ok"] = row["truth"] == row["settled"]
        rows.append(row)
        i += 1
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--tsv")
    args = ap.parse_args()
    try:
        with open(args.log, errors="replace") as fh:
            lines = fh.read().splitlines()
    except OSError as exc:
        print("cannot read %s: %s" % (args.log, exc))
        return 2
    rows = analyze(lines)
    out = []
    hdr = ("n\tcycle\tspell\tline\tunreliable_inside\tfirst_reliable\tfirst_reliable_ms"
           "\ttruth\tsettled\tsettle_ms\tok")
    out.append(hdr)
    for k, r in enumerate(rows):
        cyc = "%d:%s" % r["cycle"] if r["cycle"] else "-"
        out.append("\t".join(str(x) for x in (
            k, cyc, r["spell"], r["line"], ",".join(r["inside"]) or "-",
            r["first_reliable"] or "-",
            "%.0f" % r["first_reliable_ms"] if r.get("first_reliable_ms") is not None else "-",
            r["truth"], r["settled"] or "-",
            "%.0f" % r["settle_ms"] if r["settle_ms"] is not None else "-",
            "OK" if r["ok"] else "WRONG")))
    print("\n".join(out))
    pend = [r for r in rows if r["truth"] in ("Success", "Interrupted")]
    wrong = [r for r in rows if not r["ok"]]
    inside = [r for r in pend if r["inside"]]
    lat = sorted(r["settle_ms"] for r in pend if r["settle_ms"] is not None)
    print()
    print("casts=%d pending-outcome=%d interrupted-by-OP_InterruptCast=%d" % (
        len(rows), len(pend), len(rows) - len(pend)))
    print("pending casts with >=1 unreliable packet inside the tail: %d" % len(inside))
    print("wrong outcomes: %d" % len(wrong))
    if lat:
        print("settle latency ms (pending): min=%.0f median=%.0f max=%.0f" % (
            lat[0], lat[len(lat) // 2], lat[-1]))
    for truth in ("Success", "Interrupted"):
        sub = sorted(r["settle_ms"] for r in pend if r["truth"] == truth and r["settle_ms"] is not None)
        if sub:
            print("  %s: n=%d min=%.0f median=%.0f max=%.0f" % (
                truth, len(sub), sub[0], sub[len(sub) // 2], sub[-1]))
    if args.tsv:
        with open(args.tsv, "w") as fh:
            fh.write("\n".join(out) + "\n")
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
