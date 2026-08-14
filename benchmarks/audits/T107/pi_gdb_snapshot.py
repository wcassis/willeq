import threading, gdb, time

# T107 -- Pi-side snapshot, adapted from T106's gdb_live_snapshot.py.
# Stopped later than the x86 capture (200s vs 50s) because the plan
# requires overlap with a LATER-starting x86 client (x86 joins after the
# Pi's own ~44s zone-load is confirmed complete over ssh), and both must be
# simultaneously in-zone at their respective capture instants.
STOP_AFTER = 200.0

def do_interrupt():
    try:
        gdb.execute("interrupt")
    except Exception as e:
        print("interrupt error:", e)

def timer():
    time.sleep(STOP_AFTER)
    gdb.post_event(do_interrupt)

t = threading.Thread(target=timer)
t.start()
gdb.execute("run")

pid = gdb.selected_inferior().pid
print("=== STOPPED pid=%d at t=%.1fs ===" % (pid, STOP_AFTER))

with open("/proc/%d/status" % pid) as f:
    status = f.read()
for line in status.splitlines():
    if line.startswith(("VmRSS", "VmSize", "VmData", "VmStk", "VmExe", "VmLib")):
        print(line)

with open("/proc/%d/smaps" % pid) as f:
    smaps = f.read()
# Written directly to the NFS mount (visible from both hosts) -- NEVER to
# the Pi's 242MB /tmp tmpfs, per T107's capture discipline.
out_smaps = "/mnt/projects/fable/wt/slot1/benchmarks/audits/T107/pi_run/pi_befallen_smaps.txt"
with open(out_smaps, "w") as f:
    f.write(smaps)
print("smaps written to", out_smaps)

print("--- malloc_stats() ---")
try:
    gdb.execute("call (void)malloc_stats()")
except Exception as e:
    print("malloc_stats call failed:", e)

print("--- mallinfo2() ---")
try:
    val = gdb.parse_and_eval("mallinfo2()")
    print("mallinfo2() =", val)
except Exception as e:
    print("mallinfo2 call failed:", e)

print("=== RESUMING to completion ===")
gdb.execute("continue")
