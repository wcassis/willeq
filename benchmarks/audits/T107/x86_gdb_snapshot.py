import threading, gdb, time

# T107 -- x86 side capture, reusing T106's gdb_live_snapshot.py method
# verbatim (STOP_AFTER unchanged at 55s -- T106 confirmed this lands after
# zone-in settles and matches its own "Tracked total: 62.7MB" befallen
# reading).
STOP_AFTER = 55.0

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
out_smaps = "/tmp/claude-1000/-mnt-projects-fable/5abdd905-13d3-4400-a496-a3da67581ea4/scratchpad/T107/x86_befallen_smaps.txt"
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
