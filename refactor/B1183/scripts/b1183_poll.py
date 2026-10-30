#!/usr/bin/env python3
# B1183 (#502) -- live stand-byte +0x251 poller for the caster client.
#
# The gdb watchpoint design (b1183_gdb.py) needs the gdb-16 Python
# Watchpoint API; the lane-5 ThinkPad runs gdb 15.1, where it raises.
# WILL'S RULE is "the oracle column is READ from the client's memory per
# step, never inferred" -- a /proc/pid/mem poll of the byte IS that read,
# and it never stops the inferior (a hardware watchpoint's stop+backtrace
# loop would stall the zone-in of a 32-bit wine client).
#
# What it does:
#   1. Finds the CASTER eqgame.exe (the one whose environ has DISPLAY=:98;
#      the observer is on :99) and the eqgame.exe module base from maps.
#   2. Reads the client's OWN hotkey state: the live keymap object
#      (DAT_0098599c) plus the three static image tables it decodes with
#      (the action-name table and the two keycode tables below).
#   3. Reads DAT_00905CE4 (the local player's spawn) and polls
#      spawn+0x251 every 25 ms to TMAX. Every value-changing read is
#      logged (WR t=... old -> new spawn=...); the spawn pointer is
#      re-resolved every tick (zone transition, respawn).
#
# Keymap object layout (measured against the decompile, not assumed --
# the earlier stride-8 read was wrong, the getter/INI-write sites are the
# evidence):
#   FUN_004b385e allocates 0x963 bytes; FUN_004b381a zeroes 0x42 dwords
#   at +0x858. The 267 slots (0x858/2) store; the INI loader fills actions 0..0xF6 (247)
#     k0(action N) @ 4N          (the getter 0x004b244f: lea [ecx+eax*4])
#     k1(action N) @ 0x42C + 4N  (the getter 0x004b2459: lea [ecx+eax*4+0x42c];
#                                 the INI writes 0x004b3721/0x004b3754,
#                                 the loader loop 0x004b378b/0x004b37b1)
#   so the object is [267 k0 slots][267 k1 slots] = 0x858 bytes + tail.
# Slot value encoding (FUN_0049156d, the INI key builder/decoder):
#   the INI key is "KEYMAP_" + name[action] + ("_2" for k1); the value
#   string parses "SHIFT+"->0x40000000, "CONTROL+"->0x20000000,
#   "ALT+"->0x10000000, then the key name resolves to a keycode:
#     single char  -> table of 0x2F x {u8 char, i32 code} at 0x78DC48
#     named key    -> 0x36 interleaved {i32 value, name ptr} pairs at
#                     0x78DDC0 (value at 8i, ptr at 8i+4)
#   0 = unbound, 0xFFFFFFFF = *NONE* / unmatched.
#   The action-name table is 0xF7 pointers at 0x78D868
#   (PTR_s_AUTORUN_0078d868 in the decompile). All image VAs.
#
# Log format matches b1183_gdb.py's (merge_watch.py reads both):
#   START / INIT byte +0x251 = N / KEYMAP ... / WR t=X old -> new / END.
# Env: S_OUT (log base), S_TMAX, S_BASE (optional; resolved from maps if
# absent), S_POLL_MS. Run as root (ptrace_scope, B1164): sudo -n.
import os
import signal
import struct
import sys
import time

OUT = os.environ.get("S_OUT", "/tmp/b1183")
TMAX = float(os.environ.get("S_TMAX", "300"))
POLL = float(os.environ.get("S_POLL_MS", "25")) / 1000.0
BASE = int(os.environ.get("S_BASE", "0"), 16)

# Globals, eqgame.exe IMAGE VAs (Ghidra's DAT_ addresses; the PE image
# base is 0x400000). Runtime = VA + (BASE - 0x400000) -- NOT BASE + VA
# (the b1183_gdb.py OFF0 convention; a bare BASE+VA read lands 0x400000
# past the real global and returns unrelated dwords).
IMAGE_BASE = 0x400000
KEYMAP_VA = 0x0098599C
SPAWN_VA = 0x00905CE4
# The spawn linked list: head at DAT_00905cc4, nodes linked at +0x214 /
# +0x220 (a doubly linked list). B1192 (2026-10-29, measured on legH-run4):
# the original +0x220 walk DID work -- it logged the troll's XNEW (t=18.6,
# flag 1, byte 100) and its death XWR 100 -> 120 with the flag going 2
# (poll.log). The LOOT row was missing for a different reason: the plan's
# screen-coordinate click never opened the loot window, so the corpse's
# byte never left 120. B1192 keeps the walk but (1) walks +0x214, the
# stride EVERY client consumer loop uses (eqgame.exe.c 14204, 51757,
# 52798, 60422, 60587, 60733; teardown FUN_0043c20c at 54472), (2) tags
# each row with the node's +0x120 name (the corpse search at 14204 fixes
# the name offset and the corpse flag values 2/3), and (3) logs every
# head-pointer change (LIST lines) so an empty walk is visible in-run.
# The loot gate (28061/28062) writes 105 to the USE TARGET's +0x251, not
# the looter's -- so the corpse's byte is what moves on a loot, and the
# looter (local) holds 100. Track every non-local spawn to catch the
# corpse.
SPAWNLIST_VA = 0x00905CC4
# DAT_00905d04: the client's own LOOT-TARGET corpse reference. Set from
# the corpse getter at 18431 (DAT_00905d04 = FUN_0040d918()), used for the
# loot range check (distance to DAT_00905d04 at 18541) and the corpse-flag
# checks at 18490/18566, cleared when the corpse is gone (17583). Reading
# the byte this pointer names is the direct measurement of "the corpse the
# client is looting", independent of the list walk.
CORPSE_VA = 0x00905D04

PID = None
FD = None
maps = {}


def log(msg):
    with open(OUT + ".log", "a") as f:
        f.write(msg + "\n")
    print(msg, flush=True)


def _stop(signum, frame):
    raise SystemExit("signal %d" % signum)


signal.signal(signal.SIGINT, _stop)
signal.signal(signal.SIGTERM, _stop)


def find_caster():
    # Pass 1: DISPLAY=:98 in the environ (the caster's Xvfb; the observer
    # is on :99). Pass 2 fallback: lowest pid (the caster launches first).
    any_pids = []
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            with open(f"/proc/{pid}/cmdline", "rb") as f:
                cmd = f.read()
            if b"eqgame.exe" not in cmd:
                continue
            any_pids.append(int(pid))
            with open(f"/proc/{pid}/environ", "rb") as f:
                env = f.read()
            if b"DISPLAY=:98" in env:
                return int(pid), "environ DISPLAY=:98"
        except OSError:
            continue
    if any_pids:
        return min(any_pids), "fallback lowest pid (no DISPLAY=:98 environ matched)"
    return None, None


def elf_class(fd_path):
    # 1 = 32-bit, 2 = 64-bit: the whole design (image VAs, pointer widths)
    # assumes a 32-bit process; a 64-bit wine would make every 32-bit read
    # wrong.
    try:
        with open(fd_path, "rb") as f:
            h = f.read(20)
        if len(h) >= 20 and h[:4] == b"\x7fELF":
            return h[4]
    except OSError:
        pass
    return None


def fl(fd, addr):
    os.lseek(fd, addr, os.SEEK_SET)
    b = os.read(fd, 4)
    return struct.unpack("<f", b)[0] if len(b) == 4 else None


def open_mem(pid):
    return os.open(f"/proc/{pid}/mem", os.O_RDONLY)


def read_u32(fd, addr):
    os.lseek(fd, addr, os.SEEK_SET)
    b = os.read(fd, 4)
    if len(b) < 4:
        raise OSError(f"short read at 0x{addr:x}")
    return struct.unpack("<I", b)[0]


def read_bytes(fd, addr, n):
    os.lseek(fd, addr, os.SEEK_SET)
    return os.read(fd, n)


def decode_slot(v, charmap, namemap):
    """One live keymap slot (the poll's KEYMAP/SLOT lines).

    B1192 (#511): the slot's KEYCODE is its HIGH byte; the low 24 bits carry
    modifier state (never observed non-zero in a run). The pre-B1192 reader
    took the low 28 bits as the keycode and three high bits as modifier
    flags, so every real binding mis-decoded and the targeting keys looked
    unbound (run 6, poll.log: SLOT 24 raw 0x3b000000 read as
    'CTRL+ALT+code=0xb000000' when it is F1 = TARGETME; SLOT 32 raw
    0x42000000 read as 'SHIFT+code=0x2000000' when it is F8 = TARGETNPC).
    The high-byte encoding is proven against the client's own tables in the
    same log: NAMEKEY F1 0x3b / F6 0x40 / F7 0x41 / F8 0x42 (poll.log:50-57,
    IBM PC scancodes), CHARKEY 's' 0x1f (poll.log:24), and the official
    client's documented bindings (help/targeting.html: F1 self, F7 nearest
    PC, F8 nearest NPC) landing on the measured slots 24/30/32."""
    if v == 0:
        return "UNBOUND"
    if v == 0xFFFFFFFF:
        return "*NONE*"
    k = (v >> 24) & 0xFF
    key = charmap.get(k, namemap.get(k, "code=0x%02x" % k))
    mods = v & 0x00FFFFFF
    # Modifier state has never been observed non-zero: report it raw, never guess.
    return "%s+raw0x%06x" % (key, mods) if mods else key


def walk_list(read_u32, read_name, head, spawn, others, stride=0x214):
    """Pure walk of the spawn list (B1192: testable without a process).

    read_u32(addr) -> int (raise OSError on failure); read_name(addr) -> str.
    head: the list head pointer; spawn: the local player pointer to skip;
    others: in/out dict node -> (flag, byte) of previously seen nodes.
    stride: the link offset to follow. 0x214 (the default) is the stride
    every client consumer loop in the decompile uses (see the SPAWNLIST_VA
    comment); 0x220 is the other link of the same doubly linked list, which
    run 4 proved also traverses the live list. The stride parameter exists
    so the regression test (refactor/B1192/scripts/test_poll_walk.py) can
    exercise both directions against one synthetic list.

    Returns rows: (tag, flag, old_byte, new_byte, name, node); tag XNEW
    (old_byte None) or XWR.
    """
    rows = []
    seen = set()
    node = head
    n = 0
    while 0x00010000 < node < 0x80000000 and n < 96 and node not in seen:
        seen.add(node)
        try:
            nxt = read_u32(node + stride)
        except OSError:
            break
        if node != spawn:
            try:
                flag = read_u32(node + 0x244) & 0xFF
                b = read_u32(node + 0x251) & 0xFF
            except OSError:
                b = flag = None
            if b is not None:
                nm = read_name(node + 0x120)
                prev = others.get(node)
                if prev is None:
                    others[node] = (flag, b)
                    rows.append(("XNEW", flag, None, b, nm, node))
                elif b != prev[1]:
                    others[node] = (flag, b)
                    rows.append(("XWR", flag, prev[1], b, nm, node))
        node = nxt
        n += 1
    return rows


def main():
    global PID, FD, BASE
    t0 = time.time()
    PID, how = find_caster()
    if not PID:
        log("FAIL no eqgame.exe found")
        return 2
    log("START epoch=%.3f pid=%d via=%s class=%s tmax=%.0fs poll=%.0fms" % (
        t0, PID, how, elf_class(f"/proc/{PID}/exe"), TMAX, POLL * 1000))
    FD = open_mem(PID)

    if not BASE:
        with open(f"/proc/{PID}/maps") as f:
            for line in f:
                parts = line.split()
                if parts[1] == "r-xp" or parts[2] == "00000000":
                    if "eqgame.exe" in parts[-1]:
                        BASE = int(parts[0].split("-")[0], 16)
                        break
    if not BASE:
        log("FAIL no eqgame.exe base in maps")
        return 2
    log("BASE 0x%08x (image VAs shifted by 0x%x)" % (
        BASE, BASE - IMAGE_BASE))
    SHIFT = BASE - IMAGE_BASE
    keymap_rt = KEYMAP_VA + SHIFT
    spawn_rt = SPAWN_VA + SHIFT
    spawnlist_rt = SPAWNLIST_VA + SHIFT

    # 1. The client's OWN hotkey state: the static decode tables first
    #    (image VAs, mapped for the process's whole life), then the live
    #    keymap object they decode.
    charmap = {}
    namemap = {}
    try:
        raw = read_bytes(FD, 0x78DC48 + SHIFT, 47 * 8)
        for i in range(47):
            c = raw[i * 8]
            code = struct.unpack("<i", raw[i * 8 + 4:i * 8 + 8])[0]
            charmap[code & 0xFFFFFFFF] = chr(c)
            log("CHARKEY %r 0x%08x" % (chr(c), code & 0xFFFFFFFF))
        raw = read_bytes(FD, 0x78DDC0 + SHIFT, 54 * 8)
        for i in range(54):
            v = struct.unpack("<i", raw[i * 8:i * 8 + 4])[0]
            p = struct.unpack("<I", raw[i * 8 + 4:i * 8 + 8])[0]
            nm = "?"
            if 0x400000 < p < 0x80000000:
                b = b""
                for j in range(48):
                    c = read_bytes(FD, p + SHIFT + j, 1)
                    if not c or c == b"\x00":
                        break
                    b += c
                nm = b.decode("latin-1")
            namemap[v & 0xFFFFFFFF] = nm
            log("NAMEKEY %s 0x%08x" % (nm, v & 0xFFFFFFFF))
        raw = read_bytes(FD, 0x78D868 + SHIFT, 247 * 4)
        for i in range(247):
            p = struct.unpack("<I", raw[i * 4:i * 4 + 4])[0]
            nm = "?"
            if 0x400000 < p < 0x80000000:
                b = b""
                for j in range(48):
                    c = read_bytes(FD, p + SHIFT + j, 1)
                    if not c or c == b"\x00":
                        break
                    b += c
                nm = b.decode("latin-1")
            log("KEYNAME %d %s" % (i, nm))
    except OSError as e:
        log("TABLES read failed: %s" % e)

    def dec_slot(v):
        return decode_slot(v, charmap, namemap)

    try:
        obj = read_u32(FD, keymap_rt)
    except OSError as e:
        obj = 0
        log("KEYMAP read failed: %s" % e)
    log("KEYMAP obj=0x%08x (global 0x%08x)" % (obj, keymap_rt))
    if 0x00010000 < obj < 0x80000000:
        try:
            raw = read_bytes(FD, obj, 0x960)
            for row in range(0, len(raw) // 4, 16):
                log("KEYMAP " + "  ".join(
                    "0x%03x:%d" % (i * 4, struct.unpack("<I", raw[i * 4:i * 4 + 4])[0])
                    for i in range(row, min(row + 16, len(raw) // 4))))
            for i in range(267):  # 0x858/4/2 = 267 slots (the INI loader fills 0..246)
                k0 = struct.unpack("<I", raw[i * 4:i * 4 + 4])[0]
                k1 = struct.unpack("<I", raw[0x42C + i * 4:0x42C + i * 4 + 4])[0]
                log("SLOT %d k0=%s k1=%s raw0=0x%08x raw1=0x%08x" % (
                    i, dec_slot(k0), dec_slot(k1), k0, k1))
        except OSError as e:
            log("KEYMAP read failed: %s" % e)
    else:
        log("KEYMAP obj invalid 0x%x -- client not zoned yet" % obj)

    # 2. Poll the stand byte.
    spawn = 0
    last = None
    hits = 0
    noch = 0
    fails = 0
    log("INIT spawn=0x%08x" % 0)

    # 3. Non-local spawn tracking (the corpse). Walk the spawn list from
    #    spawnlist_rt with the client's own stride +0x214 (B1192: the
    #    original +0x220 followed the head's NULL back-link and visited
    #    nothing). For every node that is not the local player, log its
    #    +0x251 byte on change (XWR), tagged with its +0x244 corpse flag
    #    and its +0x120 name. This is what proves the oracle's loot fired
    #    (the corpse -> 105) while the looter (local, "WR" lines) holds 100.
    #    Separately, track_corpse follows DAT_00905d04, the client's own
    #    loot-target corpse reference (see CORPSE_VA) -- the direct pointer
    #    to the corpse being looted, no list involved.
    others = {}
    last_head = None
    last_corpse = None

    def read_name(rt, maxn=28):
        try:
            raw = read_bytes(FD, rt, maxn)
        except OSError:
            return "?"
        return raw.split(b"\x00")[0].decode("latin-1", "replace")

    def track_corpse(t):
        # The loot-target corpse pointer (DAT_00905d04). Log on any change
        # of pointer/name/flag/byte -- the 120 -> 105 transition of this
        # pointer's byte IS the loot row.
        nonlocal last_corpse
        try:
            p = read_u32(FD, CORPSE_VA + SHIFT)
        except OSError:
            return
        if p == 0 or p == spawn or not (0x00010000 < p < 0x80000000):
            if last_corpse is not None:
                log("XCORPSE t=%7.3f ptr=0x00000000 name='-'"
                    " flag=- byte=- (poll)" % t)
                last_corpse = None
            return
        try:
            flag = read_u32(FD, p + 0x244) & 0xFF
            b = read_u32(FD, p + 0x251) & 0xFF
        except OSError:
            return
        nm = read_name(p + 0x120)
        key = (p, nm, flag, b)
        if key != last_corpse:
            last_corpse = key
            log("XCORPSE t=%7.3f ptr=0x%08x name=%r flag=%d byte=%d (poll)"
                % (t, p, nm, flag, b))

    def track_others(t):
        nonlocal last_head
        try:
            head = read_u32(FD, spawnlist_rt)
        except OSError:
            return
        # B1192 diagnostic: the head pointer changes only when the list is
        # (re)built or a head node is freed; log each new head so an empty
        # walk can be seen against the run, not discovered afterwards.
        if head != last_head:
            last_head = head
            log("LIST t=%7.3f head=0x%08x (poll)" % (t, head))
        for tag, flag, ob, nb, nm, node in walk_list(
                lambda addr: read_u32(FD, addr),
                lambda addr: read_name(addr),
                head, spawn, others):
            if tag == "XNEW":
                log("XNEW t=%7.3f spawn=0x%08x flag=%d byte=%d name=%r (poll)" % (
                    t, node, flag, nb, nm))
            else:
                log("XWR t=%7.3f %d -> %d  spawn=0x%08x flag=%d name=%r (poll)" % (
                    t, ob, nb, node, flag, nm))
    while time.time() - t0 < TMAX:
        try:
            sp = read_u32(FD, spawn_rt)
        except OSError:
            sp = spawn
            fails += 1
        if not (0x00010000 < sp < 0x80000000):
            time.sleep(POLL)
            continue
        if sp != spawn:
            spawn = sp
            # Fingerprints: the raw 8 bytes at the global (pointer width
            # check), the position floats, and the 16 bytes around +0x250.
            try:
                raw8 = read_bytes(FD, spawn_rt, 8)
                log("SPAWN t=%.3f 0x%08x raw8=%s" % (
                    time.time() - t0, sp, raw8.hex()))
            except OSError as e:
                log("SPAWN t=%.3f 0x%08x raw8-fail %s" % (
                    time.time() - t0, sp, e))
            for off, nm in ((0x10, "x"), (0x14, "y"), (0x18, "z"),
                            (0x24, "h")):
                    try:
                        log("POS %s=%r" % (nm, fl(FD, sp + off)))
                    except OSError as e:
                        log("POS %s-fail %s" % (nm, e))
            try:
                win = read_bytes(FD, sp + 0x250, 16)
                log("WIN t=%.3f +0x250..0x25f=%s" % (
                    time.time() - t0, win.hex()))
            except OSError as e:
                log("WIN fail %s" % e)
            try:
                last = read_u32(FD, sp + 0x251) & 0xFF
            except OSError:
                time.sleep(POLL)
                continue
            log("INIT byte +0x251 = %d" % last)
        try:
            val = read_u32(FD, sp + 0x251) & 0xFF
        except OSError:
            fails += 1
            # A dead target makes every read throw; bail out after ~25 s of
            # consecutive failures instead of running to TMAX.
            if fails > 1000:
                log("ABORT %d consecutive read failures (target gone?)" % fails)
                return 3
            time.sleep(POLL)
            continue
        fails = 0
        t = time.time() - t0
        if val != last:
            hits += 1
            log("WR t=%7.3f %s -> %d  spawn=0x%08x  (poll)" % (
                t, last if last is not None else "?", val, sp))
            last = val
        noch += 1
        if noch % 200 == 0:
            log("SNAP t=%7.3f spawn=0x%08x byte=%d fails=%d" % (
                t, spawn, last, fails))
        if noch % 4 == 0:  # walk the spawn list every ~100 ms
            track_others(t)
            track_corpse(t)
        time.sleep(POLL)
    log("END hits=%d ticks=%d fails=%d t=%.3f" % (
        hits, noch, fails, time.time() - t0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
