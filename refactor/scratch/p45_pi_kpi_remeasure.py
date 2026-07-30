#!/usr/bin/env python3
"""P45 Phase 2: post-P37 KPI re-measure at befallen e205 (STOP-gated on Pi
release from P43 -- do NOT run until the orchestrator confirms).

Mirrors P14/P39/P41's exact "shipped orangepi preset" full-stack invocation
(same replay file, same --entities 205/--seed 42 stress calibration, same
production-config args) so the two whole-run p95 numbers this unit produces
are directly comparable to P14's blessed 32.313ms and P39's re-land spot of
32.5ms.

CRITICAL CAVEAT (found during Phase 1 staging, see P45 Review): this replay's
entity roster is `gles2_zone_benchmark`'s DEFAULT race cycle
(kDefaultRaceIds = EQ race IDs 1-12, generic human-shaped player races:
Human/Barbarian/Erudite/WoodElf/HighElf/DarkElf/HalfElf/Dwarf/Troll/Ogre/
Halfling/Gnome) -- NOT befallen's real zone-native creature roster (ZOM=67,
BAT=34, ORC=17, SKE=21, etc, P08f's --test-races set). P37 excludes ONLY
zone-native creature races with WLD-masked textures (BAT/CAZ/DEN/ORC/ZOM in
befallen + SKE globally) from composite eligibility. None of those race IDs
are in the default cycle, and this script (matching P14/P39/P41's own
invocations) does NOT pass --test-races. Prediction: draws_avg/p95 on this
exact "canonical replay" will show ~0 delta from P14/P39 baselines --
this is expected, is NOT evidence P37 is free of KPI impact, and must be
reported as a benchmark-coverage gap, not a clean bill of health. See the
plan Review for the arithmetic on what WOULD move if a --test-races variant
matching befallen's real roster were run instead (not staged as an
invocation here -- would need explicit orchestrator/Will sign-off given the
2-invocation budget in the plan).

IMPORTANT: overrides ARM_BUILD_BIN_DIR to THIS worktree's build-arm-noble/bin
(the pi_common.py default points at /mnt/projects/fable/main) before
importing pi_common -- same trap P14/P39/P41 documented.
"""
import csv
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
# Shipped preset, exactly as the client ships it: merge + composite +
# eqe1-path. --occluder-budget intentionally OMITTED (post-P42 the tool
# default resolves to 256 from ConstrainedRendererConfig, the P14-blessed
# value -- passing it explicitly would be redundant, not wrong, but the
# whole point of "exactly as shipped" is exercising the same code path
# production takes).
SHIPPED_PRESET_ARGS = ["--animate", "--merge-zone-draws", "--composite-scenegraph",
                       "--eqe1-path", pc.PI_EQE1_PATH]

# P14/P39 baselines this unit is attributing against (whole-run, budget 256
# post-P42-default, full stack, 800x600, e205):
P14_P95_MS = 32.313  # refactor/P14_occluder_bf472.md
P39_P95_MS = 32.5    # refactor/P39_occluder_reland.md re-land spot check
KPI_LINE_MS = 33.3   # 30fps line


def run_replay(label, extra_args, frames=1000, entities=STRESS_N):
    remote_csv = f"/tmp/p45_befallen_{label}.csv"
    remote_log = f"/tmp/p45_befallen_{label}.log"
    remote_path_file = "/tmp/p45_befallen.path"
    pc.scp_to(BEFALLEN_PATH, remote_path_file)
    cmd = [pc.PI_REMOTE_BIN, "--zone", "befallen", "--eq-path", pc.PI_EQ_PATH,
           "--path-file", remote_path_file, "--frames", str(frames)] + PROD_ARGS
    if entities is not None:
        cmd += ["--entities", str(entities), "--seed", "42"]
    cmd += extra_args
    cmd += ["--drm", "--res", str(RES[0]), str(RES[1]), "--gl-counters", "--csv", remote_csv]
    remote_cmd = " ".join(cmd) + f" > {remote_log} 2>&1"
    t0 = time.time()
    proc = pc.ssh_run(remote_cmd, timeout=300, check=False)
    dt = time.time() - t0
    if proc.returncode != 0:
        raise RuntimeError(f"Pi run failed (befallen/{label}), exit {proc.returncode}, wall {dt:.1f}s:\n"
                            f"{proc.stdout}\n{proc.stderr}")
    print(f"  [befallen/{label}] wall {dt:.1f}s")
    local_csv = OUT_DIR / f"befallen_{label}.csv"
    pc.rsync_from(remote_csv, OUT_DIR)
    pc.rsync_from(remote_log, OUT_DIR)
    pulled = OUT_DIR / Path(remote_csv).name
    if pulled != local_csv and pulled.exists():
        pulled.rename(local_csv)
    return local_csv


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


def sanity_check(summary):
    """P11b's missing --eqe1-path lesson: if composites silently no-op'd,
    draws_avg would sit near the multibuffer-era ~710 instead of the
    composite-era ~288 (P11b table). Since P37 only reverts ~0 of the
    DEFAULT race-cycle entities (see module docstring), draws_avg here is
    predicted to land close to P14/P39's own composite-era draws_avg, NOT
    the multibuffer figure -- if it lands near multibuffer levels instead,
    that means eqe1-path/composite silently no-op'd for THIS run, not that
    P37 caused a real regression, and the run must be re-checked/re-run
    before trusting the p95 number."""
    draws_avg = summary["draws_avg"]
    if draws_avg > 500:
        print(f"  [SANITY] draws_avg={draws_avg:.1f} is multibuffer-range (P11b: 710.1) "
              f"-- composite may have silently no-op'd (missing --eqe1-path class of bug). "
              f"DO NOT trust this run's p95 without investigating.")
        return False
    print(f"  [SANITY] draws_avg={draws_avg:.1f} is composite-range (P11b: 287.7) -- OK")
    return True


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--jitter-only", action="store_true",
                     help="run only the 2nd (repeat) invocation, reusing an already-collected 1st")
    args = ap.parse_args()

    print("[preflight] checking Pi reachability + binary staging...")
    proc = pc.ssh_run("echo OK && uname -a")
    print(f"  {proc.stdout.strip()}")
    local_bin = pc.ARM_BUILD_BIN_DIR / pc.BENCH_LOCAL_NAME
    if not local_bin.exists():
        raise RuntimeError(f"ARM binary not found at {local_bin} -- run scripts/build-arm-noble.sh "
                            "in THIS worktree first")
    print(f"  using ARM binary: {local_bin}")

    # Two invocations: whole replay + a repeat for jitter (plan step 4).
    plan = [
        ("e205_shipped_run1", SHIPPED_PRESET_ARGS),
        ("e205_shipped_run2", SHIPPED_PRESET_ARGS),
    ]
    print(f"[plan] {len(plan)} invocations this session (cap 12/session; well under)")

    pc.strip_and_stage_binary(SCRATCH)

    runner = pc.SessionRunner(cma_log_path=str(OUT_DIR / "cma_telemetry.tsv"))
    governor = pc.GovernorPin()
    governor.__enter__()
    results = {}
    try:
        for label, extra_args in plan:
            runner.before_invocation(label)
            csv_path = run_replay(label, extra_args)
            runner.after_invocation(label)
            s = summarize(csv_path)
            sanity_check(s)
            results[label] = s
            time.sleep(pc.H04_COOLDOWN_SEC)
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

    print("\n=== Summary vs P14 (32.313ms p95) / P39 (32.5ms p95) / 30fps line (33.3ms) ===")
    for key, s in results.items():
        flag = " *** OVER 33.3ms LINE ***" if s["total_ms_p95"] > KPI_LINE_MS else ""
        print(f"{key:22s} n={s['n_frames']:5d} total_avg={s['total_ms_avg']:7.3f} "
              f"total_p95={s['total_ms_p95']:7.3f}{flag} gpu_avg={s['gpu_ms_avg']:7.3f} "
              f"gpu_p95={s['gpu_ms_p95']:7.3f} sub_avg={s['submission_ms_avg']:7.3f} "
              f"sub_p95={s['submission_ms_p95']:7.3f} draws_avg={s['draws_avg']:6.1f} "
              f"polys_avg={s['polys_avg']:8.1f}")

    print("\nCSVs written to", OUT_DIR)
    print("\nIf per-race attribution is needed (headline regressed AND ambiguous from "
          "counters already collected): OPTIONAL 3rd invocation with --test-races "
          "34,17,35,67,89,53,33,31 (P08f's befallen zone-native set: BAT/ORC/SKE/ZOM/etc) "
          "to directly exercise the P37-excluded races and isolate their draws/entity "
          "delta. Only spend this invocation if step 4's 2 runs actually show movement "
          "or ambiguity -- do not spend it reflexively.")


if __name__ == "__main__":
    main()
