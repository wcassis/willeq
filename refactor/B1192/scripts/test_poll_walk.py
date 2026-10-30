#!/usr/bin/env python3
# B1192 (#511) -- regression test for b1183_poll.py's spawn-list walk.
#
# Context (measured, legH-run4, 2026-10-29, poll.log): the walk DID find
# the troll -- XNEW t=18.591 (flag 1, byte 100) and the death XWR
# 100 -> 120 with the corpse flag going 2 at t=38.215. What the run did
# NOT produce was the 120 -> 105 transition (the loot gate 28061/28062),
# because the plan's screen-coordinate click never opened the loot window
# (the click sat on a stale player corpse left in the zone by an earlier
# run -- visible in the 04_obs still BEFORE the kill step, and outside the
# spawn list the walk covers). B1192's poller changes: names on every
# row (+0x120), LIST lines for head changes, and DAT_00905d04 tracking
# (the client's own loot-target corpse reference) -- and this test pins
# the walk's extraction logic: both link directions, the death
# transition, and the loot transition, with names intact.
#
# Command line (also the one cited in refactor/B1192/README.md):
#   python3 refactor/B1192/scripts/test_poll_walk.py
# Exit 0 = pass, 1 = fail.
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "B1183", "scripts")))
import b1183_poll as P  # noqa: E402

A = 0x22CF5900   # list head (the shape mirrors run 4's poll.log pointers)
B = 0x22CF5C60   # the troll: alive (flag 1, byte 100) -> corpse (2/120) -> looted (105)
LOCAL = 0x0F070F88  # the local player (must be skipped, like the real poller)


class FakeMem:
    """A byte-addressable memory image of the spawn list.

    Node layout (only the offsets the poller touches):
      +0x120  name (C string)
      +0x214  forward link (the client's walk direction)
      +0x220  back link (NULL at the head -- so the two directions are
              asymmetric on this list, exactly the shape run 4 walked)
      +0x244  corpse flag
      +0x251  stand byte
    """

    def __init__(self):
        self.mem = {}

    def put32(self, addr, v):
        for i, c in enumerate(struct.pack("<I", v)):
            self.mem[addr + i] = c

    def put_name(self, addr, s):
        for i, c in enumerate(s.encode("latin-1") + b"\x00"):
            self.mem[addr + i] = c

    def put_byte(self, addr, v):
        self.mem[addr] = v & 0xFF

    def map_region(self, addr, size=0x280):
        # A real node lives in mapped heap; the whole region is readable
        # (zeros where no field was written).
        for i in range(size):
            self.mem.setdefault(addr + i, 0)

    def node(self, addr, name, flag, byte, fwd, back):
        self.map_region(addr)
        self.put_name(addr + 0x120, name)
        self.put32(addr + 0x214, fwd)
        self.put32(addr + 0x220, back)
        self.put_byte(addr + 0x244, flag)
        self.put_byte(addr + 0x251, byte)

    def read_u32(self, addr):
        # /proc/pid/mem semantics: a read failing on ANY unmapped byte
        # raises OSError (walk_list only catches OSError).
        if any(addr + i not in self.mem for i in range(4)):
            raise OSError("no mapping at 0x%x" % addr)
        b = bytes(self.mem[addr + i] for i in range(4))
        return struct.unpack("<I", b)[0]

    def read_name(self, addr, maxn=28):
        out = bytearray()
        a = addr
        for _ in range(maxn):
            c = self.mem.get(a)
            if c is None or c == 0:
                break
            out.append(c)
            a += 1
        return out.decode("latin-1")


def build():
    m = FakeMem()
    m.node(A, "TrollF", 1, 100, B, 0)      # head: forward->B, back NULL
    m.node(B, "TrollF", 1, 100, 0, A)      # tail: forward NULL, back->A
    m.node(LOCAL, "Testcaster", 0, 100, 0, 0)
    return m


def main():
    fails = []

    # 1. The client's stride (+0x214, the default): finds both nodes.
    m = build()
    others = {}
    rows = P.walk_list(m.read_u32, m.read_name, A, LOCAL, others)
    if sorted((r[0], r[5]) for r in rows) != [("XNEW", A), ("XNEW", B)]:
        fails.append("stride +0x214: expected XNEW for A and B, got %r" % rows)
    elif sorted(r[4] for r in rows) != ["TrollF", "TrollF"]:
        fails.append("stride +0x214: names %r, expected 'TrollF' on both" % rows)
    else:
        print("PASS 1: stride +0x214 finds both nodes, names intact")

    # 2. The other link (+0x220) on this list shape reaches only the head
    #    (the head's back link is NULL) -- the asymmetry that makes the
    #    walk follow the client's own stride.
    m = build()
    rows = P.walk_list(m.read_u32, m.read_name, A, LOCAL, {}, stride=0x220)
    if sorted(r[5] for r in rows) != [A]:
        fails.append("stride +0x220: expected only the head, got %r" % rows)
    else:
        print("PASS 2: stride +0x220 on this shape reaches only the head")

    # 3. The death: the troll's byte 100 -> 120 with the corpse flag going
    #    2 -- exactly run 4's XWR t=38.215 (100 -> 120, flag 2).
    m = build()
    others = {}
    P.walk_list(m.read_u32, m.read_name, A, LOCAL, others)
    m.put_byte(B + 0x251, 120)
    m.put_byte(B + 0x244, 2)
    rows = P.walk_list(m.read_u32, m.read_name, A, LOCAL, others)
    if len(rows) != 1 or (rows[0][0], rows[0][2], rows[0][3], rows[0][5]) != ("XWR", 100, 120, B):
        fails.append("death: expected one XWR 100 -> 120 on the troll, got %r" % rows)
    elif rows[0][4] != "TrollF" or rows[0][1] != 2:
        fails.append("death: expected name 'TrollF' flag 2, got %r" % rows)
    else:
        print("PASS 3: death XWR 100 -> 120, corpse flag 2, named (run 4's t=38.215)")

    # 4. The loot gate: the corpse byte 120 -> 105 -- the LOOT row of
    #    B1183's table, as the poller will now log it on the wire.
    m = build()
    others = {}
    P.walk_list(m.read_u32, m.read_name, A, LOCAL, others)
    m.put_byte(B + 0x251, 120)
    m.put_byte(B + 0x244, 2)
    P.walk_list(m.read_u32, m.read_name, A, LOCAL, others)
    m.put_byte(B + 0x251, 105)  # the loot gate 28061/28062 fires
    rows = P.walk_list(m.read_u32, m.read_name, A, LOCAL, others)
    if len(rows) != 1 or (rows[0][0], rows[0][2], rows[0][3], rows[0][4], rows[0][5]) \
            != ("XWR", 120, 105, "TrollF", B):
        fails.append("loot: expected one XWR 120 -> 105 named 'TrollF', got %r" % rows)
    else:
        print("PASS 4: loot XWR 120 -> 105 named 'TrollF' (the LOOT row)")

    # 5. The local player is never reported (the poller tracks it via WR).
    m = build()
    others = {}
    rows = P.walk_list(m.read_u32, m.read_name, A, LOCAL, others)
    if any(r[5] == LOCAL for r in rows):
        fails.append("local player reported: %r" % rows)
    else:
        print("PASS 5: local player skipped")

    if fails:
        for f in fails:
            print("FAIL: %s" % f)
        return 1
    print("PASS (5/5) b1183_poll.walk_list regression")
    return 0


sys.exit(main())
