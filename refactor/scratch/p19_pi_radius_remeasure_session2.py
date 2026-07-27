#!/usr/bin/env python3
"""P19 session 2: the single remaining invocation (befallen_count) after
session 1's SessionAbort (CmaFree dropped below the 40MB floor after
invocation #3, handled per protocol -- see review). Reuses the exact same
config as session 1's befallen_count would have used."""
import os
import sys
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
os.environ.setdefault("PI_REMOTE_BIN", "/tmp/gles2_zone_benchmark_p19")
os.environ.setdefault("H04_TOTAL_FRAMES", "1000")
os.environ.setdefault("H04_COOLDOWN_SEC", "15")
os.environ.setdefault("H04_WARMUP_REPS", "1")

sys.path.insert(0, str(PROJECT_DIR / "scripts" / "pi"))
import pi_common as pc  # noqa: E402
import p19_pi_radius_remeasure as p19  # noqa: E402

p19.OUT_DIR.mkdir(parents=True, exist_ok=True)


def main():
    print("[preflight] checking Pi reachability + binary staging...")
    proc = pc.ssh_run("echo OK && uname -a")
    print(f"  {proc.stdout.strip()}")

    pc.strip_and_stage_binary(p19.SCRATCH)

    runner = pc.SessionRunner(cma_log_path=str(p19.OUT_DIR / "cma_telemetry_session2.tsv"))
    governor = pc.GovernorPin()
    governor.__enter__()
    try:
        runner.before_invocation("befallen_count")
        csv_path = p19.run_replay("befallen", p19.ZONES["befallen"], ["--pp-budget-mode", "count"], "count")
        runner.after_invocation("befallen_count")
        s = p19.summarize(csv_path)
        print(f"befallen_count n={s['n_frames']} total_avg={s['total_ms_avg']:.3f} "
              f"total_p95={s['total_ms_p95']:.3f} regions_vis_avg={s['regions_vis_avg']:.2f} "
              f"draws_avg={s['draws_avg']:.1f}")
    except pc.SessionAbort as e:
        governor.__exit__(None, None, None)
        print(f"[ABORT] {e}")
        raise
    except BaseException:
        governor.__exit__(None, None, None)
        raise
    else:
        governor.__exit__(None, None, None)

    print(f"\n[session2] {runner.count} invocation completed, CmaFree samples: "
          f"{[s[1] for s in runner.cma.samples]}")


if __name__ == "__main__":
    main()
