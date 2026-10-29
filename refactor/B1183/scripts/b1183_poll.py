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

# Globals, eqgame.exe IMAGE VA (runtime = base + va):
KEYMAP_G = 0x0098599C
SPAWN_G = 0x00905CE4

PID = None
FD = None
maps = {}


def log(msg):
    with open(OUT + ".log", "a") as f:
        f.write(msg + "\n")
    print(msg, flush=True)


def find_caster():
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            with open(f"/proc/{pid}/cmdline", "rb") as f:
                cmd = f.read().split(b"\0")
            if b"eqgame.exe" not in b" ".join(cmd):
                continue
            with open(f"/proc/{pid}/environ", "rb") as f:
                env = f.read()
            if b"DISPLAY=:98" in env:
                return int(pid)
        except OSError:
            continue
    # fallback: any eqgame.exe
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            with open(f"/proc/{pid}/cmdline", "rb") as f:
                if b"eqgame.exe" in f.read():
                    return int(pid)
        except OSError:
            continue
    return None


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
    PID = find_caster()
    if not PID:
        log("FAIL no eqgame.exe found")
        return 2
    log("START pid=%d tmax=%.0fs poll=%.0fms" % (PID, TMAX, POLL * 1000))
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
    log("BASE 0x%08x" % BASE)

    # 1. The live keymap dump (the client's OWN hotkey state).
    try:
        obj = read_u32(FD, BASE + KEYMAP_G)
    except OSError as e:
        obj = 0
        log("KEYMAP read failed: %s" % e)
    log("KEYMAP obj=0x%08x" % obj)
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
    log("INIT spawn=0x%08x" % 0)
    while time.time() - t0 < TMAX:
        try:
            sp = read_u32(FD, BASE + SPAWN_G)
        except OSError:
            sp = spawn
        if not (0x00010000 < sp < 0x80000000):
            time.sleep(POLL)
            continue
        if sp != spawn:
            spawn = sp
            try:
                last = read_u32(FD, sp + 0x251) & 0xFF
            except OSError:
                time.sleep(POLL)
                continue
            log("INIT byte +0x251 = %d" % last)
            log("SPAWN t=%.3f 0x%08x" % (time.time() - t0, sp))
        try:
            val = read_u32(FD, sp + 0x251) & 0xFF
        except OSError:
            time.sleep(POLL)
            continue
        t = time.time() - t0
        if val != last:
            hits += 1
            log("WR t=%7.3f %s -> %d  spawn=0x%08x  (poll)" % (
                t, last if last is not None else "?", val, sp))
            last = val
        noch += 1
        time.sleep(POLL)
    log("END hits=%d ticks=%d t=%.3f" % (hits, noch, time.time() - t0))
    return 0


sys.exit(main())
