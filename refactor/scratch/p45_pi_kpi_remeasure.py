#!/usr/bin/env python3
"""P45 Phase 2: post-P37 KPI re-measure + real-roster P37 attribution A/B.

Amended plan (orchestrator, after Phase 1's critical finding that the
canonical e205 replay's DEFAULT race cycle cannot exercise P37's exclusion
at all -- see the P45 plan Review):

  Run 1-2: canonical e205 shipped replay (tip binary, a805e01, P37+P42) --
           baseline continuity vs P14's 32.313ms / P39's 32.5ms p95, AND the
           first Pi regression spot for P42's driver state-dedup change.
  Run 3:   e205 replay with a ZOM-heavy realistic befallen roster via
           --test-races (tip binary, P37 exclusions ACTIVE).
  Run 4:   the SAME real-roster invocation with the pre-P37 ARM binary
           (fda2e34, P37's actual merge-parent commit -- 0019ba6^1; see the
           Review for why this differs from the orchestrator's cited
           e8efd87^, an earlier ancestor also pre-P37 but not the immediate
           parent) -- the direct A/B answering P37's real KPI cost.
  Run 5:   reserve, not spent unless something above is ambiguous.

Runs 1-2 stay strictly comparable to P14/P39 (same invocation shape those
units used). Runs 3-4 are a NEW invocation shape (--test-races has no
P14/P39 precedent) -- they answer the P37 question only against EACH OTHER,
never against the 32.313/32.5ms baselines. Keep these two comparisons
separated in the verdict table (plan instruction).

Roster for runs 3-4 (ZOM-heavy, --test-races 67,67,67,67,34,17,35,89,53,33,31):
built on P08f's already-validated "8 zone-native befallen races" set
(34=BAT,17=ORC,35=RAT,67=ZOM,89=IMP,53=MIN,33=GHU,31=FPM -- all confirmed to
resolve cleanly via preloadModelData in Phase 1's x86 smoke tests, both pre-
and post-P37), with race 67 (ZOM) repeated 4x (4/11 ~ 36% of the cycle) to
reflect befallen's "undead crypt" theme and the orchestrator's explicit
"weight toward what a player actually sees, ZOM-heavy" instruction. CAZ and
DEN (2 of P37's other 3 excluded befallen races) are NOT included: this
codebase's race_codes.cpp has no numeric-ID mapping for those 3-letter
codes (grepped, absent), and P08f's own set never resolved them either --
inventing untested IDs risked a silent wrong-race resolution, not worth the
risk against a fixed 5-invocation Pi budget. This is a documented
approximation, not a live spawn2 frequency reproduction (querying eqemu is
out of this unit's territory -- "No eqemu" per the plan header).

IMPORTANT: overrides ARM_BUILD_BIN_DIR to THIS worktree's build-arm-noble/bin
before importing pi_common -- same trap P14/P39/P41 documented. Run 4
monkey-patches pc.ARM_BUILD_BIN_DIR to the pre-P37 scratch worktree
(../slot3-p37pre/build-arm-noble/bin) and re-stages before that one
invocation only.
"""
import csv
import os
import re
import sys
import time
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
PRE_P37_BIN_DIR = PROJECT_DIR.parent / "slot3-p37pre" / "build-arm-noble" / "bin"
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
os.environ.setdefault("PI_REMOTE_BIN", "/tmp/gles2_zone_benchmark_p45")
os.environ.setdefault("H04_TOTAL_FRAMES", "1000")
os.environ.setdefault("H04_COOLDOWN_SEC", "15")
os.environ.setdefault("H04_WARMUP_REPS", "1")

sys.path.insert(0, str(PROJECT_DIR / "scripts" / "pi"))
import pi_common as pc  # noqa: E402

OUT_DIR = PROJECT_DIR / "benchmarks" / "pi_runs" / "p45_p37_kpi"
OUT_DIR.mkdir(parents=True, exist_ok=True)
SCRATCH = Path("/tmp/p45_pi_scratch")

PROD_ARGS = pc.production_config_args()
RES = (800, 600)
BEFALLEN_PATH = PROJECT_DIR / "benchmarks/paths/befallen_seed42_1000f.path"
STRESS_N = 205  # P11-era stress calibration, reused P14/P39/P41
SHIPPED_PRESET_ARGS = ["--animate", "--merge-zone-draws", "--composite-scenegraph",
                       "--eqe1-path", pc.PI_EQE1_PATH]
ZOM_HEAVY_TEST_RACES = "67,67,67,67,34,17,35,89,53,33,31"

P14_P95_MS = 32.313  # refactor/P14_occluder_bf472.md
P39_P95_MS = 32.5    # refactor/P39_occluder_reland.md re-land spot check
KPI_LINE_MS = 33.3   # 30fps line


def run_replay(label, extra_args, frames=1000, entities=STRESS_N, gl_counters=True):
    remote_csv = f"/tmp/p45_befallen_{label}.csv"
    remote_log = f"/tmp/p45_befallen_{label}.log"
    remote_path_file = "/tmp/p45_befallen.path"
    pc.scp_to(BEFALLEN_PATH, remote_path_file)
    cmd = [pc.PI_REMOTE_BIN, "--zone", "befallen", "--eq-path", pc.PI_EQ_PATH,
           "--path-file", remote_path_file, "--frames", str(frames)] + PROD_ARGS
    if entities is not None:
        cmd += ["--entities", str(entities), "--seed", "42"]
    cmd += extra_args
    cmd += ["--drm", "--res", str(RES[0]), str(RES[1])]
    if gl_counters:
        # --gl-counters is P38-era; the pre-P37 binary (fda2e34) predates it
        # and rejects the flag outright ("Unknown option") -- caller must
        # pass gl_counters=False for that binary. Only adds tex_binds/
        # tex_param_calls CSV columns when present; draws/total_ms/etc are
        # unaffected either way.
        cmd += ["--gl-counters"]
    cmd += ["--log-level=INFO", "--csv", remote_csv]
    remote_cmd = " ".join(cmd) + f" > {remote_log} 2>&1"
    t0 = time.time()
    proc = pc.ssh_run(remote_cmd, timeout=300, check=False)
    dt = time.time() - t0
    if proc.returncode != 0:
        raise RuntimeError(f"Pi run failed (befallen/{label}), exit {proc.returncode}, wall {dt:.1f}s:\n"
                            f"{proc.stdout}\n{proc.stderr}")
    print(f"  [befallen/{label}] wall {dt:.1f}s")
    local_csv = OUT_DIR / f"befallen_{label}.csv"
    local_log = OUT_DIR / f"befallen_{label}.log"
    pc.rsync_from(remote_csv, OUT_DIR)
    pc.rsync_from(remote_log, OUT_DIR)
    pulled = OUT_DIR / Path(remote_csv).name
    if pulled != local_csv and pulled.exists():
        pulled.rename(local_csv)
    pulled_log = OUT_DIR / Path(remote_log).name
    if pulled_log != local_log and pulled_log.exists():
        pulled_log.rename(local_log)
    return local_csv, local_log


def check_drm_mode(log_path, expected=f"{RES[0]}x{RES[1]}"):
    """P43's root-cause finding: the DRM device silently substitutes the
    connector's preferred mode (2560x1440) when --res has no exact match.
    Grep the run's own log for the driver's "Found matching mode" line
    (docker/irrlicht-drm/CIrrDeviceFB.cpp) and confirm it names our
    requested resolution, not a silent substitution."""
    text = Path(log_path).read_text(errors="replace")
    m = re.search(r"DRM: Found matching mode (\d+x\d+)@(\d+)Hz for requested (\d+x\d+)", text)
    if not m:
        print(f"  [DRM-MODE] WARNING: no 'Found matching mode' line in {log_path} -- "
              f"cannot confirm resolution, treat this run's data as suspect.")
        return False
    matched, hz, requested = m.group(1), m.group(2), m.group(3)
    if matched != expected or requested != expected:
        print(f"  [DRM-MODE] *** SILENT MODE SUBSTITUTION *** matched={matched} "
              f"requested={requested} (expected {expected}) -- per P43's finding, "
              f"this run's data does not reflect the intended resolution.")
        return False
    print(f"  [DRM-MODE] confirmed {matched}@{hz}Hz for requested {requested} -- OK")
    return True


def summarize(csv_path):
    with open(csv_path) as f:
        rows = list(csv.DictReader(f))

    def col(name, cast=float):
        return [cast(r[name]) for r in rows]

    def p95_of(vals):
        s = sorted(vals)
        idx = min(len(s) - 1, int(len(s) * 0.95))
        return s[idx]

    out = {"n_frames": len(rows)}
    for name in ["total_ms", "submission_ms", "gpu_ms", "collect_cull_ms", "swap_ms"]:
        vals = col(name)
        out[f"{name}_avg"] = sum(vals) / len(vals)
        out[f"{name}_p95"] = p95_of(vals)
    for name in ["draws", "polys"]:
        vals = col(name, int)
        out[f"{name}_avg"] = sum(vals) / len(vals)
    return out


def sanity_check(label, summary, expect="composite"):
    """P11b's missing --eqe1-path lesson, extended for runs 3/4: the
    DIRECTION draws_avg must move differs per leg.
      - runs 1/2 (default cycle, all-composite regardless of P37):
        expect ~composite-range draws_avg (P11b: 287.7), NOT multibuffer
        (710.1) -- a multibuffer-range number here means eqe1/composite
        silently no-op'd, not a real signal.
      - run 3 (real roster, tip/post-P37): expect draws_avg ABOVE runs 1/2's
        (ZOM/BAT/ORC reverted to multibuffer) but still below full
        multibuffer (most of the 11-race cycle is still composite-eligible).
      - run 4 (real roster, pre-P37 binary): expect draws_avg BELOW run 3's
        (same roster, but ZOM/BAT/ORC composite instead of multibuffer) --
        this is the direct P37 cost signal.
    expect: "composite" (runs 1/2), "mixed" (run 3), "mixed_lower" (run 4,
    caller must compare against run 3's own number separately).
    """
    draws_avg = summary["draws_avg"]
    if expect == "composite":
        if draws_avg > 500:
            print(f"  [SANITY:{label}] draws_avg={draws_avg:.1f} is multibuffer-range "
                  f"(P11b: 710.1) -- composite may have silently no-op'd. DO NOT TRUST.")
            return False
        print(f"  [SANITY:{label}] draws_avg={draws_avg:.1f} composite-range (P11b: 287.7) -- OK")
        return True
    # runs 3/4: just report, direction is checked by the caller comparing
    # run3 vs run4 against each other (no fixed P11b reference for this
    # roster shape).
    print(f"  [SANITY:{label}] draws_avg={draws_avg:.1f} (real-roster leg, compare vs its pair)")
    return True


def main():
    print("[preflight] full P02 preflight (reachability, asset paths, EQE1 archives)...")
    pc.preflight()
    pc.investigate_console_drm()
    print("[preflight] checking Pi reachability + binary staging (tip)...")
    proc = pc.ssh_run("echo OK && uname -a")
    print(f"  {proc.stdout.strip()}")
    local_bin = pc.ARM_BUILD_BIN_DIR / pc.BENCH_LOCAL_NAME
    if not local_bin.exists():
        raise RuntimeError(f"ARM binary not found at {local_bin} -- run scripts/build-arm-noble.sh "
                            "in THIS worktree first")
    print(f"  tip ARM binary: {local_bin}")
    pre_p37_bin = PRE_P37_BIN_DIR / pc.BENCH_LOCAL_NAME
    if not pre_p37_bin.exists():
        raise RuntimeError(f"pre-P37 ARM binary not found at {pre_p37_bin} -- build it in "
                            "../slot3-p37pre first")
    print(f"  pre-P37 ARM binary: {pre_p37_bin}")

    runner = pc.SessionRunner(cma_log_path=str(OUT_DIR / "cma_telemetry.tsv"))
    governor = pc.GovernorPin()
    governor.__enter__()
    results = {}
    drm_ok = {}
    try:
        # --- tip binary staged once, covers runs 1, 2, 3 ---
        pc.strip_and_stage_binary(SCRATCH)

        tip_plan = [
            ("e205_shipped_run1", SHIPPED_PRESET_ARGS, "composite"),
            ("e205_shipped_run2", SHIPPED_PRESET_ARGS, "composite"),
            ("e205_realroster_tip", SHIPPED_PRESET_ARGS + ["--test-races", ZOM_HEAVY_TEST_RACES], "mixed"),
        ]
        for label, extra_args, expect in tip_plan:
            runner.before_invocation(label)
            csv_path, log_path = run_replay(label, extra_args)
            runner.after_invocation(label)
            s = summarize(csv_path)
            sanity_check(label, s, expect=expect)
            drm_ok[label] = check_drm_mode(log_path)
            results[label] = s
            time.sleep(pc.H04_COOLDOWN_SEC)

        # --- re-stage pre-P37 binary for run 4 ---
        print("\n[re-stage] switching to pre-P37 binary for run 4...")
        pc.ARM_BUILD_BIN_DIR = PRE_P37_BIN_DIR
        pc.strip_and_stage_binary(SCRATCH)

        label = "e205_realroster_preP37"
        runner.before_invocation(label)
        csv_path, log_path = run_replay(label, SHIPPED_PRESET_ARGS + ["--test-races", ZOM_HEAVY_TEST_RACES],
                                         gl_counters=False)
        runner.after_invocation(label)
        s = summarize(csv_path)
        sanity_check(label, s, expect="mixed_lower")
        drm_ok[label] = check_drm_mode(log_path)
        results[label] = s
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

    print("\n=== KPI-verdict table: runs 1-2 vs P14 (32.313ms p95) / P39 (32.5ms p95) / "
          "30fps line (33.3ms) -- baseline-comparable ===")
    for key in ("e205_shipped_run1", "e205_shipped_run2"):
        s = results[key]
        flag = " *** OVER 33.3ms LINE ***" if s["total_ms_p95"] > KPI_LINE_MS else ""
        print(f"{key:24s} n={s['n_frames']:5d} total_avg={s['total_ms_avg']:7.3f} "
              f"total_p95={s['total_ms_p95']:7.3f}{flag} gpu_avg={s['gpu_ms_avg']:7.3f} "
              f"gpu_p95={s['gpu_ms_p95']:7.3f} sub_avg={s['submission_ms_avg']:7.3f} "
              f"sub_p95={s['submission_ms_p95']:7.3f} draws_avg={s['draws_avg']:6.1f} "
              f"polys_avg={s['polys_avg']:8.1f} drm_mode_ok={drm_ok[key]}")

    print("\n=== P37-attribution table: run 3 (tip, real roster) vs run 4 (pre-P37, same "
          "roster) -- NEW invocation shape, comparable ONLY to each other, NOT to P14/P39 ===")
    for key in ("e205_realroster_tip", "e205_realroster_preP37"):
        s = results[key]
        print(f"{key:24s} n={s['n_frames']:5d} total_avg={s['total_ms_avg']:7.3f} "
              f"total_p95={s['total_ms_p95']:7.3f} gpu_avg={s['gpu_ms_avg']:7.3f} "
              f"gpu_p95={s['gpu_ms_p95']:7.3f} sub_avg={s['submission_ms_avg']:7.3f} "
              f"sub_p95={s['submission_ms_p95']:7.3f} draws_avg={s['draws_avg']:6.1f} "
              f"polys_avg={s['polys_avg']:8.1f} drm_mode_ok={drm_ok[key]}")
    tip_s, pre_s = results["e205_realroster_tip"], results["e205_realroster_preP37"]
    print(f"\nP37 cost at ZOM-heavy real roster: draws_avg {pre_s['draws_avg']:.1f} (pre-P37) "
          f"-> {tip_s['draws_avg']:.1f} (tip) = {tip_s['draws_avg'] - pre_s['draws_avg']:+.1f} draws/frame; "
          f"total_ms p95 {pre_s['total_ms_p95']:.3f} -> {tip_s['total_ms_p95']:.3f} = "
          f"{tip_s['total_ms_p95'] - pre_s['total_ms_p95']:+.3f}ms")

    print("\nCSVs/logs written to", OUT_DIR)
    print("\nRun 5 reserved -- not spent unless the above is ambiguous.")


if __name__ == "__main__":
    main()
