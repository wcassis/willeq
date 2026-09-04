#!/usr/bin/env python3
"""T346: extract the oracle's self-death outbound sequence from T311 Leg A.

Reads the opcode timeline TSV captured in T311 Leg A (a real Titanium client
against a real eqemu, four self-deaths) and emits
tests/goldens/self_death_outbound_g07.txt.

The capture lives OUTSIDE the checkout, at
/mnt/projects/fable/review/T311/legA/decode/T311A_table2_opcode_timeline.tsv
(review media path convention). Pass it as argv[1].

A "self death" is detected by what only OUR death can cause: the server
answers it with S->C OP_ZonePlayerToBind. (spawn_id == killer_id is not the
discriminator -- two of the four self-deaths in this capture were killed by
another spawn.) The window runs from that OP_Death to the next C->S
OP_ZoneEntry (the re-entry), and the COMPARED projection keeps only the three
opcodes this unit is about: OP_SpawnAppearance, OP_ClientUpdate,
OP_ZoneChange.

The TSV records only the first 32 payload bytes, so the OP_ZoneChange's
zone_id (at payload offset 64) is NOT in the capture and is NOT put in the
golden. The bind zone is pinned separately, from the OP_ZonePlayerToBind
bytes, which fit in 32 -- see SelfDeathSequence.ZonePlayerToBind_*.
"""

import sys
from collections import Counter

COMPARED = {"OP_SpawnAppearance", "OP_ClientUpdate", "OP_ZoneChange"}


def le(hexbytes, off, n):
    return int.from_bytes(bytes(hexbytes[off:off + n]), "little")


def main(path):
    rows = []
    with open(path) as fh:
        for line in fh:
            if line.startswith("#"):
                continue
            f = line.rstrip("\n").split("\t")
            if len(f) < 7 or f[1] == "local_ts":
                continue
            rows.append((f[0], f[2], f[3], bytes.fromhex(f[6].replace(" ", ""))
                         if f[6] else b""))

    windows = []
    for i, (zone, direction, op, body) in enumerate(rows):
        if direction != "S->C" or op != "OP_Death" or len(body) < 12:
            continue
        # Ours iff the server answers it with OP_ZonePlayerToBind.
        follows = [r for r in rows[i + 1:i + 12] if r[0] == zone]
        if not any(d == "S->C" and o == "OP_ZonePlayerToBind" for _, d, o, _ in follows):
            continue  # someone else's death
        seq = []
        for zone2, d2, op2, b2 in rows[i + 1:]:
            if zone2 != zone:
                break
            if d2 == "C->S" and op2 == "OP_ZoneEntry":
                break
            if d2 != "C->S":
                continue
            if op2 not in COMPARED:
                continue
            if op2 == "OP_SpawnAppearance" and len(b2) >= 8:
                seq.append("C->S OP_SpawnAppearance type=%d param=%d"
                           % (le(b2, 2, 2), le(b2, 4, 4)))
            else:
                seq.append("C->S " + op2)
        windows.append(tuple(seq))

    counts = Counter(windows)
    if len(counts) != 1:
        sys.exit("windows disagree: %r" % counts)
    (seq, n), = counts.items()
    print("# T346 golden -- the official client's self-death outbound sequence.")
    print("# Extracted by scripts/refactor/t311_death_golden.py from")
    print("# review/T311/legA/decode/T311A_table2_opcode_timeline.tsv (T311 Leg A:")
    print("# a real Titanium client against a real eqemu). All %d self-death" % n)
    print("# windows in that capture produce this identical sequence.")
    print("#")
    print("# COMPARED opcodes: OP_SpawnAppearance, OP_ClientUpdate, OP_ZoneChange.")
    print("# The absence of an OP_ClientUpdate line is the whole of step 2: the")
    print("# official client goes silent the instant it dies.")
    print("#")
    print("# NOT compared, and why:")
    print("#   OP_FloatListThing -- a heartbeat, not a death step (1291 sends")
    print("#     across the capture vs OP_ClientUpdate's 1522; present on both")
    print("#     sides of every death and everywhere else).")
    print("#   OP_Save -- the oracle DOES send one on every self death, but its")
    print("#     is 192 bytes of character payload and fable's SendSave() is a")
    print("#     bare 2-byte opcode. A real gap; a separate item, not this unit.")
    print("#   OP_DeleteSpawn -- sent after the server APPROVES the zone change,")
    print("#     from the zone teardown (fable: CleanupZone), which needs the")
    print("#     full handshake this headless test does not run.")
    print("#")
    print("# Never re-blessed. If a line here changes, the capture was re-read.")
    for line in seq:
        print(line)


if __name__ == "__main__":
    main(sys.argv[1])
