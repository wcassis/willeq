#!/usr/bin/env python3
"""#102: ONE rendered, script-driven willeq zone run ON THE ORANGE PI.

Runs on the Pi (python3 from the NFS worktree), launched over ssh by
scripts/pi_rendered_zone_sweep.sh. It exists because `--scenario` forces the
headless Automated lane (no GPU pipeline at all -- B854's caveat); a RENDERED
client is driven the way scripts/pose_live_capture.sh drives one: `--script`
(a held command line, then `wait N`, then `/q`).

What it does:
  * launches the client (argv after `--`) with stdin=/dev/null, stdout+stderr
    captured to <out>/client.log, each line prefixed with the epoch it was
    READ (so time-to-ready is measured, not inferred);
  * samples /proc/<pid>/status every --sample-ms (RssAnon/RssFile/RssShmem/
    VmHWM) into <out>/samples.tsv -- the true peak RssAnon between the
    ledger's lifecycle points (the ledger prints only AT points);
  * records /proc/meminfo MemAvailable before the launch and at every
    [memledger] lifecycle point (system-wide view: GEM/driver pages the
    process RSS never shows);
  * dead-client watchdog: no new output line for --stall seconds -> SIGTERM,
    5 s, SIGKILL; overall wall cap --cap seconds -> the same. The reason is
    recorded; the client's own exit status is recorded either way;
  * writes <out>/summary.json;
  * B932, opt-in: --snap-points p1,p2 writes <out>/snap_<point>.json at each
    named [memledger] point -- scripts/pi/proc_mem_snapshot.py's status +
    per-mapped-file Rss/Anonymous + the mapped .so list, i.e. the proof of
    which GL driver libraries the process really loaded.
  * B955, opt-in: --own-cgroup launches the client in its OWN memory cgroup
    (`systemd-run --user --scope`, same pid: the scope execs the client), so
    the client's gpu_buffers ledger row can read its GEM/shmem bytes from
    memory.stat (include/client/core/gpu_buffer_census.h). The log is written
    by THIS runner, outside that cgroup, so it never lands in the row; frame
    dumps the client writes into a tmpfs do, and the summarizer names them.

It never interprets the run (pass/fail is the host-side summarizer's job,
scripts/pi/rendered_zone_summarize.py), so a change of criterion never
needs a Pi re-run.

usage: rendered_zone_run.py --out DIR [--cap 300] [--stall 180]
                            [--sample-ms 20] [--snap-points startup,ready]
                            -- <client argv...>
"""
import argparse
import json
import os
import re
import signal
import subprocess
import sys
import threading
import time

POINT_RE = re.compile(r"\[memledger\] point=([a-z_0-9]+) ")


def mem_available_kb():
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                if line.startswith("MemAvailable:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return -1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--cap", type=float, default=300.0)
    ap.add_argument("--stall", type=float, default=180.0)
    ap.add_argument("--sample-ms", type=float, default=20.0)
    # The script's held command fires when the client is fully CONNECTED
    # (loading Phase 11), not at the ledger's `ready` (measured, dalnir on
    # the Pi: 15 s later), so a fixed script wait cannot land after
    # steady_60s for every zone. The runner ends the run itself: SIGTERM
    # (the client's clean requestQuit path, P36) --stop-delay s after the
    # --stop-point ledger line. The script's own /q is the fallback.
    ap.add_argument("--stop-point", default="steady_60s")
    ap.add_argument("--stop-delay", type=float, default=5.0)
    ap.add_argument("--snap-points", default="")
    ap.add_argument("--own-cgroup", action="store_true",
                    help="B955: run the client in its own systemd --user scope (memcg)")
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd
    if not cmd:
        ap.error("no client command after --")
    os.makedirs(a.out, exist_ok=True)

    summary = {
        "cmd": cmd,
        "cap_s": a.cap,
        "stall_s": a.stall,
        "mem_available_kb_before": mem_available_kb(),
        "points": [],
        "kill_reason": "",
        "stop_reason": "",
    }
    t0 = time.time()
    summary["start_epoch"] = t0
    if a.own_cgroup:
        unit = "willeq-run-%d-%d" % (os.getpid(), int(t0))
        cmd = ["systemd-run", "--user", "--scope", "--quiet", "--collect", "--unit=" + unit] + cmd
        summary["cgroup_unit"] = unit
    proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, bufsize=0, cwd=a.out)
    last_line = [time.time()]
    lines = [0]
    snap_points = set(p for p in a.snap_points.split(",") if p)

    def snap(point):
        # A capability binary is non-dumpable: its /proc/<pid>/smaps is
        # root-owned, so the snapshot runs under sudo -n (the sweep's Pi
        # user has it). /proc/<pid>/status stays world-readable.
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "proc_mem_snapshot.py")
        t = time.time()
        try:
            out = subprocess.run(["sudo", "-n", "python3", script, str(proc.pid), "--label", point],
                                 capture_output=True, timeout=30)
            if out.returncode != 0:
                raise OSError(out.stderr.decode("utf-8", "replace")[-300:])
            d = json.loads(out.stdout)
            d["epoch"] = t
            with open(os.path.join(a.out, "snap_%s.json" % point), "w") as f:
                json.dump(d, f, indent=1)
        except (OSError, ValueError, subprocess.SubprocessError) as e:
            summary.setdefault("snap_errors", []).append("%s: %s" % (point, e))

    def pump():
        with open(os.path.join(a.out, "client.log"), "w", buffering=1) as log:
            buf = b""
            while True:
                chunk = proc.stdout.read(4096)
                if not chunk:
                    break
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    now = time.time()
                    text = line.decode("utf-8", "replace")
                    log.write("%.4f %s\n" % (now, text))
                    last_line[0] = now
                    lines[0] += 1
                    m = POINT_RE.search(text)
                    if m:
                        summary["points"].append({"point": m.group(1), "epoch": now,
                                                  "mem_available_kb": mem_available_kb()})
                        if m.group(1) in snap_points:
                            snap_points.discard(m.group(1))
                            threading.Thread(target=snap, args=(m.group(1),), daemon=True).start()
                        if m.group(1) == a.stop_point and not summary["stop_reason"]:
                            summary["stop_reason"] = "SIGTERM %.0f s after %s" % (a.stop_delay, a.stop_point)
                            threading.Timer(a.stop_delay, lambda: proc.poll() is None
                                            and proc.send_signal(signal.SIGTERM)).start()
            if buf:
                log.write("%.4f %s\n" % (time.time(), buf.decode("utf-8", "replace")))

    def stop(reason):
        if summary["kill_reason"]:
            return
        summary["kill_reason"] = reason
        summary["kill_epoch"] = time.time()
        try:
            proc.send_signal(signal.SIGTERM)
        except OSError:
            return
        for _ in range(50):
            if proc.poll() is not None:
                return
            time.sleep(0.1)
        try:
            proc.kill()
        except OSError:
            pass

    def watchdog():
        while proc.poll() is None:
            time.sleep(1.0)
            now = time.time()
            if now - t0 >= a.cap:
                stop("cap: %.0f s overall wall cap reached" % a.cap)
                return
            if now - last_line[0] >= a.stall:
                stop("stall: no output line for %.0f s (after %d lines)" % (a.stall, lines[0]))
                return

    pt = threading.Thread(target=pump, daemon=True)
    pt.start()
    threading.Thread(target=watchdog, daemon=True).start()

    peak = {"rss_anon_kb": 0, "epoch": 0.0}
    vm_hwm = 0
    with open(os.path.join(a.out, "samples.tsv"), "w", buffering=1 << 16) as s:
        s.write("epoch\trss_anon_kb\trss_file_kb\trss_shmem_kb\tvm_hwm_kb\n")
        path = "/proc/%d/status" % proc.pid
        while proc.poll() is None:
            vals = {}
            try:
                with open(path) as f:
                    for line in f:
                        k = line.split(":", 1)[0]
                        if k in ("RssAnon", "RssFile", "RssShmem", "VmHWM"):
                            vals[k] = int(line.split()[1])
            except (OSError, ValueError, IndexError):
                break
            now = time.time()
            ra = vals.get("RssAnon", 0)
            if ra > peak["rss_anon_kb"]:
                peak = {"rss_anon_kb": ra, "epoch": now}
            vm_hwm = max(vm_hwm, vals.get("VmHWM", 0))
            s.write("%.3f\t%d\t%d\t%d\t%d\n" % (now, ra, vals.get("RssFile", 0),
                                                vals.get("RssShmem", 0), vals.get("VmHWM", 0)))
            time.sleep(a.sample_ms / 1000.0)
    rc = proc.wait()
    pt.join(timeout=5)
    summary["end_epoch"] = time.time()
    summary["returncode"] = rc
    summary["signal"] = -rc if rc < 0 else 0
    summary["log_lines"] = lines[0]
    summary["peak_rss_anon_kb_sampled"] = peak["rss_anon_kb"]
    summary["peak_rss_anon_epoch"] = peak["epoch"]
    summary["vm_hwm_kb"] = vm_hwm
    summary["mem_available_kb_after"] = mem_available_kb()
    with open(os.path.join(a.out, "summary.json"), "w") as f:
        json.dump(summary, f, indent=1)
    print("rendered_zone_run: rc=%d kill=%r lines=%d peak_rss_anon_kb=%d"
          % (rc, summary["kill_reason"], lines[0], peak["rss_anon_kb"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
