#!/usr/bin/env python3
# B1192 (#511) -- regression test for b1183_poll.py's keymap slot decode.
#
# Context (measured, legH-run6, 2026-10-29, poll.log): the pre-B1192
# dec_slot read the slot's keycode as its LOW 28 bits with three high
# modifier bits, but the live keymap stores the keycode in the HIGH byte
# and the modifier state in the low 24 bits. Every real binding therefore
# mis-decoded: SLOT 24 raw 0x3b000000 came out 'CTRL+ALT+code=0xb000000'
# when it is F1 = TARGETME, and SLOT 32 raw 0x42000000 came out
# 'SHIFT+code=0x2000000' when it is F8 = TARGETNPC -- which is how the
# run-6 diagnosis missed that the official client already binds F8 to
# target-nearest-NPC and the plan kept trying name-based /target on a
# corpse (the official client has no such thing; /loot then had no corpse
# target and never wrote the LOOT row's 105).
#
# Ground truth is run 6's own poll.log (the client's own tables, read live):
#   NAMEKEY F1 0x3b (poll.log:50), F6 0x40 (:55), F7 0x41 (:56), F8 0x42 (:57)
#   CHARKEY '1' 0x02 (poll.log:3), 's' 0x1f (:24)
#   SLOT 24 raw0=0x3b000000 (TARGETME), SLOT 30 raw0=0x41000000 (TARGETPC),
#   SLOT 32 raw0=0x42000000 (TARGETNPC) -- and the official client's
#   documented bindings (help/targeting.html: F1 self, F7 nearest PC,
#   F8 nearest NPC) land exactly there.
#
# Command line (also the one cited in refactor/B1192/README.md):
#   python3 refactor/B1192/scripts/test_poll_decode.py
# Exit 0 = pass, 1 = fail.
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "B1183", "scripts")))
import b1183_poll as P  # noqa: E402

# Run 6's poll.log CHARKEY/NAMEKEY lines (the client's decode tables).
CHAR = {0x02: '1', 0x1f: 's'}
NAME = {0x3b: 'F1', 0x40: 'F6', 0x41: 'F7', 0x42: 'F8'}


def main():
    fails = []

    def chk(v, want, what):
        got = P.decode_slot(v, CHAR, NAME)
        if got != want:
            fails.append("%s: 0x%08x -> %r, expected %r" % (what, v, got, want))
        else:
            print("PASS %s: 0x%08x -> %r" % (what, v, got))

    # Run 6's real slots (poll.log SLOT lines) against the real tables.
    chk(0, "UNBOUND", "unbound slot")
    chk(0xFFFFFFFF, "*NONE*", "the *NONE* sentinel")
    chk(0x3b000000, "F1", "SLOT 24 TARGETME")
    chk(0x41000000, "F7", "SLOT 30 TARGETPC")
    chk(0x42000000, "F8", "SLOT 32 TARGETNPC -- the loot fix's key")
    chk(0x1f000000, "s", "SLOT 3 k1 (the sit key 's', B1183 FINDING 1)")
    chk(0x02000000, "1", "a digit slot")

    # The pre-B1192 mis-decode must not come back: F8 is NOT a modifier +
    # an unresolvable code...
    got = P.decode_slot(0x42000000, CHAR, NAME)
    if "code=" in got or got.split("+")[0] in ("SHIFT", "CTRL", "ALT"):
        fails.append("F8 slot mis-decoded again: %r" % got)
    else:
        print("PASS F8 decodes as a key, not a modifier + unknown code: %r" % got)

    # ... and an unknown code in the high byte still reports the code.
    chk(0xAB000000, "code=0xab", "unknown high byte")

    if fails:
        for f in fails:
            print("FAIL: %s" % f)
        return 1
    print("PASS (9/9) b1183_poll.decode_slot regression (B1192 #511)")
    return 0


sys.exit(main())
