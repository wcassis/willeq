#!/usr/bin/env python3
"""H02 — CI gate: golden-frame, metric, and memory verification.

Runs `gles2_zone_benchmark` (the real GLES2/llvmpipe path built by H01) for
each zone in tests/goldens/manifest.json, dumps its designated frames, and
checks them against three independent assertions:

  1. Golden-frame pixel comparison (decoded-pixel, not PNG-byte compare) --
     exact-zero-diff is the pass bar. llvmpipe is deterministic (verified in
     the H02 review), so any difference is a real regression, not noise.
  2. Metric assertions from the benchmark CSV: exact-match on draw calls /
     state changes / polygon counts per designated frame (deterministic on a
     fixed camera path), and a threshold assertion on p95 CPU-ms (<= 2x the
     recorded baseline -- generous, to catch a render-loop regression, not
     to catch scheduler noise). T291: that CPU-ms is PROCESS CPU time
     (getrusage), csv column cpu_proc_ms -- not the wall-clock cpu_ms it
     used to read, which made the ceiling load-sensitive and produced eight
     false trips with zero image mismatches. See cpu_ms_series().
  3. Memory assertions (T69 extends the H02 texture-cache-only check to the
     full per-subsystem breakdown emitted by gles2_zone_benchmark.cpp's
     --memory-json, itself sourced from IrrlichtRenderer::getMemoryReportBreakdown()'s
     accounting -- see that function's doc comment and this file's MEMORY_*
     lists below):
       (a) Budget-enforced: usage <= a real configured ceiling
           (ConstrainedRendererConfig fields), independent of any baseline.
           Texture cache (pre-existing) and framebuffer (new).
       (b) Baseline-guarded: exact-match against the blessed value. These
           categories have NO configured budget -- this is regression
           detection, not budget enforcement. T69 measured 9 repeat runs
           across all 3 golden zones and found EVERY one of these fields
           bit-identical run to run (see refactor/T69_memory_enforcement_gate.md
           Review), so exact match is an empirically justified tolerance of
           zero, not a guess.
       (c) Reported, not asserted: process RSS and the untracked-RSS gap
           (RSS minus every tallied category). Printed and baselined every
           run for visibility, but never gated in this unit -- see the T69
           Review for why (RSS is real but noisier than the categories in
           (b), and a spuriously-firing gate is worse than no gate).

  4. T80 memory-coverage run (SEPARATE invocation, see MEMORY_COVERAGE_*
     and run_memory_coverage_benchmark()/bless_memory_coverage()/
     verify_memory_coverage() below): the golden-image runs above never pass
     --eqe1-path/--objects/--entities, so three categories
     (eqe1MappedBytes/eqe1ArchiveCount, placeableObjectsBytes,
     characterModelsBytes) always baselined at zero there -- not because
     nothing is wrong, but because nothing was ever loaded to measure. This
     runs a second, separate benchmark invocation per zone with full asset
     loading and NO pixel output at all (no --dump-frames/--dump-png-prefix/
     --csv), so the golden PNGs and the manifest's "zones" block are
     byte-for-byte unaffected -- see refactor/T80_memory_gate_coverage.md
     Review for the determinism data and the separation proof.

Usage:
  visual_gate.py --bless    Regenerate goldens + manifest baselines + contact sheet.
  visual_gate.py            Compare against the committed goldens/manifest.

Exit code is nonzero on any failure (or when build-gles2/ is missing --
see the ctest wrapper, scripts/ci/visual_gate.sh, for the SKIP path).
"""
import argparse
import base64
import csv
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
GOLDENS_DIR = PROJECT_DIR / "tests" / "goldens"
MANIFEST_PATH = GOLDENS_DIR / "manifest.json"
BENCH_BIN = PROJECT_DIR / "build-gles2" / "bin" / "gles2_zone_benchmark"
EQ_PATH = PROJECT_DIR / "data" / "eq_client"
ARTIFACTS_DIR = PROJECT_DIR / "build-gles2" / "ci_artifacts"  # gitignored (whole build-gles2/ is)

# T80: ETC1 archive directory for the memory-coverage run (see
# MEMORY_COVERAGE_* below). Mirrors the pre-existing data/eq_client
# convention (a gitignored local symlink to an NFS path -- see .gitignore
# and scripts/pi/env.sh's PI_EQE1_PATH, same canonical location).
EQE1_PATH = PROJECT_DIR / "data" / "eqe1_client"

RESOLUTION_DEFAULT = {"width": 800, "height": 600}

# T69: per-category memory field classification (see module docstring).
# Every name here must match a key gles2_zone_benchmark.cpp's --memory-json
# writes (BenchMemorySnapshot in that file) -- kept as plain field-name
# lists (not a schema) so bless()/verify() share one source of truth for
# "which fields get baselined" and "which get asserted".

# (a) usage/budget pairs: assert usage <= budget, independent of baseline.
MEMORY_BUDGET_PAIRS = [
    ("textureCacheBytesUsed", "textureCacheBytesLimit", "texture cache"),
    ("framebufferBytes", "framebufferBudgetBytes", "framebuffer"),
]

# (b) no configured budget -- baseline with the tolerance measured in T69
# (zero: 9 repeat runs across all 3 golden zones, every field below was
# bit-identical run to run on this deterministic llvmpipe path). A future
# zone/config where these are NOT bit-identical is real news, not noise --
# see the T69 Review before loosening this to a nonzero tolerance.
MEMORY_BASELINE_FIELDS = [
    "textureCount",
    "eqe1MappedBytes",
    "eqe1ArchiveCount",
    "zoneGeometryBytes",
    "placeableObjectsBytes",
    "s3dZoneSourceBytes",
    "gpuTextureBytes",
    "vboEboBytes",
    "characterModelsBytes",
    "totalEstimate",
]

# (c) reported/baselined for visibility, never asserted in this unit -- see
# module docstring and refactor/T69_memory_enforcement_gate.md Review.
MEMORY_REPORT_ONLY_FIELDS = [
    "processRssBytes",
    "untrackedBytes",
]

# T80 -- memory-coverage run (SEPARATE from the golden-image runs above).
#
# Three categories baselined at zero under the golden-image invocation
# because it never exercises them: eqe1MappedBytes/eqe1ArchiveCount (no
# --eqe1-path), placeableObjectsBytes (no --objects), characterModelsBytes
# (no --entities). A regression in any of these would sail through the gate
# undetected, even though the "memory" field list in the manifest makes it
# look guarded.
#
# The golden-image runs are NOT extended with these flags -- loading objects
# and entities changes what is rendered, which would move the golden PNGs
# and force an unrelated image re-bless (see T80_memory_gate_coverage.md).
# Instead this is a second, separate benchmark invocation per zone: full
# asset loading, memory-JSON only, no --dump-frames/--dump-png-prefix/--csv
# (no images, nothing that could touch tests/goldens/*.png). See
# run_memory_coverage_benchmark(), bless_memory_coverage(),
# verify_memory_coverage() below, and the T80 Review for the determinism
# measurement that justifies exact-match tolerance here too.
MEMORY_COVERAGE_FIELDS = [
    "eqe1MappedBytes",
    "eqe1ArchiveCount",
    "placeableObjectsBytes",
    "characterModelsBytes",
]

# Printed for context on every coverage run (matches MEMORY_REPORT_ONLY_FIELDS'
# treatment on the golden-image path) -- not asserted, this run's asset mix
# is deliberately heavier than the golden-image RSS baseline and isn't
# comparable to it.
MEMORY_COVERAGE_REPORT_ONLY_FIELDS = [
    "totalEstimate",
    "processRssBytes",
    "untrackedBytes",
]

# T80: fixed, explicit inputs -- not the loader's implicit defaults -- so the
# coverage run's asset mix is stable and self-documenting rather than
# silently following whatever kDefaultRaceIds happens to be in
# zone_bench_loader.cpp on a given day.
MEMORY_COVERAGE_ENTITY_COUNT = 5
MEMORY_COVERAGE_TEST_RACES = [1, 2, 3, 4, 5]
# T291: how many times --bless re-runs each zone's benchmark purely to
# measure the CPU baseline (the max per-run p95 wins). See bless().
BLESS_CPU_REPEATS = 3

MEMORY_COVERAGE_FRAMES = 5
MEMORY_COVERAGE_WARMUP = 1


def load_manifest():
    with open(MANIFEST_PATH) as f:
        return json.load(f)


def run_benchmark(zone_cfg, bench_cfg, out_dir, production_args=None):
    """Run the benchmark for one zone, dumping designated frames + CSV + memory JSON.

    Returns (png_paths_by_frame, csv_rows, memory_dict, stdout_text).
    """
    zone = zone_cfg["zone"]
    frames = zone_cfg.get("designatedFrames", [0])
    prefix = out_dir / zone
    csv_path = out_dir / f"{zone}.csv"
    mem_path = out_dir / f"{zone}_mem.json"

    cmd = [
        str(BENCH_BIN),
        "--zone", zone,
        "--eq-path", str(EQ_PATH),
        "--frames", str(bench_cfg["frames"]),
        "--warmup", str(bench_cfg["warmup"]),
        "--res", str(bench_cfg["resolution"]["width"]), str(bench_cfg["resolution"]["height"]),
        "--dump-frames", ",".join(str(f) for f in frames),
        "--dump-png-prefix", str(prefix),
        "--csv", str(csv_path),
        "--memory-json", str(mem_path),
        # P42 item 3: the benchmark's --occluder-budget default now resolves
        # from ConstrainedRendererConfig (orangepi preset -> 256) instead of
        # the old hardcoded 16 -- correct for perf measurement (matches
        # shipped production), but these committed goldens (pixel PNGs +
        # draws/polys counts in manifest.json) were captured under the old
        # default and are untouched by this unit ("goldens untouched" is a
        # hard rule). Pin the gate's own invocation to the prior literal
        # value so it stays on the exact code path the goldens were blessed
        # against; --occluder-budget is an explicit-flag-wins override (see
        # zone_bench_loader.cpp), so this does not fight item 3's default
        # fix -- Pi measurements (scripts/pi/pi_common.py) and any other
        # caller that doesn't pass this flag still get the corrected 256
        # default. A future re-bless (Will's call, out of this unit's scope)
        # can drop this pin once goldens are regenerated under the new default.
        "--occluder-budget", "16",
    ] + (production_args or []) + zone_cfg.get("extraArgs", [])

    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    if proc.returncode != 0:
        raise RuntimeError(
            f"benchmark failed for zone {zone} (exit {proc.returncode}):\n"
            f"cmd: {' '.join(cmd)}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )

    png_paths = {f: Path(f"{prefix}_f{f}.png") for f in frames}
    for f, p in png_paths.items():
        if not p.exists():
            raise RuntimeError(f"expected golden frame PNG missing: {p}\nstdout:\n{proc.stdout}")

    csv_rows = list(csv.DictReader(open(csv_path)))

    memory = None
    if mem_path.exists():
        with open(mem_path) as f:
            memory = json.load(f)

    return png_paths, csv_rows, memory, proc.stdout


def run_memory_coverage_benchmark(zone, out_dir):
    """T80: separate benchmark invocation for the three no-longer-zero memory
    categories. Full asset loading (--eqe1-path/--objects/--entities/
    --test-races), memory-JSON only -- deliberately NO --dump-frames/
    --dump-png-prefix/--csv, so this run cannot write or touch a golden PNG.
    Uses its own output filename (f"{zone}_coverage_mem.json") distinct from
    run_benchmark()'s f"{zone}_mem.json" so the two never collide in
    ARTIFACTS_DIR.

    Returns the memory dict, or None if EQE1_PATH isn't present on this
    machine (see verify_memory_coverage()/bless_memory_coverage() for how
    that's handled -- a loud skip of this section only, not a silent zero).
    """
    if not EQE1_PATH.exists():
        return None

    mem_path = out_dir / f"{zone}_coverage_mem.json"
    cmd = [
        str(BENCH_BIN),
        "--zone", zone,
        "--eq-path", str(EQ_PATH),
        "--frames", str(MEMORY_COVERAGE_FRAMES),
        "--warmup", str(MEMORY_COVERAGE_WARMUP),
        "--res", str(RESOLUTION_DEFAULT["width"]), str(RESOLUTION_DEFAULT["height"]),
        "--memory-json", str(mem_path),
        "--eqe1-path", str(EQE1_PATH),
        "--objects",
        "--entities", str(MEMORY_COVERAGE_ENTITY_COUNT),
        "--test-races", ",".join(str(r) for r in MEMORY_COVERAGE_TEST_RACES),
        "--occluder-budget", "16",
    ]

    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    if proc.returncode != 0:
        raise RuntimeError(
            f"memory-coverage benchmark failed for zone {zone} (exit {proc.returncode}):\n"
            f"cmd: {' '.join(cmd)}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )

    if not mem_path.exists():
        raise RuntimeError(f"memory-coverage: expected memory JSON missing: {mem_path}\nstdout:\n{proc.stdout}")

    with open(mem_path) as f:
        return json.load(f)


def extract_gl_version(stdout_text):
    m = re.search(r"\[OpenGL ES [^\]]*\]", stdout_text)
    if m:
        return m.group(0).strip("[]")
    return None


def p95(values):
    values = sorted(values)
    idx = int(len(values) * 0.95)
    if idx >= len(values):
        idx = len(values) - 1
    return values[idx]


def cpu_ms_series(csv_rows, zone):
    """T291: the per-frame CPU series the p95 ceiling is asserted on.

    PROCESS CPU time (getrusage(RUSAGE_SELF): ru_utime + ru_stime) over the
    benchmark's draw-submit span -- csv column `cpu_proc_ms`, added by
    tools/hwcaps/zone_bench_metrics.{h,cpp}. NOT `cpu_ms`, which is the same
    span measured on steady_clock and therefore inflates whenever an
    unrelated process steals the core. That load-sensitivity is the whole
    reason this gate tripped eight times (T219, T220, T239, T258, T274,
    T282, T286, T287) with zero image mismatches in the record; T261's
    RUN_SERIAL only serialised within one ctest run, never against a sibling
    worktree compiling, so T274 and T286 tripped it anyway.

    A missing column means the build-gles2/ benchmark binary predates T291 --
    fail loudly rather than silently falling back to the wall clock and
    re-inheriting the flakiness.
    """
    if not csv_rows or "cpu_proc_ms" not in csv_rows[0]:
        raise RuntimeError(
            f"{zone}: benchmark CSV has no `cpu_proc_ms` column -- the "
            f"build-gles2/ binary predates T291. Rebuild it: "
            f"bash scripts/build-gles2-x86.sh")
    return [float(r["cpu_proc_ms"]) for r in csv_rows]


def compare_images_exact(path_a, path_b):
    """Decoded-pixel comparison. Returns (identical: bool, diff_count: int, size).

    Compares RGB only (not RGBA): these renders are opaque framebuffer dumps,
    and ImageChops.difference()/Image.getbbox() on RGBA silently ignores
    color-channel differences whenever the alpha channel is unchanged (which
    it always is here, alpha is always 255) -- comparing RGBA looks correct
    but never detects a real pixel mismatch. Confirmed with the H02 negative
    test (see review): an RGBA compare of a deliberately perturbed golden
    reported "identical" despite a 20x20 magenta patch; switching to RGB
    fixed it.
    """
    try:
        from PIL import Image, ImageChops
        a = Image.open(path_a).convert("RGB")
        b = Image.open(path_b).convert("RGB")
        if a.size != b.size:
            return False, -1, (a.size, b.size)
        diff = ImageChops.difference(a, b)
        bbox = diff.getbbox()
        if bbox is None:
            return True, 0, a.size
        # Count differing pixels for a clearer failure message.
        diff_data = diff.getdata()
        diff_count = sum(1 for px in diff_data if any(px))
        return False, diff_count, a.size
    except ImportError:
        return compare_images_imagemagick(path_a, path_b)


def compare_images_imagemagick(path_a, path_b):
    """Fallback: ImageMagick `compare -metric AE`."""
    proc = subprocess.run(
        ["compare", "-metric", "AE", str(path_a), str(path_b), "/dev/null"],
        capture_output=True, text=True,
    )
    # compare writes the AE count to stderr regardless of match/mismatch.
    try:
        diff_count = int(proc.stderr.strip())
    except ValueError:
        diff_count = -1
    return diff_count == 0, diff_count, None


def save_diff_artifact(golden_path, actual_path, out_path):
    try:
        from PIL import Image, ImageChops
        a = Image.open(golden_path).convert("RGB")
        b = Image.open(actual_path).convert("RGB")
        if a.size == b.size:
            diff = ImageChops.difference(a, b)
            diff.save(out_path)
            return
    except ImportError:
        pass
    subprocess.run(["compare", str(golden_path), str(actual_path), str(out_path)],
                    capture_output=True)


def bless(manifest):
    ARTIFACTS_DIR.mkdir(parents=True, exist_ok=True)
    bench_cfg = manifest["benchmark"]
    production_args = manifest.get("productionConfig", {}).get("args", [])

    for zone_cfg in manifest["zones"]:
        zone = zone_cfg["zone"]
        print(f"[bless] {zone} ...")
        png_paths, csv_rows, memory, stdout_text = run_benchmark(
            zone_cfg, bench_cfg, ARTIFACTS_DIR, production_args)

        gl_version = extract_gl_version(stdout_text)
        if gl_version:
            manifest["blessedUnder"]["glVersion"] = gl_version

        metrics = {}
        for f in zone_cfg["designatedFrames"]:
            row = csv_rows[f]
            metrics[str(f)] = {
                "draws": int(row["draws"]),
                "stateChanges": int(row["state_changes"]),
                "polys": int(row["polys"]),
            }
        zone_cfg["metrics"] = metrics
        # T291: the p95 baseline is the MAX per-run p95 over BLESS_CPU_REPEATS
        # independent benchmark invocations, not a single run's. The gate
        # takes p95 over only three measured frames (one per --camera-pose),
        # so p95 == max of three samples -- a statistic with no averaging in
        # it at all. Blessing that from one run means the ceiling inherits
        # whatever that one run happened to sample; bless low and every
        # subsequent run is a coin flip against a 2x ceiling. Only the CPU
        # baseline uses the repeats: the golden PNGs, the draws/state/poly
        # metrics and the memory numbers all come from the FIRST run (they
        # are exact-deterministic -- H02/T69 -- so repeating them would prove
        # nothing and could only introduce ambiguity about which run's
        # pixels were written).
        run_p95s = [p95(cpu_ms_series(csv_rows, zone))]
        repeat_dir = ARTIFACTS_DIR / "cpu_repeat"
        repeat_dir.mkdir(parents=True, exist_ok=True)
        for extra in range(1, BLESS_CPU_REPEATS):
            _, extra_rows, _, _ = run_benchmark(
                zone_cfg, bench_cfg, repeat_dir, production_args)
            run_p95s.append(p95(cpu_ms_series(extra_rows, zone)))
        zone_cfg["p95CpuMsBaseline"] = max(run_p95s)
        print(f"  [cpu] {zone}: per-run p95 " +
              ", ".join(f"{v:.3f}" for v in run_p95s) +
              f" -> baseline {max(run_p95s):.3f} (max)")
        if memory:
            # T69: bless every field the benchmark now emits -- budget pairs
            # (blessed for display/debugging even though the check is
            # against the live budget, not the blessed value), baseline
            # fields (the actual regression-detection targets), and the
            # report-only fields (recorded so a re-bless's diff shows RSS
            # movement over time even though nothing gates on it).
            mem = {}
            for used_key, limit_key, _label in MEMORY_BUDGET_PAIRS:
                mem[used_key] = memory[used_key]
                mem[limit_key] = memory[limit_key]
            for key in MEMORY_BASELINE_FIELDS:
                mem[key] = memory[key]
            for key in MEMORY_REPORT_ONLY_FIELDS:
                mem[key] = memory[key]
            zone_cfg["memory"] = mem

        for f, png_path in png_paths.items():
            dest = GOLDENS_DIR / f"{zone}_f{f}.png"
            shutil.copyfile(png_path, dest)
            print(f"  wrote {dest}")

    # T80: separate memory-coverage run, blessed after the golden-image loop
    # above and written to a distinct manifest key -- the golden-image loop
    # above (zone_cfg["memory"] on manifest["zones"]) is byte-for-byte
    # unchanged by this addition.
    bless_memory_coverage(manifest)

    with open(MANIFEST_PATH, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"[bless] manifest written: {MANIFEST_PATH}")

    write_contact_sheet(manifest)
    print("[bless] done. Goldens are NOT approved -- Will's sign-off is required before merge.")
    return 0


def bless_memory_coverage(manifest):
    """T80: bless the SEPARATE memory-coverage run. Writes
    manifest["memoryCoverage"]["zones"][i]["memory"] only -- never touches
    manifest["zones"] (the golden-image section) or any tests/goldens/*.png.
    No-op (with a printed note) if EQE1_PATH isn't present on this machine.
    """
    coverage_cfg = manifest.get("memoryCoverage")
    if not coverage_cfg:
        print("[bless] memoryCoverage: no section in manifest, skipping (nothing to bless)")
        return
    if not EQE1_PATH.exists():
        print(f"[bless] memoryCoverage: SKIP -- {EQE1_PATH} not present on this machine "
              f"(the three T80 categories will NOT be re-baselined this run)")
        return

    for zone_cfg in coverage_cfg["zones"]:
        zone = zone_cfg["zone"]
        print(f"[bless] memory-coverage {zone} ...")
        memory = run_memory_coverage_benchmark(zone, ARTIFACTS_DIR)
        mem = {}
        for key in MEMORY_COVERAGE_FIELDS:
            mem[key] = memory[key]
        for key in MEMORY_COVERAGE_REPORT_ONLY_FIELDS:
            mem[key] = memory[key]
        zone_cfg["memory"] = mem


def verify_memory_coverage(manifest, failures):
    """T80: verify the SEPARATE memory-coverage run. Appends to `failures`
    in place (same accumulation pattern verify() uses for the golden-image
    checks) rather than returning its own list, so one combined FAIL/PASS
    report covers both runs. Never reads or writes tests/goldens/*.png.
    """
    coverage_cfg = manifest.get("memoryCoverage")
    if not coverage_cfg:
        print("[verify] memoryCoverage: no section in manifest, skipping")
        return
    if not EQE1_PATH.exists():
        print(f"[verify] memoryCoverage: SKIP -- {EQE1_PATH} not present on this machine. "
              f"eqe1MappedBytes/eqe1ArchiveCount/placeableObjectsBytes/characterModelsBytes "
              f"are NOT being checked this run -- this is a real coverage gap on this machine, "
              f"not a pass.")
        return

    for zone_cfg in coverage_cfg["zones"]:
        zone = zone_cfg["zone"]
        print(f"[verify] memory-coverage {zone} ...")
        try:
            memory = run_memory_coverage_benchmark(zone, ARTIFACTS_DIR)
        except RuntimeError as e:
            failures.append(f"memory-coverage {zone}: {e}")
            continue

        expected_mem = zone_cfg.get("memory")
        if not expected_mem:
            # T80 ships the mechanism unblessed, same as T69 did for its new
            # (b)-bucket fields: the committed manifest has no baseline for
            # this zone yet (Will has not signed off on the proposed numbers
            # -- see the T80 Review's baseline table), so this is a real,
            # visible coverage gap, not a failure of the run itself. Printed
            # loudly rather than silently passed.
            print(f"  [memory-coverage] {zone}: NOT YET BLESSED -- no baseline in manifest, "
                  f"skipping assertion (see refactor/T80_memory_gate_coverage.md Review "
                  f"for the proposed values pending Will's sign-off)")
            continue

        # Exact match -- T80 measured 5 (qeynos2) + 3 + 3 (gfaydark, befallen)
        # repeat runs under this exact asset mix and found every field below
        # bit-identical run to run, same discipline and same empirically-zero
        # tolerance as T69's MEMORY_BASELINE_FIELDS. See the T80 Review.
        for key in MEMORY_COVERAGE_FIELDS:
            if key not in memory or key not in expected_mem:
                continue
            if memory[key] != expected_mem[key]:
                failures.append(
                    f"memory-coverage {zone}: {key} mismatch (expected {expected_mem[key]}, got {memory[key]})"
                )

        rss = memory.get("processRssBytes")
        untracked = memory.get("untrackedBytes")
        total = memory.get("totalEstimate")
        if rss is not None:
            pct = (100.0 * total / rss) if (total is not None and rss) else 0.0
            print(f"  [memory-coverage] {zone}: tracked={total} RSS={rss} ({pct:.0f}%) "
                  f"untracked={untracked}  (reported only, not gated)")


def verify(manifest):
    ARTIFACTS_DIR.mkdir(parents=True, exist_ok=True)
    bench_cfg = manifest["benchmark"]
    production_args = manifest.get("productionConfig", {}).get("args", [])
    failures = []
    mesa_mismatch_warned = False

    for zone_cfg in manifest["zones"]:
        zone = zone_cfg["zone"]
        print(f"[verify] {zone} ...")
        try:
            png_paths, csv_rows, memory, stdout_text = run_benchmark(
                zone_cfg, bench_cfg, ARTIFACTS_DIR, production_args)
        except RuntimeError as e:
            failures.append(f"{zone}: {e}")
            continue

        gl_version = extract_gl_version(stdout_text)
        expected_gl_version = manifest["blessedUnder"]["glVersion"]
        if gl_version and gl_version != expected_gl_version and not mesa_mismatch_warned:
            print(f"  NOTE: mesa/GLES version differs from blessed baseline "
                  f"(blessed='{expected_gl_version}', current='{gl_version}') -- "
                  f"mesa upgraded, re-bless with --bless instead of treating diffs as regressions.")
            mesa_mismatch_warned = True

        # 1. Golden-frame pixel comparison.
        for f, actual_path in png_paths.items():
            golden_path = GOLDENS_DIR / f"{zone}_f{f}.png"
            if not golden_path.exists():
                failures.append(f"{zone} frame {f}: no golden at {golden_path} (run --bless first)")
                continue
            identical, diff_count, size = compare_images_exact(golden_path, actual_path)
            if not identical:
                diff_out = ARTIFACTS_DIR / f"{zone}_f{f}_diff.png"
                save_diff_artifact(golden_path, actual_path, diff_out)
                artifact_golden = ARTIFACTS_DIR / f"{zone}_f{f}_golden.png"
                shutil.copyfile(golden_path, artifact_golden)
                failures.append(
                    f"{zone} frame {f}: PIXEL MISMATCH ({diff_count} differing pixels, size={size}). "
                    f"Artifacts: {actual_path}, {artifact_golden}, {diff_out}"
                )

        # 2. Metric assertions.
        for f_str, expected in zone_cfg["metrics"].items():
            f = int(f_str)
            row = csv_rows[f]
            for key, csv_key in (("draws", "draws"), ("stateChanges", "state_changes"), ("polys", "polys")):
                actual_val = int(row[csv_key])
                if actual_val != expected[key]:
                    failures.append(
                        f"{zone} frame {f}: {key} mismatch (expected {expected[key]}, got {actual_val})"
                    )

        p95_cpu = p95(cpu_ms_series(csv_rows, zone))
        baseline = zone_cfg["p95CpuMsBaseline"]
        ceiling = baseline * 2.0
        print(f"  [cpu] {zone}: p95 process-CPU ms {p95_cpu:.3f} "
              f"(baseline {baseline:.3f}, ceiling {ceiling:.3f})")
        if p95_cpu > ceiling:
            failures.append(
                f"{zone}: p95 process-CPU ms {p95_cpu:.3f} exceeds ceiling {ceiling:.3f} (baseline {baseline:.3f} x2.0)"
            )

        # 3. Memory assertions (T69).
        if memory:
            expected_mem = zone_cfg["memory"]

            # (a) Budget pairs: usage <= a real configured ceiling, checked
            # against the LIVE limit (not the blessed one) since the budget
            # is a config value, not a measurement -- and separately, exact-
            # match the usage against its blessed value (regression
            # detection on top of the budget check; texture cache did this
            # from H02 on, framebuffer usage is exact-deterministic per T69's
            # repeat-run data, same as every other new field).
            for used_key, limit_key, label in MEMORY_BUDGET_PAIRS:
                if used_key not in memory or used_key not in expected_mem:
                    continue  # field predates this bless (old manifest); skip rather than false-fail
                if memory[used_key] != expected_mem[used_key]:
                    failures.append(
                        f"{zone}: {label} bytes used mismatch (expected "
                        f"{expected_mem[used_key]}, got {memory[used_key]})"
                    )
                if memory[used_key] > memory[limit_key]:
                    failures.append(
                        f"{zone}: {label} bytes used {memory[used_key]} "
                        f"exceeds budget {memory[limit_key]}"
                    )

            # (b) Baseline-guarded fields: exact match, no configured budget
            # (see MEMORY_BASELINE_FIELDS' comment for why zero tolerance is
            # measured, not assumed).
            for key in MEMORY_BASELINE_FIELDS:
                if key not in memory or key not in expected_mem:
                    continue
                if memory[key] != expected_mem[key]:
                    failures.append(
                        f"{zone}: {key} mismatch (expected {expected_mem[key]}, got {memory[key]})"
                    )

            # (c) Report-only: printed prominently, recorded in the baseline
            # via bless(), never asserted in this unit.
            rss = memory.get("processRssBytes")
            untracked = memory.get("untrackedBytes")
            total = memory.get("totalEstimate")
            if rss is not None:
                pct = (100.0 * total / rss) if (total is not None and rss) else 0.0
                print(f"  [memory] {zone}: tracked={total} RSS={rss} ({pct:.0f}%) "
                      f"untracked={untracked}  (reported only, not gated -- see T69 Review)")

    # T80: separate memory-coverage run, verified after the golden-image loop
    # above and appending to the same `failures` list for one combined
    # report. Never touches the golden-image comparisons above.
    verify_memory_coverage(manifest, failures)

    if failures:
        print("\n=== VISUAL GATE: FAIL ===")
        for f in failures:
            print(f"  - {f}")
        return 1

    print("\n=== VISUAL GATE: PASS ===")
    return 0


def write_contact_sheet(manifest):
    rows_html = []
    for zone_cfg in manifest["zones"]:
        zone = zone_cfg["zone"]
        rows_html.append(f'<h2>{zone}</h2><p>{zone_cfg.get("description", "")}</p><div class="frames">')
        frame_descriptions = zone_cfg.get("frameDescriptions", {})
        for f in zone_cfg["designatedFrames"]:
            png_path = GOLDENS_DIR / f"{zone}_f{f}.png"
            data = base64.b64encode(png_path.read_bytes()).decode("ascii")
            metrics = zone_cfg["metrics"].get(str(f), {})
            desc = frame_descriptions.get(str(f), "")
            desc_html = f'<br><strong>{desc}</strong>' if desc else ""
            rows_html.append(
                f'<figure><img src="data:image/png;base64,{data}" alt="{zone} frame {f}">'
                f'<figcaption>frame {f} -- draws={metrics.get("draws")} '
                f'state={metrics.get("stateChanges")} polys={metrics.get("polys")}{desc_html}<br>'
                f'What to check: geometry silhouette, texture placement, lighting/fog tone.</figcaption></figure>'
            )
        rows_html.append("</div>")

    html = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>H02 golden contact sheet (NOT APPROVED)</title>
<style>
body {{ font-family: sans-serif; background: #111; color: #eee; margin: 2rem; }}
h1 {{ color: #f80; }}
.frames {{ display: flex; flex-wrap: wrap; gap: 1rem; }}
figure {{ margin: 0; background: #222; padding: 0.5rem; border-radius: 4px; }}
img {{ max-width: 260px; display: block; }}
figcaption {{ font-size: 0.8rem; color: #ccc; margin-top: 0.4rem; }}
.banner {{ background: #542; border: 1px solid #f80; padding: 0.75rem; margin-bottom: 1.5rem; }}
</style></head>
<body>
<h1>H02 Golden Frame Contact Sheet</h1>
<div class="banner"><strong>NOT APPROVED.</strong> These frames were generated by the executor
and are pending Will's visual sign-off. This page does not constitute approval.</div>
<p>Blessed under: {manifest['blessedUnder']['glRenderer']} / {manifest['blessedUnder']['glVersion']}</p>
{''.join(rows_html)}
</body></html>
"""
    out_path = GOLDENS_DIR / "contact_sheet.html"
    out_path.write_text(html)
    print(f"[bless] contact sheet: {out_path}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bless", action="store_true", help="Regenerate goldens + manifest + contact sheet")
    args = parser.parse_args()

    if not BENCH_BIN.exists():
        print(f"SKIP: {BENCH_BIN} not found (build-gles2/ absent -- run scripts/build-gles2-x86.sh first)")
        return 125  # ctest SKIP_RETURN_CODE (see scripts/ci/visual_gate.sh / tests/CMakeLists.txt)

    manifest = load_manifest()
    if args.bless:
        return bless(manifest)
    return verify(manifest)


if __name__ == "__main__":
    sys.exit(main())
