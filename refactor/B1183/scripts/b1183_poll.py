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
#   2. Reads DAT_0098599c (the live keymap pointer) and dumps 0x964 bytes
#      of the keymap object -- the client's OWN hotkey state.
#   3. Reads DAT_00905CE4 (the local player's spawn) and polls
#      spawn+0x251 every 25 ms to TMAX. Every value-changing read is
#      logged (WR t=... old -> new spawn=...); the spawn pointer is
#      re-resolved every tick (zone transition, respawn).
#
# Log format matches b1183_gdb.py's (merge_watch.py reads both):
#   START / INIT byte +0x251 = N / KEYMAP ... / WR t=X old -> new / END.
# Env: S_OUT (log base), S_TMAX, S_BASE (optional; resolved from maps if
# absent), S_POLL_MS. Run as root (ptrace_scope, B1164): sudo -n.
import os
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

PID = None
FD = None
maps = {}


def log(msg):
    with open(OUT + ".log", "a") as f:
        f.write(msg + "\n")
    print(msg, flush=True)


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


def main():
    global PID, FD, BASE
    t0 = time.time()
    PID, how = find_caster()
    if not PID:
        log("FAIL no eqgame.exe found")
        return 2
    log("START pid=%d via=%s class=%s tmax=%.0fs poll=%.0fms" % (
        PID, how, elf_class(f"/proc/{PID}/exe"), TMAX, POLL * 1000))
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

    # 1. The live keymap dump (the client's OWN hotkey state).
    try:
        obj = read_u32(FD, keymap_rt)
    except OSError as e:
        obj = 0
        log("KEYMAP read failed: %s" % e)
    log("KEYMAP obj=0x%08x (global 0x%08x)" % (obj, keymap_rt))
    if 0x00010000 < obj < 0x80000000:
        marks = {
            0x60: "action12 DUCK k0", 0x64: "action12 DUCK k1",
            0x80: "action16 USE k0", 0x84: "action16 USE k1",
            0x448: "action137 CAMP k0", 0x44C: "action137 CAMP k1",
            0x450: "action138 SIT_STAND k0", 0x454: "action138 SIT_STAND k1",
        }
        try:
            raw = read_bytes(FD, obj, 0x964)
            dw = struct.unpack("<%dI" % (len(raw) // 4), raw[: (len(raw) // 4) * 4])
            for row in range(0, len(dw), 16):
                cells = []
                for i in range(row, min(row + 16, len(dw))):
                    m = marks.get(i * 4)
                    cells.append("0x%03x:%d%s" % (i * 4, dw[i],
                                                  ("#" + m) if m else ""))
                log("KEYMAP " + "  ".join(cells))
            log("KEYMAP tail 0x960:%d 0x961:%d 0x962:%d" % (
                raw[0x960], raw[0x961], raw[0x962]))
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
            time.sleep(POLL)
            continue
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
        time.sleep(POLL)
    log("END hits=%d ticks=%d fails=%d t=%.3f" % (
        hits, noch, fails, time.time() - t0))
    return 0


sys.exit(main())
