#!/usr/bin/env python3
"""P38: first-principles frame time-budget audit on the Pi, realistic load.

Will's steering: stop optimizing the KPI margin, explain the FUNDAMENTAL gap
between what a Voodoo1-era zone with 5 NPCs SHOULD cost on a Mali 400 and
what it actually costs. This is an audit -- report + bounded local
experiments (measured on Pi, reverted). See refactor/P38_time_budget_audit.md.

Workload: befallen canonical replay (benchmarks/paths/befallen_seed42_1000f.path),
--entities 5 (REALISTIC, not the P11-era calibrated N=205 stress load),
production config (manifest productionConfig args), shipped orangepi-preset
fog/clip (baked into the benchmark), 800x600, plus one FIXED-POSE run
(single --camera-pose repeated, isolates GPU/driver cost from camera-path
variance).

Suspects, each toggled by an env var / CLI flag, same binary:
  - --gl-counters                    driver glBindTexture/glTexParameteri counts (always on here)
  - WILLEQ_MIPMAP_PROTOTYPE=1        mipmap generation + mipmapped min-filter (COGLES2Texture.cpp)
  - WILLEQ_STATE_DEDUP_PROTOTYPE=1   per-texture glTexParameteri dedup (COpenGLES2Driver.cpp)
  - --lighting-model lightweight     per-pixel vs lightweight A/B (pre-existing flag)
  - --res 400 300 vs --res 800 600   fill-bound probe

IMPORTANT: overrides ARM_BUILD_BIN_DIR to THIS worktree's build-arm-noble/bin
(the pi_common.py default points at /mnt/projects/fable/main -- the
ARM_BUILD_BIN_DIR trap) before importing pi_common.
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
os.environ.setdefault("PI_REMOTE_BIN", "/tmp/gles2_zone_benchmark_p38")
os.environ.setdefault("H04_TOTAL_FRAMES", "1000")
os.environ.setdefault("H04_COOLDOWN_SEC", "15")
os.environ.setdefault("H04_WARMUP_REPS", "1")

sys.path.insert(0, str(PROJECT_DIR / "scripts" / "pi"))
import pi_common as pc  # noqa: E402

OUT_DIR = PROJECT_DIR / "benchmarks" / "pi_runs" / "p38_time_budget_audit"
OUT_DIR.mkdir(parents=True, exist_ok=True)
SCRATCH = Path("/tmp/p38_pi_scratch")

PROD_ARGS = pc.production_config_args()
BEFALLEN_PATH = PROJECT_DIR / "benchmarks/paths/befallen_seed42_1000f.path"
FIXEDPOSE_PATH = PROJECT_DIR / "benchmarks/paths/befallen_fixedpose_300f.path"
REALISTIC_N = 5  # Will's steering: realistic load, not the P11-era stress N=205


def run_replay(label, extra_args, env_prefix="", res=(800, 600), frames=1000,
               entities=REALISTIC_N, fixed_pose=False):
    # Fixed-pose mode reuses the SAME --path-file/--animate pipeline (entities
    # + --gl-counters wiring) with a synthetic path holding one pose constant
    # (benchmarks/paths/befallen_fixedpose_300f.path) -- the benchmark's
    # native --camera-pose static-capture branch is zone-geometry-only (no
    # entities, no gl-counters instrumentation), so it can't isolate "camera
    # variance vs GPU/driver cost" the way this unit needs.
    path_file = FIXEDPOSE_PATH if fixed_pose else BEFALLEN_PATH
    if fixed_pose:
        frames = 300
    remote_csv = f"/tmp/p38_befallen_{label}.csv"
    remote_log = f"/tmp/p38_befallen_{label}.log"
    remote_path_file = "/tmp/p38_befallen_fixedpose.path" if fixed_pose else "/tmp/p38_befallen.path"
    pc.scp_to(path_file, remote_path_file)
    cmd = [pc.PI_REMOTE_BIN, "--zone", "befallen", "--eq-path", pc.PI_EQ_PATH,
           "--path-file", remote_path_file]
    cmd += ["--frames", str(frames)] + PROD_ARGS
    if entities is not None:
        cmd += ["--entities", str(entities), "--seed", "42"]
    cmd += ["--animate"]
    cmd += extra_args
    cmd += ["--drm", "--res", str(res[0]), str(res[1]), "--gl-counters", "--csv", remote_csv]
    remote_cmd = (env_prefix + " " if env_prefix else "") + " ".join(cmd) + f" > {remote_log} 2>&1"
    t0 = time.time()
    proc = pc.ssh_run(remote_cmd, timeout=300, check=False)
    dt = time.time() - t0
    if proc.returncode != 0:
        raise RuntimeError(f"Pi run failed (befallen/{label}), exit {proc.returncode}, wall {dt:.1f}s:\n"
                            f"{proc.stdout}\n{proc.stderr}")
    print(f"  [befallen/{label}] wall {dt:.1f}s")
    local_csv = OUT_DIR / f"befallen_{label}.csv"
    pc.rsync_from(remote_csv, OUT_DIR)
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

    out = {"n_frames": len(rows), "rows": rows}
    for name in ["total_ms", "submission_ms", "gpu_ms", "collect_cull_ms", "sort_ms",
                 "other_ms", "swap_ms", "cpu_ms"]:
        vals = col(name)
        out[f"{name}_avg"] = sum(vals) / len(vals)
        out[f"{name}_p95"] = p95_of(vals)
    for name in ["draws", "state_changes", "polys", "tex_binds", "tex_param_calls",
                 "regions_drawn", "regions_occluded", "entities_visible"]:
        vals = col(name, int)
        out[f"{name}_avg"] = sum(vals) / len(vals)
    return out


def main():
    print("[preflight] checking Pi reachability + binary staging...")
    proc = pc.ssh_run("echo OK && uname -a")
    print(f"  {proc.stdout.strip()}")
    local_bin = pc.ARM_BUILD_BIN_DIR / pc.BENCH_LOCAL_NAME
    if not local_bin.exists():
        raise RuntimeError(f"ARM binary not found at {local_bin} -- run scripts/build-arm-noble.sh in THIS worktree first")
    print(f"  using ARM binary: {local_bin}")

    # Session 4 plan (CORRECTION): config/constrained_presets.json's actual
    # SHIPPED "orangepi" preset (the one loaded at runtime, not the
    # constrained_renderer_config.cpp hardcoded-preset fallback this script
    # originally mirrored) sets "mergeZoneDraws": true and
    # "enableCompositeEntities": true -- discovered only after session 1-3's
    # runs already landed. Those earlier runs (now labeled *_unmerged in the
    # writeup) measure a real and useful counterfactual (what an UNMERGED
    # zone costs -- P09/P10's original problem, still reachable if the JSON
    # preset value were ever reverted), but they do NOT represent what ships
    # today. This session adds the TRUE shipped-preset-correct budget-table
    # entry.
    SHIPPED_PRESET_ARGS = ["--merge-zone-draws", "--composite-scenegraph",
                           "--eqe1-path", pc.PI_EQE1_PATH]
    # Session 10 CORRECTION: session2/session7's "statededup_on"/
    # "statededup_on_shipped" runs used an ARM binary built BEFORE the
    # state-dedup prototype code was added (build-arm-noble.sh was launched
    # right after the mipmap+gl-counters code landed, but the dedup code was
    # added afterward while that build was already in flight -- never
    # re-run before those two Pi invocations). Confirmed via a direct A/B on
    # the Pi (WILLEQ_STATE_DEDUP_PROTOTYPE=1 produced byte-identical
    # TexParam-call counts to the control on the stale binary). Rebuilt ARM
    # just now; re-running both variants for real data.
    plan = [
        ("statededup_on_v2", [], "WILLEQ_STATE_DEDUP_PROTOTYPE=1", {}),
        ("statededup_on_shipped_v2", SHIPPED_PRESET_ARGS, "WILLEQ_STATE_DEDUP_PROTOTYPE=1", {}),
    ]
    print(f"[plan] {len(plan)} invocations this session (cap 12/session)")

    pc.strip_and_stage_binary(SCRATCH)

    runner = pc.SessionRunner(cma_log_path=str(OUT_DIR / "cma_telemetry.tsv"))
    governor = pc.GovernorPin()
    governor.__enter__()
    results = {}
    try:
        for label, extra_args, env_prefix, kwargs in plan:
            runner.before_invocation(label)
            csv_path = run_replay(label, extra_args, env_prefix, **kwargs)
            runner.after_invocation(label)
            results[label] = summarize(csv_path)
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

    print("\n=== Whole-run summary ===")
    for key, s in results.items():
        print(f"{key:24s} n={s['n_frames']:5d} total_avg={s['total_ms_avg']:7.3f} "
              f"total_p95={s['total_ms_p95']:7.3f} sub_avg={s['submission_ms_avg']:7.3f} "
              f"sub_p95={s['submission_ms_p95']:7.3f} gpu_avg={s['gpu_ms_avg']:7.3f} "
              f"gpu_p95={s['gpu_ms_p95']:7.3f} collect_avg={s['collect_cull_ms_avg']:6.3f} "
              f"swap_avg={s['swap_ms_avg']:6.3f} draws_avg={s['draws_avg']:6.1f} "
              f"tris_avg={s['polys_avg']:7.1f} binds_avg={s['tex_binds_avg']:7.1f} "
              f"texparam_avg={s['tex_param_calls_avg']:7.1f}")

    print("\nCSVs written to", OUT_DIR)


if __name__ == "__main__":
    main()
