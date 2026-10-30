# B1183 (#502) -- live keymap dump + stand-byte +0x251 write watch for the
# caster client. B1164 pattern (b1164_s_gdb.py): one `gdb -nx -batch -x`
# script under `timeout -s INT`; nothing here blocks the inferior.
#
# What it does, on attach:
#   1. Dumps the live keymap object (DAT_0098599c, 0x963 bytes of key dwords)
#      -- the client's OWN hotkey state, read not inferred (B1183's keys are
#      read from this dump, never from fable's table).
#   2. Resolves the local player's spawn (DAT_00905ce4) and sets a write
#      watchpoint on its +0x251 stand byte. Every write that CHANGES the byte
#      is logged with the writer backtrace mapped to eqgame.exe image VAs;
#      writes to the other bytes of the same dword are counted as NOCH.
#   3. Re-arms the watch if the spawn pointer itself moves.
#
# Stop: TMAX (the watchpoint's stop() returns True, the main continue
# returns, detach) or SIGINT (timeout). Log: $S_OUT.log; stdout: $S_OUT.out.
# Env: S_OUT, S_TMAX, S_BASE (the eqgame.exe module base from /proc maps).
import gdb
import os
import time

OUT = os.environ.get("S_OUT", "/tmp/b1183_gdb")
TMAX = float(os.environ.get("S_TMAX", "180"))
BASE = int(os.environ.get("S_BASE", "0x400000"), 16)
OFF0 = BASE - 0x400000  # runtime addr -> eqgame.exe image VA delta

# Globals, image VA -> runtime:
KEYMAP_G = 0x0098599C + OFF0  # const int *DAT_0098599c -> keymap object
SPAWN_G = 0x00905CE4 + OFF0   # int *DAT_00905CE4 -> local player's spawn

t0 = time.time()
spawn_addr = 0
watch = None
last_val = None
hits = 0
nochange = 0


def log(msg):
    with open(OUT + ".log", "a") as f:
        f.write(msg + "\n")
    print(msg, flush=True)


def u32(a):
    return int(gdb.parse_and_eval("*(unsigned int*)0x%x" % a))


def u8(a):
    return int(gdb.parse_and_eval("*(unsigned char*)0x%x" % a))


def rt2va(a):
    a = int(a)
    if BASE <= a < BASE + 0x00800000:
        return "0x%08x" % (a - OFF0)
    return None


class Watcher(gdb.Watchpoint):
    """Write watchpoint: log value-changing hits with the writer backtrace."""

    def stop(self):
        global hits, nochange, last_val, spawn_addr, watch
        hits += 1
        t = time.time() - t0
        addr = self.memory_address()
        try:
            val = u8(addr)
        except gdb.error as e:
            log("WATCHFAIL t=%.3f %s" % (t, e))
            return False
        if val != last_val:
            frames = []
            try:
                for f in list(gdb.selected_frame().superiors())[:14]:
                    pc = int(f.pc())
                    va = rt2va(pc)
                    frames.append(va if va else "extern+0x%x" % pc)
            except gdb.error:
                frames = ["(no backtrace)"]
            log("WR t=%7.3f %s -> %d  spawn=0x%08x  %s" % (
                t, last_val if last_val is not None else "?", val,
                addr - 0x251, " <- ".join(frames[:8])))
            last_val = val
        else:
            nochange += 1
            if nochange % 50 == 1:
                log("NOCH t=%.3f count=%d byte=%d (another byte of the dword)"
                    % (t, nochange, val))
        # Re-arm if the spawn pointer moved (zone transition, respawn).
        try:
            sp = u32(SPAWN_G)
        except gdb.error:
            sp = spawn_addr
        if sp and sp != spawn_addr:
            log("SPAWN t=%.3f 0x%08x -> 0x%08x re-arming" % (t, spawn_addr, sp))
            self.destroy()
            spawn_addr = sp
            last_val = u8(sp + 0x251)
            log("INIT(re) spawn=0x%08x byte=%d" % (sp, last_val))
            watch = Watcher("*(unsigned char*)0x%08x" % (sp + 0x251),
                            gdb.BP_LOC_WRITE)
        return time.time() - t0 > TMAX


def main():
    global spawn_addr, watch, last_val
    log("START base=0x%x tmax=%.0fs" % (BASE, TMAX))

    # 1. The live keymap dump (597 key dwords, action slot = 8*i bytes).
    try:
        obj = u32(KEYMAP_G)
    except gdb.error as e:
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
        for start in range(0, 0x960, 16):
            row = []
            for i in range(start, start + 16, 4):
                try:
                    d = u32(obj + i)
                except gdb.error:
                    d = -1
                m = marks.get(i)
                row.append("0x%03x:%d%s" % (i, d, ("#" + m) if m else ""))
            log("KEYMAP " + "  ".join(row))
        log("KEYMAP tail 0x960:%d 0x961:%d 0x962:%d"
            % (u8(obj + 0x960), u8(obj + 0x961), u8(obj + 0x962)))
    else:
        log("KEYMAP obj invalid 0x%x -- client not zoned yet" % obj)

    # 2. Spawn + watchpoint.
    try:
        spawn_addr = u32(SPAWN_G)
    except gdb.error as e:
        spawn_addr = 0
        log("SPAWN read failed: %s" % e)
    log("INIT spawn=0x%08x" % spawn_addr)
    if 0x00010000 < spawn_addr < 0x80000000:
        last_val = u8(spawn_addr + 0x251)
        log("INIT byte +0x251 = %d" % last_val)
        for off, nm in ((0x10, "x"), (0x14, "y"), (0x18, "z"),
                        (0x24, "h")):
            try:
                log("POS %s=%s" % (nm, gdb.parse_and_eval(
                    "*(float*)0x%x" % (spawn_addr + off))))
            except gdb.error:
                pass
        watch = Watcher("*(unsigned char*)0x%08x" % (spawn_addr + 0x251),
                        gdb.BP_LOC_WRITE)
        log("WATCH armed on 0x%08x (byte %d)" % (spawn_addr + 0x251, last_val))
    else:
        log("INIT spawn invalid 0x%x -- no watch" % spawn_addr)

    # 3. Run until TMAX / INT / inferior exit.
    while True:
        gdb.execute("continue")
        ev = gdb.selected_event()
        if isinstance(ev, gdb.StopEvent):
            sig = ev.stop_signal()
            if sig and "INT" in str(sig):
                log("INT t=%.3f" % (time.time() - t0))
                break
            if time.time() - t0 > TMAX:
                log("TMAX t=%.3f" % (time.time() - t0))
                break
            continue  # stop() returned False but gdb stopped anyway: re-run
        log("EVENT %s t=%.3f" % (type(ev).__name__, time.time() - t0))
        break  # ExitedEvent etc.: the process is gone

    try:
        if watch is not None:
            watch.delete()
    except gdb.error:
        pass
    log("END hits=%d nochange=%d t=%.3f" % (hits, nochange, time.time() - t0))
    try:
        gdb.execute("detach")
    except gdb.error:
        pass


main()
