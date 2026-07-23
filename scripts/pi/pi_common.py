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


def run_zone_variant(zone_cfg, resolution, total_frames, use_eqe1, remote_csv, remote_log):
    """One benchmark invocation on the Pi; ALL output stays under /tmp for the
    duration of the timed phase (I/O rule). Returns nothing -- caller pulls
    files back after all timed runs for this pass complete."""
    pose_flags, n_poses = pose_flags_repeated(zone_cfg, total_frames)
    cmd_parts = [
        PI_REMOTE_BIN,
        "--zone", zone_cfg["zone"],
        "--eq-path", PI_EQ_PATH,
    ]
    if use_eqe1:
        cmd_parts += ["--eqe1-path", PI_EQE1_PATH]
    cmd_parts += [
        "--drm",
        "--res", str(resolution["width"]), str(resolution["height"]),
        "--csv", remote_csv,
    ] + pose_flags
    remote_cmd = " ".join(cmd_parts) + f" > {remote_log} 2>&1"
    t0 = time.time()
    proc = ssh_run(remote_cmd, timeout=180, check=False)
    dt = time.time() - t0
    if proc.returncode != 0:
        # Pull the log back immediately for diagnosis (small file, and the
        # timed phase for THIS invocation already ended/failed).
        raise RuntimeError(
            f"benchmark failed on Pi (zone={zone_cfg['zone']} eqe1={use_eqe1}, "
            f"exit {proc.returncode}, wall {dt:.1f}s) -- see {remote_log} on the Pi.\n"
            f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
    print(f"  [{zone_cfg['zone']} eqe1={use_eqe1}] {n_poses} poses x{total_frames // n_poses} "
          f"= {total_frames} frames, wall {dt:.1f}s")


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
