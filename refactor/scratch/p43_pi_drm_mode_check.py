#!/usr/bin/env python3
"""P43: minimal Pi invocation to check DRM/KMS mode selection at 400x300 vs
800x600. Hypothesis (from reading docker/irrlicht-drm/CIrrDeviceFB.cpp
~L330-376): if the connector has no EXACT mode matching --res, the device
silently falls back to the preferred mode and mode_.hdisplay/vdisplay
(NOT the requested resolution) become the device's Width/Height -- which
would explain both the resolution-dependent geometry counts (frustum/
occlusion computed from the FALLBACK resolution's aspect/size, not the
requested one) and the GPU-time inversion (actual rendered pixel count is
the fallback mode's, not the nominal "quarter of the pixels").

x86 (Phase A) already ruled out a flag/config delta: matching P38/P41's
exact invocation (production args + --animate + seed 42 + shipped preset)
on llvmpipe/no-DRM still produced byte-identical counters across
resolutions. So this Pi-only mechanism is the remaining candidate.

Short runs only (--frames 5, no full 1000-frame timed measurement) --
this is a log-capture / mode-identification probe, not a timing run.
"""
import os
import sys
import time
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
os.environ.setdefault("ARM_BUILD_BIN_DIR", str(PROJECT_DIR / "build-arm-noble" / "bin"))
os.environ.setdefault("PROJECT_DIR", str(PROJECT_DIR))
os.environ.setdefault("MANIFEST_PATH", str(PROJECT_DIR / "tests/goldens/manifest.json"))
os.environ.setdefault("ARTIFACTS_DIR", str(PROJECT_DIR / "benchmarks/pi_runs"))
os.environ.setdefault("BASELINE_DIR", str(PROJECT_DIR / "benchmarks/pi_baseline"))
os.environ.setdefault("PI_HOST", "10.0.30.192")
os.environ.setdefault("PI_USER", "user")
os.environ.setdefault("PI_SSH_OPTS", "-o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new")
os.environ.setdefault("PI_EQ_PATH", "/mnt/projects/EverQuestP1999")
os.environ.setdefault("PI_EQE1_PATH", "/mnt/projects/EverQuest_ETC1")
os.environ.setdefault("PI_REMOTE_TMP", "/tmp")
os.environ.setdefault("PI_REMOTE_BIN", "/tmp/gles2_zone_benchmark_p43")
os.environ.setdefault("H04_TOTAL_FRAMES", "5")
os.environ.setdefault("H04_COOLDOWN_SEC", "5")
os.environ.setdefault("H04_WARMUP_REPS", "1")

sys.path.insert(0, str(PROJECT_DIR / "scripts" / "pi"))
import pi_common as pc  # noqa: E402

OUT_DIR = PROJECT_DIR / "benchmarks" / "pi_runs" / "p43_res_visibility"
OUT_DIR.mkdir(parents=True, exist_ok=True)
SCRATCH = Path("/tmp/p43_pi_scratch")

PROD_ARGS = pc.production_config_args()
BEFALLEN_PATH = PROJECT_DIR / "benchmarks/paths/befallen_seed42_1000f.path"
SHIPPED_PRESET_ARGS = ["--merge-zone-draws", "--composite-scenegraph",
                       "--eqe1-path", pc.PI_EQE1_PATH]


def run_probe(label, res, frames=5):
    remote_csv = f"/tmp/p43_{label}.csv"
    remote_log = f"/tmp/p43_{label}.log"
    remote_path_file = "/tmp/p43_befallen.path"
    pc.scp_to(BEFALLEN_PATH, remote_path_file)
    cmd = [pc.PI_REMOTE_BIN, "--zone", "befallen", "--eq-path", pc.PI_EQ_PATH,
           "--path-file", remote_path_file, "--frames", str(frames)]
    cmd += PROD_ARGS
    cmd += ["--entities", "5", "--seed", "42", "--animate"]
    cmd += SHIPPED_PRESET_ARGS
    # era-matched: occluder budget was hardcoded 16 pre-P42, explicit here so
    # this probe's geometry counts are comparable to P38/P41's original numbers
    cmd += ["--occluder-budget", "16"]
    cmd += ["--drm", "--res", str(res[0]), str(res[1]), "--gl-counters",
            "--csv", remote_csv]
    remote_cmd = " ".join(cmd) + f" > {remote_log} 2>&1"
    t0 = time.time()
    proc = pc.ssh_run(remote_cmd, timeout=120, check=False)
    dt = time.time() - t0
    if proc.returncode != 0:
        raise RuntimeError(f"Pi run failed ({label}), exit {proc.returncode}, wall {dt:.1f}s:\n"
                            f"{proc.stdout}\n{proc.stderr}")
    print(f"  [{label}] wall {dt:.1f}s")
    # Pull back the log (has the DRM mode-selection lines) and csv
    pc.rsync_from(remote_log, OUT_DIR)
    pc.rsync_from(remote_csv, OUT_DIR)
    return OUT_DIR / Path(remote_log).name


def main():
    print("[preflight] checking Pi reachability + binary staging...")
    proc = pc.ssh_run("echo OK && uname -a")
    print(f"  {proc.stdout.strip()}")
    local_bin = pc.ARM_BUILD_BIN_DIR / pc.BENCH_LOCAL_NAME
    if not local_bin.exists():
        raise RuntimeError(f"ARM binary not found at {local_bin}")
    print(f"  using ARM binary: {local_bin}")

    print("[preflight] CmaFree gate...")
    info = pc.read_pi_meminfo()
    print(f"  CmaFree={info.get('CmaFree')} kB MemAvailable={info.get('MemAvailable')} kB")
    if info.get("CmaFree", 0) < pc.CMA_FREE_ABORT_KB:
        print("[preflight] CmaFree low -- remediating with sync + drop_caches (no reboot)...")
        pc.ssh_run("sync && echo 1 | sudo -n tee /proc/sys/vm/drop_caches > /dev/null", check=False)
        info = pc.read_pi_meminfo()
        print(f"  post-remediation CmaFree={info.get('CmaFree')} kB")
        if info.get("CmaFree", 0) < pc.CMA_FREE_ABORT_KB:
            raise RuntimeError("CmaFree still below abort threshold after remediation -- STOP, do not proceed")

    pc.strip_and_stage_binary(SCRATCH)

    # Also dump the connector's raw mode list once (no benchmark run needed) --
    # cheap, informational, helps interpret the per-run logs below.
    print("\n[drm modes] querying connector mode list independent of any run...")
    proc = pc.ssh_run("cat /sys/class/drm/*/modes 2>/dev/null; "
                       "for f in /sys/class/drm/card*-*/status; do echo \"$f: $(cat $f)\"; done",
                       check=False)
    print(proc.stdout)

    runner = pc.SessionRunner(cma_log_path=str(OUT_DIR / "cma_telemetry.tsv"))
    governor = pc.GovernorPin()
    governor.__enter__()
    try:
        for label, res in [("res_400x300", (400, 300)), ("res_800x600", (800, 600))]:
            runner.before_invocation(label)
            log_path = run_probe(label, res)
            runner.after_invocation(label)
            print(f"\n--- {label} DRM log lines ---")
            for line in log_path.read_text(errors="replace").splitlines():
                if "DRM" in line or "drm" in line:
                    print(" ", line)
            time.sleep(5)
    except pc.SessionAbort as e:
        governor.__exit__(None, None, None)
        print(f"[ABORT] {e}")
        raise
    except BaseException:
        governor.__exit__(None, None, None)
        raise
    else:
        governor.__exit__(None, None, None)

    print(f"\n[session] {runner.count} invocations completed, CmaFree samples: "
          f"{[s[1] for s in runner.cma.samples]}")
    print("\nDone. Logs/CSVs in", OUT_DIR)


if __name__ == "__main__":
    main()
