#!/usr/bin/env python3
"""P52 Phase 2 RECOVERY -- the original 6-invocation matrix
(p52_pi_gpu_sync_matrix.py) used pi_common.run_zone_bench_generic(), which
builds --camera-pose flags from the manifest's static-pose fixtures. That
routes gles2_zone_benchmark.cpp into its SEPARATE static-pose-capture loop
(main(), `if (!opts.cameraPoses.empty())`), which does NOT run the
per-frame instrumentation this unit added (nor the pre-existing P08 phase
timing / P38 GL counters) -- all of that lives in the moving-path loop
(driven by --path-file or the navmesh/orbit fallback), confirmed by
diffing against P51's own --path-file CSVs which DO have real
collect_cull_ms/tex_binds/entities_visible values. All 6 original runs are
a write-off for GPU-sync data (CSVs kept for the record, not used below).

Budget-constrained recovery: 2 invocations (all that remains of the ≤8
ceiling), spent on the two highest-value legs -- canonical e5 and stress
e205, both via --path-file (same loop, same convention P51's
scaling probe used), matching P51's own scaling-probe shape so the
resulting entities-visible/tex_binds/collect_cull_ms are directly
comparable to P51's numbers.
"""
import csv
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "scripts" / "pi"))
import pi_common as pc  # noqa: E402

OUT_DIR = Path(pc.PROJECT_DIR) / "benchmarks" / "pi_runs" / "p52_gpu_sync"
OUT_DIR.mkdir(parents=True, exist_ok=True)
PATH_FILE = Path(pc.PROJECT_DIR) / "benchmarks" / "paths" / "befallen_fixedpose_300f.path"
REMOTE_PATH_FILE = "/tmp/p52_befallen_fixedpose_300f.path"
RES = (800, 600)
PROD_ARGS = pc.production_config_args()


def run_one(label, entities, extra_flags):
    remote_csv = f"/tmp/p52_{label}.csv"
    remote_log = f"/tmp/p52_{label}.log"
    cmd = [pc.PI_REMOTE_BIN, "--zone", "befallen", "--eq-path", pc.PI_EQ_PATH,
           "--path-file", REMOTE_PATH_FILE, "--frames", "300"] + PROD_ARGS
    cmd += ["--entities", str(entities), "--seed", "42", "--animate"]
    cmd += extra_flags
    cmd += ["--drm", "--res", str(RES[0]), str(RES[1]), "--log-level=INFO", "--csv", remote_csv]
    remote_cmd = " ".join(cmd) + f" > {remote_log} 2>&1"
    t0 = time.time()
    proc = pc.ssh_run(remote_cmd, timeout=180, check=False)
    dt = time.time() - t0
    print(f"  [{label}] wall {dt:.1f}s exit={proc.returncode}")
    if proc.returncode != 0:
        raise RuntimeError(f"Pi run failed ({label}), exit {proc.returncode}, wall {dt:.1f}s:\n{proc.stdout}\n{proc.stderr}")
    pc.rsync_from(remote_csv, OUT_DIR)
    pc.rsync_from(remote_log, OUT_DIR)


def check_drm_mode(log_path):
    text = Path(log_path).read_text(errors="replace")
    m = re.search(r"DRM: Found matching mode (\d+x\d+)@(\d+)Hz for requested (\d+x\d+)", text)
    expected = f"{RES[0]}x{RES[1]}"
    if not m:
        print(f"  [DRM-MODE] WARNING: no confirmation line in {log_path}")
        return
    ok = m.group(1) == expected and m.group(3) == expected
    print(f"  [DRM-MODE] {'confirmed' if ok else '*** SILENT MODE SUBSTITUTION ***'} "
          f"{m.group(1)}@{m.group(2)}Hz for requested {m.group(3)}")


def main():
    manifest = pc.preflight()
    pc.investigate_console_drm()
    scratch = Path("/tmp/p52_gpu_sync_stage")
    pc.strip_and_stage_binary(scratch)
    pc.scp_to(PATH_FILE, REMOTE_PATH_FILE)

    cma = pc.CmaMonitor(log_path=OUT_DIR / "cma_telemetry_pathfile_fix.tsv")
    governor = pc.GovernorPin()
    governor.__enter__()
    try:
        cma.sample("A_canonical_e5_pathfile (pre)")
        run_one("A_canonical_e5_pathfile", 5, ["--gpu-sync-probe", "--gl-counters"])
        cma.sample("A_canonical_e5_pathfile (post)")
        check_drm_mode(OUT_DIR / "p52_A_canonical_e5_pathfile.log")
        time.sleep(15)

        cma.sample("B_stress_e205_pathfile (pre)")
        run_one("B_stress_e205_pathfile", 205, ["--gpu-sync-probe"])
        cma.sample("B_stress_e205_pathfile (post)")
        check_drm_mode(OUT_DIR / "p52_B_stress_e205_pathfile.log")
    finally:
        governor.__exit__(None, None, None)

    print("\n=== P52 path-file recovery: 2 invocations complete ===")


if __name__ == "__main__":
    main()
