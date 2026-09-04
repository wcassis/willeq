#!/usr/bin/env python3
"""T349 (BOARD B059): extract the official client's 192-byte OP_Save body.

Reads the SERVER-SIDE packet logs captured in T311 Leg A (a real Titanium
client against a real eqemu, four self-deaths) and emits
tests/goldens/op_save_body_g08.txt.

The captures live OUTSIDE the checkout, at
/mnt/projects/fable/review/T311/legA/zonelogs/*_packetlog.log (review media
path convention). Pass them as argv[1:]. The opcode-timeline TSV cannot be
used here: it keeps only the first 32 payload bytes and this body is 192.

WHY A MASK. All four bodies agree on most bytes and disagree on the rest.
Of the disagreements, exactly one group DECODES: the three floats at offsets
5/9/13, which are byte-for-byte the y/x/z of the last C->S OP_ClientUpdate
before that death (checked here, against the same logs -- the script exits
non-zero if any of the four fails that check). Those are emitted as `yy`,
`xx`, `zz` markers: the test supplies the position and compares them. Every
OTHER byte that varies across the four is emitted as `..`, because nothing in
the capture explains it -- offsets 110..174 are visibly uninitialized client
memory (32-bit heap pointers, and the fourth death does not even share the
field shape of the other three).

Bytes that agree across all four are emitted verbatim and ARE compared. The
caveat, since it cannot be measured away from one capture: all four samples
come from ONE client process, so a "constant" may be process-lifetime-stable
stack content rather than a real field. eqemu's Handle_OP_Save
(/mnt/projects/akk-stack/code/zone/client_packet.cpp:13661) reads none of the
body, so the distinction has no server-visible consequence.
"""

import struct
import sys

sys.path.insert(0, "/mnt/projects/fable/review/T311/legA/decode")
from parse_packetlog import parse  # noqa: E402

POS = {"y": 5, "x": 9, "z": 13}
BODY = 192


def main(paths):
    saves = []
    for path in paths:
        recs = parse(path)
        last_update = None
        for r in recs:
            if r["dir"] == "C->S" and r["op"] == "OP_ClientUpdate" and len(r["data"]) >= 46:
                last_update = r["data"]
            elif r["dir"] == "C->S" and r["op"] == "OP_Save":
                if len(r["data"]) != BODY:
                    sys.exit("OP_Save body is %d bytes, expected %d" % (len(r["data"]), BODY))
                if last_update is None:
                    sys.exit("an OP_Save with no preceding OP_ClientUpdate")
                saves.append((r["data"], last_update))
    if len(saves) != 4:
        sys.exit("expected the capture's 4 self-death OP_Save packets, found %d" % len(saves))

    # PlayerPositionUpdateClient_Struct: x@18, z@26, y@30 (see
    # review/T311/legA/decode/parse_packetlog.py, which the TSVs were made with).
    for body, upd in saves:
        for name, off, updoff in (("y", 5, 30), ("x", 9, 18), ("z", 13, 26)):
            if body[off:off + 4] != upd[updoff:updoff + 4]:
                sys.exit("OP_Save %s at offset %d is not the last OP_ClientUpdate's %s"
                         % (name, off, name))

    bodies = [b for b, _ in saves]

    # The uninitialized-memory region, masked WHOLESALE rather than per byte.
    # A few bytes inside it happen to agree across the four (offsets 113, 128,
    # 136, 152) -- they are stale stack that did not move, not fields, and
    # emitting them as constants would make fable copy four arbitrary numbers
    # out of another process's stack. Its bounds are the first and last varying
    # byte above the decoded prefix; the assertion below keeps that honest.
    varying = [i for i in range(BODY) if len({b[i] for b in bodies}) > 1]
    noise_lo, noise_hi = 110, 174
    stray = [i for i in varying if i >= 19 and not (noise_lo <= i <= noise_hi)]
    if stray:
        sys.exit("varying bytes outside the noise region: %r" % stray)

    cells = []
    for i in range(BODY):
        if any(off <= i < off + 4 for off in POS.values()):
            for name, off in POS.items():
                if off <= i < off + 4:
                    cells.append(name * 2)
        elif noise_lo <= i <= noise_hi or i in varying:
            cells.append("..")
        else:
            cells.append("%02x" % bodies[0][i])

    print("# T349 golden -- the official client's 192-byte OP_Save body (B059).")
    print("# Extracted by scripts/refactor/t349_op_save_golden.py from")
    print("# review/T311/legA/zonelogs/*_packetlog.log (T311 Leg A: a real")
    print("# Titanium client against a real eqemu, four self-deaths, dumped")
    print("# server-side as \"Size [194]\" = 2 opcode bytes + 192 body bytes).")
    print("#")
    print("# One cell per body byte, 16 per line, offset in the left column.")
    print("#   hex  -- identical in all 4 captured bodies; COMPARED verbatim.")
    print("#   yy/xx/zz -- the position triple at offsets 5/9/13, byte-for-byte")
    print("#           the y/x/z of the last C->S OP_ClientUpdate before that")
    print("#           death (all 4, z bit pattern included). The test supplies")
    print("#           the position and COMPARES these.")
    print("#   ..   -- NOT compared. Offsets 110..174 are uninitialized client")
    print("#           memory (32-bit heap pointers; the 4th death does not even")
    print("#           share the other three's field shape), masked as a whole")
    print("#           region -- four bytes inside it agree across the four")
    print("#           samples and are stale stack, not fields. The rest simply")
    print("#           vary with nothing in the capture to explain them.")
    print("#")
    print("# eqemu reads NONE of this body: Handle_OP_Save")
    print("# (/mnt/projects/akk-stack/code/zone/client_packet.cpp:13661) just")
    print("# calls Save(), and Save_Struct is declared uint8[192]. So the size")
    print("# and the position are the whole of the semantics; the constants are")
    print("# matched because matching the oracle is free, not because anything")
    print("# reads them. All 4 samples are from ONE client process, so a")
    print("# \"constant\" here may be process-stable stack content.")
    print("#")
    print("# Never re-blessed. If a cell here changes, the capture was re-read.")
    for row in range(0, BODY, 16):
        print("%03d: %s" % (row, " ".join(cells[row:row + 16])))


if __name__ == "__main__":
    main(sys.argv[1:])
