#!/usr/bin/env python3
"""H04 -- shared helpers for the Orange Pi tier-2 timing scripts.

Used by pi_bench_run.py (bench_pi.sh) and pi_bench_compare.py (compare_pi.sh).
All environment (host, paths, tolerances) comes from scripts/pi/env.sh via
os.environ -- this module reads it, never hard-codes it, so env.sh stays the
single place to override host/paths.
"""
import csv
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

PROJECT_DIR = Path(os.environ["PROJECT_DIR"])
MANIFEST_PATH = Path(os.environ["MANIFEST_PATH"])
ARTIFACTS_DIR = Path(os.environ["ARTIFACTS_DIR"])
BASELINE_DIR = Path(os.environ["BASELINE_DIR"])

PI_HOST = os.environ["PI_HOST"]
PI_USER = os.environ["PI_USER"]
PI_SSH_OPTS = os.environ["PI_SSH_OPTS"].split()
ARM_BUILD_BIN_DIR = Path(os.environ["ARM_BUILD_BIN_DIR"])
PI_EQ_PATH = os.environ["PI_EQ_PATH"]
PI_EQE1_PATH = os.environ["PI_EQE1_PATH"]
PI_REMOTE_TMP = os.environ["PI_REMOTE_TMP"]
PI_REMOTE_BIN = os.environ["PI_REMOTE_BIN"]
H04_TOTAL_FRAMES = int(os.environ["H04_TOTAL_FRAMES"])
H04_COOLDOWN_SEC = float(os.environ["H04_COOLDOWN_SEC"])
H04_WARMUP_REPS = int(os.environ.get("H04_WARMUP_REPS", "1"))

BENCH_LOCAL_NAME = "gles2_zone_benchmark"


# ─────────────────────────────────────────────────────────────────────────
# P02 protocol upgrades (permanent, panic-safe hardware protocol)
# ─────────────────────────────────────────────────────────────────────────
# P01 was interrupted by a Pi kernel panic after 41 dense back-to-back
# gles2_zone_benchmark --drm invocations in one session (see
# refactor/P01_eqe1_perf_investigation.md's "Panic context record"). The
# orchestrator's leading hypothesis was kernel-side GEM/CMA buffer
# accumulation across many rapid DRM context init/teardown cycles. These
# three primitives implement the panic-safe protocol required before ANY
# further hardware experiment runs: (1) CmaFree telemetry + abort
# thresholds, (2) a hard session cap with scripted reboot + wait-for-ssh +
# NFS mount reverification, (3) randomized/interleaved A/B ordering (see
# randomized_variant_order() below and its use in pi_bench_run.py).

# CmaFree abort thresholds (see SessionAbort / CmaMonitor below).
CMA_FREE_ABORT_KB = 40 * 1024       # abort if CmaFree drops below this
CMA_TREND_WINDOW = 5                # consecutive runs to check for a monotonic downward ratchet
MAX_INVOCATIONS_PER_SESSION = 12    # session cap before a scripted reboot cycle
REBOOT_WAIT_TIMEOUT_SEC = 180       # max time to wait for ssh to come back after reboot
REBOOT_POLL_INTERVAL_SEC = 5


class SessionAbort(RuntimeError):
    """Raised when the panic-safe protocol decides to stop hardware work
    mid-session (CmaFree below threshold, or a monotonic downward ratchet
    across CMA_TREND_WINDOW consecutive runs). Callers must treat this as a
    clean-stop signal: finish governor restore / cleanup, then surface it."""


def read_pi_meminfo():
    """Sample /proc/meminfo CmaFree and MemAvailable (kB) on the Pi. Cheap,
    single ssh round-trip; safe to call before and after every benchmark
    invocation per the protocol."""
    proc = ssh_run(
        "grep -E '^(CmaFree|MemAvailable):' /proc/meminfo",
        timeout=30,
    )
    out = {}
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        key, val = line.split(":", 1)
        out[key.strip()] = int(val.strip().split()[0])  # kB
    return out


class CmaMonitor:
    """Tracks CmaFree across a session's invocations. Logs the trend
    prominently every sample (a downward ratchet is itself the P01 panic-
    mechanism finding, not just a safety trigger) and raises SessionAbort if
    CmaFree drops below CMA_FREE_ABORT_KB, or if it has decreased on every
    one of the last CMA_TREND_WINDOW samples (a monotonic ratchet, even
    while still above the absolute floor)."""

    def __init__(self, log_path=None):
        self.samples = []  # list of (label, CmaFree_kB, MemAvailable_kB)
        self.log_path = log_path

    def sample(self, label):
        info = read_pi_meminfo()
        cma = info.get("CmaFree")
        avail = info.get("MemAvailable")
        self.samples.append((label, cma, avail))
        trend = ""
        if len(self.samples) >= 2:
            prev_cma = self.samples[-2][1]
            delta = cma - prev_cma
            trend = f" (delta {delta:+d} kB vs previous sample)"
        print(f"[cma] {label}: CmaFree={cma} kB MemAvailable={avail} kB{trend}")
        if self.log_path is not None:
            with open(self.log_path, "a") as f:
                f.write(f"{time.time():.0f}\t{label}\t{cma}\t{avail}\n")

        if cma is not None and cma < CMA_FREE_ABORT_KB:
            raise SessionAbort(
                f"CmaFree {cma} kB is below the abort threshold "
                f"{CMA_FREE_ABORT_KB} kB at '{label}' -- aborting session cleanly "
                f"(governor restore still runs) per the panic-safe protocol."
            )

        if len(self.samples) >= CMA_TREND_WINDOW:
            window = [s[1] for s in self.samples[-CMA_TREND_WINDOW:]]
            if all(a is not None and b is not None for a, b in zip(window, window[1:])) and \
               all(b < a for a, b in zip(window, window[1:])):
                print(f"[cma] WARNING: monotonic downward ratchet across the last "
                      f"{CMA_TREND_WINDOW} samples: {window} -- this is the P01 "
                      f"panic-mechanism signature. Aborting session cleanly.")
                raise SessionAbort(
                    f"CmaFree trended monotonically down across the last "
                    f"{CMA_TREND_WINDOW} samples ({window}) -- aborting session "
                    f"per the panic-safe protocol even though still above the "
                    f"absolute floor."
                )
        return cma, avail


def verify_nfs_mounted():
    proc = ssh_run("mountpoint -q /mnt/projects && echo MOUNTED || echo NOT_MOUNTED", timeout=30, check=False)
    if "MOUNTED" not in proc.stdout:
        raise RuntimeError(
            "NFS /mnt/projects is NOT mounted on the Pi after reboot -- fstab "
            "auto-mount was expected to handle this. Refusing to continue."
        )
    print("[reboot] NFS /mnt/projects confirmed mounted.")


def wait_for_ssh(timeout=REBOOT_WAIT_TIMEOUT_SEC, poll_interval=REBOOT_POLL_INTERVAL_SEC):
    """Poll ssh reachability after a reboot. Raises if the Pi doesn't come
    back within timeout -- this is the panic-detection path for a reboot
    that never completes (as opposed to a mid-session ssh timeout, which is
    how the P01 panic first manifested)."""
    print(f"[reboot] waiting for ssh (timeout {timeout}s)...")
    deadline = time.time() + timeout
    last_err = None
    while time.time() < deadline:
        try:
            proc = ssh_run("echo READY", timeout=10, check=False)
            if proc.returncode == 0 and "READY" in proc.stdout:
                print("[reboot] ssh reachable.")
                return
        except Exception as e:  # subprocess timeout etc.
            last_err = e
        time.sleep(poll_interval)
    raise RuntimeError(
        f"Pi did not come back over ssh within {timeout}s of the scripted "
        f"reboot -- possible panic during boot or a hang unrelated to the "
        f"benchmark session. Check the kmsg follower log "
        f"(/mnt/projects/fable/pi_console/kmsg_follow.log) before retrying. "
        f"Last error: {last_err}"
    )


def scripted_reboot_and_wait():
    """Session-cap reboot cycle: sudo reboot, wait for ssh, verify the NFS
    mount came back (fstab auto-mounts it per the P02 setup, but verify
    rather than assume). Cooldowns before/after are the caller's
    responsibility (session boundaries already have natural pauses)."""
    print("[reboot] session cap reached -- issuing scripted reboot...")
    try:
        ssh_run("sudo -n reboot", timeout=10, check=False)
    except Exception as e:
        print(f"[reboot] ssh call to trigger reboot raised (expected -- the "
              f"connection drops as the Pi goes down): {e}")
    # Give the Pi a moment to actually start going down before polling --
    # otherwise the first few ssh attempts might still succeed against the
    # not-yet-rebooted system and produce a false "back up" read.
    time.sleep(10)
    wait_for_ssh()
    verify_nfs_mounted()


class SessionRunner:
    """Tracks invocation count + CmaFree across a session. Does NOT reboot
    itself (rebooting invalidates an active GovernorPin, so the reboot
    decision belongs to the caller's loop, between GovernorPin blocks) --
    instead exposes over_cap() so the caller knows when to close the current
    GovernorPin, call scripted_reboot_and_wait(), and open a fresh one.
    Use as: runner.before_invocation(label); ...run benchmark...;
    runner.after_invocation(label); if runner.over_cap(): <reboot cycle>."""

    def __init__(self, cma_log_path=None, cap=MAX_INVOCATIONS_PER_SESSION):
        self.count = 0
        self.cap = cap
        self.cma = CmaMonitor(log_path=cma_log_path)

    def before_invocation(self, label):
        self.cma.sample(f"{label} (pre)")

    def after_invocation(self, label):
        self.count += 1
        self.cma.sample(f"{label} (post, invocation #{self.count})")

    def over_cap(self):
        """True exactly once every `cap` invocations (count=12,24,36,...) --
        naturally correct across reboots since count is a monotonic total,
        never reset, so the modulo keeps firing at the right spacing."""
        return self.count > 0 and self.count % self.cap == 0


def run_session(work_items, run_fn, cma_log_path=None, label_fn=None, cooldown_sec=None,
                cap=MAX_INVOCATIONS_PER_SESSION, post_reboot_fn=None):
    """P02: generic panic-safe session driver. Runs run_fn(item) once per
    item in work_items, inside a GovernorPin, sampling CmaFree before/after
    each invocation and cycling a scripted reboot every `cap` invocations
    (closing/reopening the governor pin around the reboot). On SessionAbort
    (CmaFree threshold/ratchet), restores the governor and re-raises so the
    caller can persist whatever partial results exist and stop cleanly.

    label_fn(item) -> str for logging; defaults to str(item).
    cooldown_sec defaults to H04_COOLDOWN_SEC.
    post_reboot_fn() is called after each scripted reboot completes, BEFORE
    the governor is re-pinned -- REQUIRED when the staged benchmark binary
    lives on Pi /tmp (tmpfs), because a reboot wipes /tmp and the next
    invocation would fail exit-127; pass a re-stage function (e.g.
    lambda: strip_and_stage_binary(scratch)). Found the hard way in P02's
    first capped session: invocation 13 after the first reboot cycle hit
    exactly this.
    Returns the SessionRunner used (for its .cma.samples history)."""
    if label_fn is None:
        label_fn = str
    if cooldown_sec is None:
        cooldown_sec = H04_COOLDOWN_SEC

    runner = SessionRunner(cma_log_path=cma_log_path, cap=cap)
    work_items = list(work_items)
    governor = GovernorPin()
    governor.__enter__()
    try:
        for idx, item in enumerate(work_items):
            label = label_fn(item)
            runner.before_invocation(label)
            run_fn(item)
            runner.after_invocation(label)
            time.sleep(cooldown_sec)
            # Only cycle a reboot when more work remains -- a trailing reboot
            # after the final invocation wastes ~90s and (worse) can push a
            # bounded caller past its own timeout before results are written.
            if runner.over_cap() and idx + 1 < len(work_items):
                print(f"\n=== Session cap ({runner.cap}) reached at invocation "
                      f"#{runner.count} -- cycling: governor restore, reboot, "
                      f"wait-for-ssh, verify NFS mount, re-stage, re-pin governor ===")
                governor.__exit__(None, None, None)
                scripted_reboot_and_wait()
                if post_reboot_fn is not None:
                    post_reboot_fn()
                governor = GovernorPin()
                governor.__enter__()
    except SessionAbort:
        governor.__exit__(None, None, None)
        raise
    except BaseException:
        governor.__exit__(None, None, None)
        raise
    else:
        governor.__exit__(None, None, None)
    return runner


def randomized_variant_order(zones, variants, repeats=1, rng_seed=None):
    """Build a randomized/interleaved (zone, variant) run order across
    `repeats` full passes -- fixes H3 (the fixed rgba-then-eqe1 order
    confound P01 confirmed real and significant) permanently for both the
    tier-2 baseline gate and ad hoc investigation runs. Each pass visits
    every (zone, variant) combination exactly once, in a freshly shuffled
    order (not just one shuffle repeated `repeats` times), then passes are
    concatenated. Deterministic if rng_seed is given (reproducible A/B
    sequences for debugging), otherwise seeded from system entropy."""
    import random
    rng = random.Random(rng_seed)
    order = []
    for _ in range(repeats):
        combo = [(z, v) for z in zones for v in variants]
        rng.shuffle(combo)
        order.extend(combo)
    return order


def sshtarget():
    return f"{PI_USER}@{PI_HOST}"


def ssh_run(remote_cmd, timeout=300, check=True):
    """Run a single remote command string over ssh. No interactive prompts."""
    cmd = ["ssh"] + PI_SSH_OPTS + [sshtarget(), remote_cmd]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if check and proc.returncode != 0:
        raise RuntimeError(
            f"ssh command failed (exit {proc.returncode}): {remote_cmd}\n"
            f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
    return proc


def scp_to(local_path, remote_path, timeout=120):
    cmd = ["scp"] + PI_SSH_OPTS + [str(local_path), f"{sshtarget()}:{remote_path}"]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(f"scp to Pi failed: {proc.stdout}\n{proc.stderr}")


def rsync_from(remote_glob, local_dir, timeout=120):
    """Pull files matching remote_glob (already-expanded remote path/glob
    string) from the Pi to local_dir. Runs AFTER the timed phase only."""
    local_dir.mkdir(parents=True, exist_ok=True)
    cmd = ["rsync", "-av", "-e", "ssh " + " ".join(PI_SSH_OPTS),
           f"{sshtarget()}:{remote_glob}", str(local_dir) + "/"]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(f"rsync from Pi failed: {proc.stdout}\n{proc.stderr}")


def preflight():
    """Reachability + asset checks. Raises on hard failures, prints warnings
    for soft ones (stale binary)."""
    print(f"[preflight] ssh reachable check ({sshtarget()})...")
    proc = ssh_run("echo OK && uname -a")
    print(f"  {proc.stdout.strip()}")

    local_bin = ARM_BUILD_BIN_DIR / BENCH_LOCAL_NAME
    if not local_bin.exists():
        raise RuntimeError(f"ARM binary not found: {local_bin} -- run scripts/build-arm-noble.sh first")

    bin_mtime = local_bin.stat().st_mtime
    src_dirs = [
        PROJECT_DIR / "src" / "client" / "graphics",
        PROJECT_DIR / "include" / "client" / "graphics",
        PROJECT_DIR / "tools" / "hwcaps",
    ]
    newest_src = 0.0
    for d in src_dirs:
        if not d.exists():
            continue
        for p in d.rglob("*"):
            if p.is_file():
                newest_src = max(newest_src, p.stat().st_mtime)
    if newest_src > bin_mtime:
        age = newest_src - bin_mtime
        print(f"[preflight] WARNING: source under {src_dirs} is {age:.0f}s newer than "
              f"the ARM binary ({local_bin}) -- it may be stale. Re-run "
              f"scripts/build-arm-noble.sh if this run should reflect recent changes.")
    else:
        print(f"[preflight] ARM binary is newer than watched source dirs -- OK.")

    print("[preflight] checking zone assets reachable from the Pi...")
    proc = ssh_run(f"test -d {PI_EQ_PATH} && test -d {PI_EQE1_PATH} && echo ASSETS_OK")
    if "ASSETS_OK" not in proc.stdout:
        raise RuntimeError(f"Zone asset paths not reachable from Pi: {PI_EQ_PATH} / {PI_EQE1_PATH}")
    print(f"  {PI_EQ_PATH} and {PI_EQE1_PATH} both present on the Pi.")

    manifest = load_manifest()
    missing_eqe1 = []
    for zc in manifest["zones"]:
        zone = zc["zone"]
        for suffix in ("", "_obj", "_chr"):
            fname = f"{zone}{suffix}.eqe1"
            proc = ssh_run(f"test -f {PI_EQE1_PATH}/{fname} && echo YES || echo NO", check=False)
            if "YES" not in proc.stdout:
                missing_eqe1.append(fname)
    if missing_eqe1:
        print(f"[preflight] WARNING: missing EQE1 archives for golden zones: {missing_eqe1} "
              f"-- EQE1 A/B will be skipped for affected zones.")
    else:
        print("[preflight] EQE1 archives present for all golden zones (zone/_obj/_chr).")
    return manifest


def investigate_console_drm():
    """Document DRM device ownership / console state. Does NOT take any
    corrective action unless the device is actually busy (in which case we
    abort loudly rather than guess) -- see H04 review for why no chvt /
    getty-stop automation turned out to be necessary on this hardware."""
    print("[console/DRM] investigating current state...")
    proc = ssh_run(
        "echo '--who--'; who; "
        "echo '--active-vt--'; cat /sys/class/tty/tty0/active 2>&1; "
        "echo '--getty@tty1--'; systemctl is-active getty@tty1 2>&1; "
        "echo '--x-procs--'; (ps aux | grep -iE 'Xorg|weston|Xwayland' | grep -v grep) || echo none; "
        "echo '--drm-busy--'; sudo -n lsof /dev/dri/card0 /dev/dri/card1 2>&1 || echo 'lsof: nothing open'",
        check=False,
    )
    print(proc.stdout)
    busy = ("gles2_zone_benchmark" not in proc.stdout) and (
        "/dev/dri/card0" in proc.stdout and "COMMAND" in proc.stdout
    )
    if busy:
        raise RuntimeError(
            "DRM device appears to be held open by another process -- refusing to "
            "run. See the console/DRM investigation output above."
        )
    print("[console/DRM] no other process holds /dev/dri/{card0,card1} open; "
          "getty@tty1 running on tty1 does not block DRM master acquisition on "
          "this kernel/driver (sun4i-drm + lima) -- confirmed via smoke test. "
          "No chvt/getty automation performed or needed.")


def load_manifest():
    with open(MANIFEST_PATH) as f:
        return json.load(f)


def production_config_args(manifest=None):
    """Q01: shared production-lighting-config args (single source of truth:
    tests/goldens/manifest.json's top-level "productionConfig" block, also
    consumed by scripts/ci/visual_gate.py) -- mirrors the orangepi preset
    (adaptive count-mode lighting, zone budget 20) so the Pi tier-2 baseline
    measures what production actually ships instead of full per-pixel
    lighting. NOT added to any zone's extraArgs -- pose_flags_repeated()
    above asserts extraArgs is --camera-pose entries only."""
    if manifest is None:
        manifest = load_manifest()
    return list(manifest.get("productionConfig", {}).get("args", []))


def pose_flags_repeated(zone_cfg, total_frames):
    """Build a flat list of --camera-pose ... args, repeating the manifest's
    pose list (in order) until total_frames poses have been emitted. Frame i
    of the resulting run is pose (i % n_poses) of the manifest -- so metrics
    for designatedFrames repeat identically across the run and can be
    cross-checked against the manifest at any repeat offset."""
    extra = zone_cfg["extraArgs"]
    poses = []
    i = 0
    while i < len(extra):
        assert extra[i] == "--camera-pose"
        poses.append(extra[i + 1])
        i += 2
    n = len(poses)
    if n == 0:
        raise RuntimeError(f"zone {zone_cfg['zone']}: no --camera-pose entries in manifest extraArgs")
    reps = (total_frames + n - 1) // n
    flags = []
    for r in range(reps):
        for p in poses[: min(n, total_frames - r * n)]:
            flags += ["--camera-pose", p]
    return flags, n


class GovernorPin:
    """Context manager: pins the 'performance' cpufreq governor on all cores,
    restores the original governor(s) on exit (even on exception)."""

    def __enter__(self):
        proc = ssh_run(
            "for f in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; "
            "do cat $f; done"
        )
        self.original = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
        print(f"[governor] original per-cpu governors: {self.original}")
        ssh_run(
            "for f in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; "
            "do echo performance | sudo -n tee $f > /dev/null; done"
        )
        proc = ssh_run(
            "for f in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; "
            "do cat $f; done"
        )
        pinned = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
        print(f"[governor] pinned to: {pinned}")
        if any(g != "performance" for g in pinned):
            raise RuntimeError(f"failed to pin performance governor on all cores: {pinned}")
        return self

    def __exit__(self, exc_type, exc, tb):
        # Restore whatever each core's governor originally was.
        restore_val = self.original[0] if self.original else "ondemand"
        print(f"[governor] restoring to '{restore_val}' on all cores...")
        ssh_run(
            f"for f in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; "
            f"do echo {restore_val} | sudo -n tee $f > /dev/null; done",
            check=False,
        )
        proc = ssh_run(
            "for f in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; "
            "do cat $f; done",
            check=False,
        )
        restored = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
        print(f"[governor] after restore: {restored}")
        if restored != self.original:
            print(f"[governor] WARNING: post-restore state {restored} != original {self.original}")
        return False  # don't suppress exceptions


def strip_and_stage_binary(scratch_dir):
    """Strip a copy of the ARM benchmark binary and scp it to Pi /tmp so
    timed runs never page the (huge, unstripped) binary off NFS."""
    local_bin = ARM_BUILD_BIN_DIR / BENCH_LOCAL_NAME
    scratch_dir.mkdir(parents=True, exist_ok=True)
    staged = scratch_dir / BENCH_LOCAL_NAME
    shutil.copy2(local_bin, staged)
    orig_size = staged.stat().st_size
    strip_tool = shutil.which("arm-linux-gnueabihf-strip") or shutil.which("strip")
    if strip_tool is None:
        print("[stage] WARNING: no strip tool found locally -- staging unstripped binary "
              f"({orig_size} bytes); this violates the I/O-rule's spirit for timed runs.")
    else:
        subprocess.run([strip_tool, str(staged)], check=True)
        new_size = staged.stat().st_size
        print(f"[stage] stripped {BENCH_LOCAL_NAME}: {orig_size} -> {new_size} bytes "
              f"(tool: {strip_tool})")
    scp_to(staged, PI_REMOTE_BIN)
    ssh_run(f"chmod +x {PI_REMOTE_BIN}")
    proc = ssh_run(f"ls -la {PI_REMOTE_BIN}; df -h /tmp")
    print(proc.stdout)


def run_zone_bench_generic(zone_cfg, resolution, total_frames, extra_cli_args,
                            remote_csv, remote_log, label_extra=""):
    """General-purpose single benchmark invocation on the Pi (P02): same
    pose-repeat/I-O-rule machinery as run_zone_variant, but accepts arbitrary
    extra CLI args (e.g. --lighting-model, --dump-filters) instead of just
    the eqe1 on/off toggle. run_zone_variant (below) is now a thin wrapper
    over this for the eqe1-only case, kept for backward compatibility with
    existing callers/tests."""
    pose_flags, n_poses = pose_flags_repeated(zone_cfg, total_frames)
    # Q01: productionConfig args come first so a caller's own extra_cli_args
    # (e.g. an investigation script's --pp-budget-mode override) still wins
    # on any flag it repeats -- the benchmark's arg parser is last-flag-wins.
    cmd_parts = [
        PI_REMOTE_BIN,
        "--zone", zone_cfg["zone"],
        "--eq-path", PI_EQ_PATH,
    ] + production_config_args() + list(extra_cli_args) + [
        "--drm",
        "--res", str(resolution["width"]), str(resolution["height"]),
        "--csv", remote_csv,
    ] + pose_flags
    remote_cmd = " ".join(cmd_parts) + f" > {remote_log} 2>&1"
    t0 = time.time()
    proc = ssh_run(remote_cmd, timeout=180, check=False)
    dt = time.time() - t0
    if proc.returncode != 0:
        raise RuntimeError(
            f"benchmark failed on Pi (zone={zone_cfg['zone']}{label_extra}, "
            f"exit {proc.returncode}, wall {dt:.1f}s) -- see {remote_log} on the Pi.\n"
            f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
    print(f"  [{zone_cfg['zone']}{label_extra}] {n_poses} poses x{total_frames // n_poses} "
          f"= {total_frames} frames, wall {dt:.1f}s")
    return n_poses


def run_zone_variant(zone_cfg, resolution, total_frames, use_eqe1, remote_csv, remote_log):
    """One benchmark invocation on the Pi; ALL output stays under /tmp for the
    duration of the timed phase (I/O rule). Returns nothing -- caller pulls
    files back after all timed runs for this pass complete."""
    extra = ["--eqe1-path", PI_EQE1_PATH] if use_eqe1 else []
    run_zone_bench_generic(zone_cfg, resolution, total_frames, extra, remote_csv, remote_log,
                            label_extra=f" eqe1={use_eqe1}")


def read_csv_metrics(csv_path):
    with open(csv_path) as f:
        rows = list(csv.DictReader(f))
    return rows


def p95(values):
    if not values:
        return None
    values = sorted(values)
    idx = int(len(values) * 0.95)
    if idx >= len(values):
        idx = len(values) - 1
    return values[idx]


def summarize_multi_csv(csv_paths, n_poses, warmup_reps=None):
    """P02: pool steady-state frames across MULTIPLE independent benchmark
    invocations (repeats) of the same zone/variant -- each invocation
    already discards its own cold-start warmup cycle internally (see
    summarize_csv), so pooling their post-warmup rows gives an honest
    combined p95/mean/min/max plus per-rep numbers for transparency, without
    needing a single giant CSV. Returns the SAME dict shape as
    summarize_csv() (so pi_bench_compare.py's bless/compare code needs no
    changes) plus an extra 'reps' key: a list of each individual rep's
    total_ms_p95, for variance reporting."""
    per_rep_summaries = [summarize_csv(read_csv_metrics(p), n_poses, warmup_reps)
                         for p in csv_paths]
    # Pool steady rows across all reps for the combined stats.
    pooled_total_ms = []
    pooled_rows_all = []
    for p in csv_paths:
        rows = read_csv_metrics(p)
        warmup = (warmup_reps if warmup_reps is not None else H04_WARMUP_REPS) * n_poses
        steady = rows[warmup:] if len(rows) > warmup else rows
        pooled_total_ms.extend(float(r["total_ms"]) for r in steady)
        pooled_rows_all.extend(rows)

    per_pose = {}
    for i in range(n_poses):
        pose_rows = [r for j, r in enumerate(pooled_rows_all) if j % n_poses == i]
        draws = {int(r["draws"]) for r in pose_rows}
        states = {int(r["state_changes"]) for r in pose_rows}
        polys = {int(r["polys"]) for r in pose_rows}
        per_pose[i] = {
            "draws_set": sorted(draws),
            "state_changes_set": sorted(states),
            "polys_set": sorted(polys),
            "deterministic": len(draws) == 1 and len(states) == 1 and len(polys) == 1,
            "draws": next(iter(draws)) if len(draws) == 1 else None,
            "state_changes": next(iter(states)) if len(states) == 1 else None,
            "polys": next(iter(polys)) if len(polys) == 1 else None,
        }

    p95_ms = p95(pooled_total_ms)
    return {
        "n_frames": len(pooled_rows_all),
        "n_reps": len(csv_paths),
        "warmup_frames_discarded": (warmup_reps if warmup_reps is not None else H04_WARMUP_REPS) * n_poses * len(csv_paths),
        "n_frames_steady": len(pooled_total_ms),
        "total_ms_p95": p95_ms,
        "total_ms_mean": sum(pooled_total_ms) / len(pooled_total_ms) if pooled_total_ms else None,
        "total_ms_min": min(pooled_total_ms) if pooled_total_ms else None,
        "total_ms_max": max(pooled_total_ms) if pooled_total_ms else None,
        "fps_p95": 1000.0 / p95_ms if p95_ms else None,
        "per_pose": per_pose,
        "reps": [s["total_ms_p95"] for s in per_rep_summaries],
    }


def summarize_csv(rows, n_poses, warmup_reps=None):
    """Returns dict: overall p95/mean/min/max total_ms + fps (computed AFTER
    discarding the first warmup_reps pose-cycles -- cold shader/texture
    cache and GPU DVFS ramp-up make the first cycle consistently slower, see
    H04 review), plus per designated-frame-position (0..n_poses-1)
    draws/state_changes/polys (must be identical across ALL repeats,
    warmup included, since the scene is deterministic -- so the
    determinism check still runs over the full row set)."""
    if warmup_reps is None:
        warmup_reps = H04_WARMUP_REPS
    warmup_frames = warmup_reps * n_poses

    per_pose = {}
    for i in range(n_poses):
        pose_rows = [r for j, r in enumerate(rows) if j % n_poses == i]
        draws = {int(r["draws"]) for r in pose_rows}
        states = {int(r["state_changes"]) for r in pose_rows}
        polys = {int(r["polys"]) for r in pose_rows}
        per_pose[i] = {
            "draws_set": sorted(draws),
            "state_changes_set": sorted(states),
            "polys_set": sorted(polys),
            "deterministic": len(draws) == 1 and len(states) == 1 and len(polys) == 1,
            "draws": next(iter(draws)) if len(draws) == 1 else None,
            "state_changes": next(iter(states)) if len(states) == 1 else None,
            "polys": next(iter(polys)) if len(polys) == 1 else None,
        }

    steady_rows = rows[warmup_frames:] if len(rows) > warmup_frames else rows
    total_ms = [float(r["total_ms"]) for r in steady_rows]
    p95_ms = p95(total_ms)
    return {
        "n_frames": len(rows),
        "warmup_frames_discarded": warmup_frames,
        "n_frames_steady": len(steady_rows),
        "total_ms_p95": p95_ms,
        "total_ms_mean": sum(total_ms) / len(total_ms),
        "total_ms_min": min(total_ms),
        "total_ms_max": max(total_ms),
        "fps_p95": 1000.0 / p95_ms if p95_ms else None,
        "per_pose": per_pose,
    }
