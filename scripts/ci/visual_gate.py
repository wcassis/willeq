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
           Texture cache (H02) and framebuffer (T69), plus -- since T293 --
           GPU textures, zone geometry, placeable objects, S3D zone source,
           VBO/EBO, character models, the EQE1 mapped address space, and the
           tracked total. T293 derived those budgets from the assets on disk,
           a 1280x720 maximum resolution and the Orange Pi One's measured
           envelope (Will's ruling 2026-08-26, discharging the block T69 left
           open); see MEMORY_BUDGET_PAIRS and docs/decisions.md. T293 also
           fixed the reason (b) used to be the larger bucket AND the reason
           the framebuffer budget had never actually fired -- see
           assert_memory_budgets().
       (b) Baseline-guarded: exact-match against the blessed value. Since
           T293 these categories all have a budget too; this bucket is the
           regression-detection half, which catches drift a budget with
           headroom cannot. T69 measured 9 repeat runs
           across all 3 golden zones (the pre-B813 sample; B813 added
           iceclad + timorous as the 4th and 5th zones) and found EVERY one of
           these fields bit-identical run to run (see
           refactor/T69_memory_enforcement_gate.md Review), so exact match is
           an empirically justified tolerance of zero, not a guess.
       (c) Reported, not asserted: process RSS and the T69 untracked-RSS gap
           (RSS minus every tallied ESTIMATE). Printed and baselined every
           run for visibility. Never gated -- T69 ruled it too noisy, and
           T491 superseded the whole question: the estimate-based gap is not
           what is controlled any more, the allocator ledger below is.
       (d) T491 memory ledger (Will's ruling 2026-09-14, docs/decisions.md):
           the benchmark's memory JSON now carries a "ledger" object -- every
           anonymous byte charged at the allocator to a lifecycle category
           (include/client/core/memory_ledger.h) -- and "ledgerBudgets" from
           the orangepi preset. assert_memory_ledger() asserts
           (i) unaccounted -- RssAnon minus the ledger minus glibc's measured
               free-list slack, i.e. stacks, direct mmap and JIT pages, the
               part attribution work can actually remove -- is at most
               LEDGER_UNATTRIBUTED_MAX_PCT of RssAnon,
           (ii) allocator slack is at most LEDGER_SLACK_MAX_PCT (a glibc
                tuning bound, deliberately looser: no caller owns those
                bytes, so no attribution work moves them), and
           (iii) every category's live bytes are within its configured budget
                 (0 = no budget, skipped and named).
           report_rss_target_gap() additionally asserts the 256 MB INTERIM
           CEILING and PRINTS the gap to the 64 MB target by bucket, largest
           first (Will's standing decision, 2026-09-17).
           All of it runs on the golden-image runs AND the memory-coverage
           runs. The bounds are a ratchet: lowered as attribution improves,
           history in docs/decisions.md's 2026-09-14 entry.

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
  visual_gate.py --zones a,b
                            Verify only zones a and b (CI splits the blocking
                            and non-blocking gate jobs this way; see
                            select_zones() and docs/ci.md).
  B1024 -- the platform dimension (docs/ci.md "Two golden sets: x86 and Pi"):
  visual_gate.py --platform pi --bench /path/to/gles2_zone_benchmark-nollvm
                Verify the Pi set against tests/goldens/pi/ + manifest
                platforms.pi (NOT YET BLESSED, exit 1, until blessed; the
                Mesa-prefix maps proof runs too). Run this ON THE ORANGE PI
                under the Pi lock.
  visual_gate.py --platform pi --bench BIN --capture DIR
                Capture the Pi set into DIR (staged bless; never touches
                tests/goldens/; the orchestrator commits it).
  visual_gate.py --platform pi --bench BIN --goldens-dir DIR
                Verify against a captured set (run2-vs-run1 determinism).
  --platform x86 (or no --platform) is the CI gate job, byte-identical to
  today's invocation (pinned by tests/data/b1024/x86_gate_plan.txt).
  --bless --platform pi is refused: Pi goldens are committed by the
  orchestrator from a captured set (that commit is the bless).

Exit code is nonzero on any failure (or when build-gles2/ is missing --
see the ctest wrapper, scripts/ci/visual_gate.sh, for the SKIP path).
"""
import argparse
import base64
import csv
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import owned_memory  # noqa: E402  B978: the one owned-memory definition

PROJECT_DIR = Path(__file__).resolve().parents[2]
GOLDENS_DIR = PROJECT_DIR / "tests" / "goldens"
MANIFEST_PATH = GOLDENS_DIR / "manifest.json"
# B1012: CI points this at the ref's own tree (build-gles2/ref/<slug>) through
# WILLEQ_GLES2_BUILD_DIR; a developer's checkout keeps build-gles2/.
GLES2_BUILD_DIR = Path(os.environ.get("WILLEQ_GLES2_BUILD_DIR") or (PROJECT_DIR / "build-gles2"))
BENCH_BIN = GLES2_BUILD_DIR / "bin" / "gles2_zone_benchmark"
EQ_PATH = PROJECT_DIR / "data" / "eq_client"
ARTIFACTS_DIR = GLES2_BUILD_DIR / "ci_artifacts"  # gitignored (whole build-gles2/ is)

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
#
# T293 (2026-08-26): this list was two entries long until Will ruled on the
# numbers. T69 could only enforce the two categories that already had a
# configured ceiling in ConstrainedRendererConfig and explicitly refused to
# invent the rest ("Proposing numbers is Will's call, not this unit's").
# ConstrainedRendererConfig now carries a derived budget for every remaining
# measurable category -- derived from the assets on disk, a 1280x720 maximum
# resolution and the Orange Pi One's measured envelope; see that header's
# block comment, docs/decisions.md ("2026-08-26 -- per-subsystem memory
# budgets") and refactor/T293_memory_budgets.md for the derivation table --
# and the benchmark emits each one alongside its usage number, so all of them
# are now real "usage <= budget" assertions and not only baseline guards.
#
# A budget of 0 means the active preset configures none for that category;
# assert_memory_budgets() skips it (and says so) rather than asserting
# usage <= 0.
MEMORY_BUDGET_PAIRS = [
    ("textureCacheBytesUsed", "textureCacheBytesLimit", "texture cache"),
    ("framebufferBytes", "framebufferBudgetBytes", "framebuffer"),
    # --- T293 additions ---
    ("gpuTextureBytes", "gpuTextureBudgetBytes", "GPU textures"),
    ("zoneGeometryBytes", "zoneGeometryBudgetBytes", "zone geometry"),
    ("placeableObjectsBytes", "placeableObjectsBudgetBytes", "placeable objects"),
    ("s3dZoneSourceBytes", "s3dZoneSourceBudgetBytes", "S3D zone source"),
    ("vboEboBytes", "vboEboBudgetBytes", "VBO/EBO"),
    ("characterModelsBytes", "characterModelsBudgetBytes", "character models"),
    # Address space, not RAM. eqe1MappedBytes is MAPPED bytes, of which T105
    # measured only 1.6-10.6% resident, which is why it is excluded from
    # totalEstimate (T108). The ceiling that binds it is the 32-bit ARM user
    # address space (~3 GB) -- T195's eager directory scan exhausted that with
    # ~2,900 MB mapped while 260 MB of RAM was still free.
    ("eqe1MappedBytes", "eqe1MappedBudgetBytes", "EQE1 mapped address space"),
    # The one genuinely aggregate ceiling: ConstrainedRendererConfig::
    # totalMemoryBudgetBytes has shipped on the orangepi preset (128 MiB)
    # since long before T69 with nothing asserting against it. This is a CI
    # assertion on a measured total, NOT runtime enforcement and NOT a
    # cross-asset coordinator -- Will's "no global cap" ruling is about
    # runtime behaviour; nothing here evicts or coordinates anything.
    ("totalEstimate", "totalEstimateBudgetBytes", "tracked total"),
]

# T293: pairs whose usage is ALSO exact-matched against the blessed baseline
# in verify(). Everything else in MEMORY_BUDGET_PAIRS is already covered by
# MEMORY_BASELINE_FIELDS below, and checking it twice would report one
# regression as two failures.
MEMORY_BUDGET_PAIRS_ALSO_BASELINED = ["textureCacheBytesUsed", "framebufferBytes"]

# (b) no configured budget -- baseline with the tolerance measured in T69
# (zero: 9 repeat runs across all 3 golden zones -- the pre-B813 sample;
# B813's two additions reproduced the same bit-identity on this host's
# llvmpipe, every field below was bit-identical run to run on this
# deterministic llvmpipe path). A future
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

# T491: the ledger's unattributed bound, in percent of RssAnon. What is left
# unattributed on the x86 llvmpipe path is (a) glibc free-list slack after
# the zone-load churn (mallinfo2 arena - in use, printed alongside) and (b)
# anonymous memory that never goes through malloc: llvmpipe's JIT code pages
# and rasteriser thread stacks. Neither exists on the Pi's Mali path, so the
# x86 number OVERSTATES the Pi's. Start at the measured x86 worst case plus
# headroom and ratchet DOWN as attribution improves -- never up without a
# Review that says why. History: docs/decisions.md 2026-09-14.
# T491 -- the memory-ledger bounds, every one of them MEASURED on this tip
# (x86, llvmpipe, 800x600) rather than argued. See refactor/T491... Review for
# the table; the trend lives in docs/decisions.md's 2026-09-14 entry.
#
# (1) THE ATTRIBUTION RATCHET. The ledger's "unattributed" category is the
#     bytes allocated with NO MemoryScope on the calling thread -- static
#     initialisers and the first allocations of a thread before its entry
#     point scopes itself. This, not "RssAnon - ledger", is the number Will's
#     "100% accounting" drives to zero, and it is the only one a code change
#     in this repo moves. Measured: 87,456 bytes on EVERY run, i.e. 0.040% of
#     the ledger on the coverage run and 0.087% on befallen. Bound set an
#     order of magnitude above the worst measurement so a real regression
#     (a new unscoped subsystem) is red; ratchet DOWN, never up.
LEDGER_UNATTRIBUTED_MAX_PCT = 1.0

# (2) NON-HEAP ANONYMOUS RSS, from the /proc/self/smaps census: every
#     anonymous byte that is not on glibc's heap, so no allocator hook can
#     ever charge it. Measured 15.6-15.8 MiB on EVERY zone, constant, and it
#     is llvmpipe: libLLVM.so 6.6 MiB of JIT pages, 6.7 MiB of unnamed
#     anonymous mappings (rasteriser thread stacks, ~290 of them),
#     libgallium 1.15 MiB. NONE of that exists on the Pi's Mali path, so this
#     number OVERSTATES the Pi's. Bounded in BYTES, not percent, because it
#     is constant while RssAnon varies 74-194 MiB across the runs -- a
#     percentage bound would be four different bounds wearing one number.
LEDGER_NONHEAP_MAX_BYTES = 24 * 1024 * 1024

# (3) Allocator slack (glibc free lists and fragmentation, arena - inUse).
#     Measured 9.4-17.4 MiB, worst 12.6% of RssAnon. No caller owns these
#     bytes: this is a glibc tuning bound (M_ARENA_MAX / M_TRIM_THRESHOLD),
#     deliberately loose, and no attribution work moves it.
LEDGER_SLACK_MAX_PCT = 35.0

# T491 addendum -- Will's standing decision, 2026-09-17: "the goal is willeq's
# RSS footprint between 32 and 64 MB. The 256 MB ceiling is now an interim
# bound, not the target." The ceiling stays the FAILING bound; the gap to the
# target is printed on every run, by bucket, largest first, so the next units
# can be planned off it. Mirrors include/client/core/memory_ledger.h.
RSS_CEILING_BYTES = 256 * 1024 * 1024
RSS_TARGET_BYTES = 64 * 1024 * 1024

# B1024 (#344) -- per-platform golden sets. One compare rule (exact match,
# compare_images_exact unchanged), two golden sets: the x86 set the CI gate
# verifies today (llvmpipe, 16-bit depth, B971) and a Pi set (Lima/Mali-400,
# 24-bit depth) rendered on the Orange Pi under the Pi lock by hand. The two
# sets can never be cross-compared (B980 measured the residual as coverage
# flips on alpha-cutout/clip edges -- a driver difference, not a regression),
# so each platform resolves its OWN goldens location and its OWN manifest
# section. See docs/ci.md "Two golden sets: x86 and Pi (B1024)" and
# docs/decisions.md 2026-10-11 (Will: "per-platform Pi goldens, go with
# option (a)").
#
# Pi constants. PI_EQ_PATH mirrors scripts/pi/env.sh's PI_EQ_PATH (the NFS
# mount the Pi sees); PI_MESA_PREFIX is the shipped LLVM-less Mesa prefix the
# published ci-arm/<sha>/bin/*-nollvm binaries run against (docs/ci.md "The
# shipped Mesa prefix"), whose directory name the published BUILD_INFO's
# mesa_nollvm_pi_prefix carries.
PI_EQ_PATH = Path(os.environ.get("PI_EQ_PATH", "/mnt/projects/EverQuestP1999"))
PI_MESA_PREFIX = os.environ.get(
    "WILLEQ_MESA_NOLLLVM_PREFIX", "/home/user/mesa-nollvm-25.2.8")


@dataclass(frozen=True)
class PlatformConfig:
    """Everything a run needs that differs by platform (B1024): the goldens
    location, the manifest section, the benchmark binary, the EQ path and
    the extra benchmark args. x86 is today's invocation, byte for byte
    (pinned by tests/data/b1024/x86_gate_plan.txt via
    scripts/ci/test_visual_gate_platform.py --verify-x86); pi is the same
    invocation on the Pi's GPU (its own EQ path + --drm, no other change)."""
    platform: str
    golden_root: Path            # where <zone>/<frame>.png goldens live
    section: object              # manifest["zones"] or manifest["platforms"]["pi"]
    bench: Path                  # the benchmark binary to launch
    eq_path: Path                # the --eq-path argument
    extra_bench_args: list       # appended after production args + extraArgs
    maps_required: bool          # pi: the Mesa-prefix proof must pass
    maps_prefix: str = PI_MESA_PREFIX


def platform_config(manifest, platform, bench=None, goldens_dir=None):
    """B1024: the ONE resolver for the platform dimension.

    x86 (the default; the CI gate job passes no --platform) = today,
    unchanged: goldens tests/goldens/<zone>_f<n>.png, section
    manifest["zones"], the built BENCH_BIN, EQ_PATH, no extra args, no maps
    proof. pi = goldens tests/goldens/pi/<zone>/f<n>.png (or goldens_dir/...),
    section manifest["platforms"]["pi"] (may be None until blessed -- verify
    then fails NOT YET BLESSED; there is no fallback to the x86 set), the
    --bench PATH (REQUIRED), PI_EQ_PATH, and --drm. The same poses, frames,
    warmup, resolution, productionConfig args and --occluder-budget 16 pin
    as run_benchmark() -- the Pi set is the x86 gate's invocation on the
    Pi's GPU, nothing else.
    """
    if platform == "x86":
        root = Path(goldens_dir) if goldens_dir else GOLDENS_DIR
        return PlatformConfig(
            platform="x86", golden_root=root, section=manifest["zones"],
            bench=Path(bench) if bench else BENCH_BIN, eq_path=EQ_PATH,
            extra_bench_args=[], maps_required=False)
    if platform != "pi":
        raise ValueError(f"unknown --platform {platform!r} (x86|pi)")
    if not bench:
        raise ValueError(
            "--platform pi requires --bench PATH (the published "
            "ci-arm/<sha>/bin/gles2_zone_benchmark-nollvm)")
    root = Path(goldens_dir) if goldens_dir else GOLDENS_DIR / "pi"
    return PlatformConfig(
        platform="pi", golden_root=root,
        section=manifest.get("platforms", {}).get("pi"),
        bench=Path(bench), eq_path=PI_EQ_PATH,
        extra_bench_args=["--drm"], maps_required=True,
        maps_prefix=PI_MESA_PREFIX)


def golden_frame_path(cfg, zone, frame):
    """B1024: the golden PNG for one platform's one frame -- x86
    <root>/<zone>_f<n>.png (today's layout), pi <root>/<zone>/f<n>.png."""
    if cfg.platform == "x86":
        return cfg.golden_root / f"{zone}_f{frame}.png"
    return cfg.golden_root / zone / f"f{frame}.png"


def check_maps_text(maps_path, mesa_prefix=PI_MESA_PREFIX):
    """B1024: the Mesa-prefix proof for a pi zone run. The maps file is
    /proc/<pid>/maps of the benchmark while its GL stack is up (captured the
    way scripts/pi/mesa_frame_ab.sh:39-40 polls for libgallium). The zone
    FAILS if libLLVM is mapped (that is the LLVM build, not the published
    -nollvm binary) or if libgallium is not under the shipped LLVM-less
    prefix (mesa_prefix -- the published BUILD_INFO's mesa_nollvm_pi_prefix).
    Returns a list of failure strings (empty = proof green)."""
    failures = []
    if not maps_path.exists():
        return [f"maps proof missing: {maps_path} (the benchmark never showed a libgallium mapping)"]
    text = maps_path.read_text()
    if "libLLVM" in text:
        failures.append(f"{maps_path.name}: libLLVM is mapped -- that is the LLVM "
                        f"Mesa build, not the published -nollvm binary")
    gallium_lines = [ln for ln in text.splitlines() if "libgallium" in ln]
    if not gallium_lines:
        failures.append(f"{maps_path.name}: no libgallium mapping found")
    for ln in gallium_lines:
        # /proc/pid/maps: start-end perms offset dev inode path
        parts = ln.split()
        path = parts[5] if len(parts) >= 6 else ""
        if not path.startswith(mesa_prefix + "/"):
            failures.append(f"{maps_path.name}: libgallium {path} is not under "
                            f"the shipped prefix {mesa_prefix}/")
    return failures


def poll_maps(proc, maps_path, timeout_s=120.0):
    """B1024: copy /proc/<pid>/maps of a RUNNING benchmark once its GL stack
    is up (poll for libgallium, the mesa_frame_ab.sh:39-40 precedent -- the
    first pose is drawn after load, so a fixed delay would be a guess), and
    again shortly before it exits is NOT needed: the GL libraries stay mapped
    for the whole run. Returns True when a maps file was written."""
    maps_path = Path(maps_path)
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            text = Path(f"/proc/{proc.pid}/maps").read_text()
        except (OSError, FileNotFoundError):
            if proc.poll() is not None:
                return False
            time.sleep(0.5)
            continue
        if "libgallium" in text:
            maps_path.parent.mkdir(parents=True, exist_ok=True)
            maps_path.write_text(text)
            return True
        time.sleep(0.5)
    return False


def load_manifest():
    with open(MANIFEST_PATH) as f:
        return json.load(f)


def run_benchmark(zone_cfg, bench_cfg, out_dir, production_args=None, cfg=None):
    """Run the benchmark for one zone, dumping designated frames + CSV + memory JSON.

    Returns (png_paths_by_frame, csv_rows, memory_dict, stdout_text).

    B1024: with cfg=None (x86, the pre-B1024 path) the invocation is
    byte-identical to today's -- pinned by tests/data/b1024/x86_gate_plan.txt
    and --verify-x86. With a PlatformConfig, the platform's binary / EQ path /
    extra args replace the x86 ones, and --platform pi launches via Popen so
    poll_maps() can take the Mesa-prefix proof off a LIVE process. The pi run
    writes its frame PNGs straight into <out_dir>/<zone>/ -- the same layout
    the committed set has (tests/goldens/pi/<zone>/f<n>.png) -- so a
    capture's staged frames are the committed frames, unrenamed. The frames,
    warmup, resolution, productionConfig args and the --occluder-budget 16 pin
    are the SAME for both platforms: the Pi set is the x86 gate's invocation
    on the Pi's GPU, nothing else.
    """
    zone = zone_cfg["zone"]
    frames = zone_cfg.get("designatedFrames", [0])
    if cfg is None:
        bench, eq_path, extra = BENCH_BIN, EQ_PATH, ()
        maps_path = None
        prefix = out_dir / zone
    else:
        bench, eq_path, extra = cfg.bench, cfg.eq_path, cfg.extra_bench_args
        maps_path = out_dir / f"{zone}_maps.txt" if cfg.maps_required else None
        # x86-with-cfg and pi share the per-zone dump prefix: --dump-png-prefix
        # <prefix> makes the benchmark write <prefix>_f<n>.png, i.e.
        # <out_dir>/<zone>/f<n>.png.
        prefix = out_dir / zone
    csv_path = out_dir / f"{zone}.csv"
    mem_path = out_dir / f"{zone}_mem.json"
    # The benchmark's writeImageToFile() does not mkdir: without this the pi
    # capture fails "FAILED to save" for every frame (the zone dir does not
    # exist on a fresh capture run; x86 runs into the pre-existing
    # ARTIFACTS_DIR, which is why it never bit there).
    prefix.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        str(bench),
        "--zone", zone,
        "--eq-path", str(eq_path),
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
    ] + (production_args or []) + zone_cfg.get("extraArgs", []) + list(extra)

    if maps_path is None:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        if proc.returncode != 0:
            raise RuntimeError(
                f"benchmark failed for zone {zone} (exit {proc.returncode}):\n"
                f"cmd: {' '.join(cmd)}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
            )
        stdout_text, stderr_text = proc.stdout, proc.stderr
    else:
        # B1024: the pi run is a LIVE process while poll_maps() takes its
        # /proc/<pid>/maps (the GL libraries are mapped for the whole run;
        # the poll lands the moment libgallium appears -- a fixed delay
        # would be a guess, see mesa_frame_ab.sh:39-40). maps_path stays a
        # local here; poll_maps gets it directly.
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True)
        if not poll_maps(proc, maps_path):
            # the maps file may still exist (taken before a crash);
            # check_maps_text() reports it either way
            pass
        rc = proc.wait()
        stdout_text = proc.stdout.read()
        stderr_text = proc.stderr.read()
        if rc != 0:
            raise RuntimeError(
                f"benchmark failed for zone {zone} (exit {rc}):\n"
                f"cmd: {' '.join(cmd)}\nstdout:\n{stdout_text}\nstderr:\n{stderr_text}"
            )

    png_paths = {f: Path(f"{prefix}_f{f}.png") for f in frames}
    for f, p in png_paths.items():
        if not p.exists():
            raise RuntimeError(f"expected golden frame PNG missing: {p}\nstdout:\n{stdout_text}")

    csv_rows = list(csv.DictReader(open(csv_path)))

    memory = None
    if mem_path.exists():
        with open(mem_path) as f:
            memory = json.load(f)

    return png_paths, csv_rows, memory, stdout_text


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
                if used_key not in memory or limit_key not in memory:
                    continue  # benchmark predates this field
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


def assert_memory_budgets(label_prefix, memory, failures):
    """T293: assert usage <= budget for every configured per-subsystem budget.

    Deliberately independent of the blessed baseline. The pre-T293 code did
    the budget check inside the same loop that exact-matched usage against
    manifest["zones"][i]["memory"], and skipped the whole pair when used_key
    was absent from that baseline -- so T69's framebuffer budget assertion had
    NEVER actually fired against the committed manifest, whose "memory" blocks
    carry only the three pre-T69 texture-cache fields. A budget is a config
    value, not a measurement: it is knowable without a bless, and coupling it
    to one silently disabled it. The baseline exact-match stays where it
    belongs, in the MEMORY_BASELINE_FIELDS loop.

    A budget of 0 (or a missing budget key) means the active preset configures
    no ceiling for that category -- skipped, and named in the printed summary
    so an unbudgeted category is visible rather than looking checked.
    """
    checked, skipped = [], []
    for used_key, limit_key, label in MEMORY_BUDGET_PAIRS:
        if used_key not in memory or limit_key not in memory:
            continue  # benchmark predates this field; nothing to assert
        budget = memory[limit_key]
        if not budget:
            skipped.append(label)
            continue
        used = memory[used_key]
        checked.append(f"{label} {100.0 * used / budget:.0f}%")
        if used > budget:
            failures.append(
                f"{label_prefix}: {label} bytes used {used} exceeds budget {budget}"
            )
    if checked:
        print(f"  [budget] {label_prefix}: " + ", ".join(checked))
    if skipped:
        print(f"  [budget] {label_prefix}: NO BUDGET CONFIGURED for " + ", ".join(skipped))


def _heap_anon_bytes(mappings):
    """Resident anonymous bytes of the glibc brk heap, from the smaps census.
    None/0 when no census is available (an older benchmark)."""
    if not isinstance(mappings, dict) or not mappings.get("available"):
        return 0
    for o in mappings.get("owners", []):
        if o.get("name") == "[heap]":
            return o.get("anonBytes", 0)
    return 0


def _nonheap_anon_bytes(mappings):
    """Anonymous RSS that is NOT on the glibc heap -- thread stacks, direct
    mmap, JIT code pages. None when no census is available."""
    if not isinstance(mappings, dict) or not mappings.get("available"):
        return None
    return max(0, mappings.get("totalAnonBytes", 0) - _heap_anon_bytes(mappings))


def report_mapping_owners(label_prefix, mappings):
    """T491 addendum (board B801): print WHO owns the anonymous RSS, by mapping.

    T490 measured that a zone load costs ~nothing in anonymous RSS (on the Pi
    nro went 192,284 kB -> 190,548 kB across the whole load ladder, qeynos2
    went DOWN) -- the entire gap to Will's 64 MB target is the pre-zone
    baseline. Most of that never goes through malloc (driver/EGL buffers,
    thread stacks, every shared library's writable data), so the allocator
    ledger files it under `unaccounted` by construction and no amount of
    MemoryScope work will move it. /proc/self/smaps names the owner, and this
    is the table the next unit plans the shrink from.
    """
    if not isinstance(mappings, dict) or not mappings.get("available"):
        return
    total = mappings.get("totalAnonBytes", 0)
    owners = mappings.get("owners", [])
    print(f"  [mappings] {label_prefix}: {mappings.get('mappings', 0)} mappings, "
          f"anon total {total / 1048576:.1f} MiB; biggest owners:")
    for o in owners[:10]:
        anon = o.get("anonBytes", 0)
        if not anon:
            continue
        pct = (100.0 * anon / total) if total else 0.0
        print(f"      {o.get('name', '?'):<28} {anon / 1048576:9.2f} MiB anon  "
              f"{pct:5.1f}%  ({o.get('mappings', 0)} maps, "
              f"{o.get('sizeBytes', 0) / 1048576:.1f} MiB mapped)")


def owned_bytes(rss_anon, gpu_driver, gpu_buffers=0, gpu_buffers_outside=False):
    """B933 + B978: willeq-owned memory, the figure the 64 MB target is read
    against -- owned = RssAnon - gpu_driver + gpu_buffers, the buffers added
    only when they live outside RssAnon (the Pi's Lima GEM; on llvmpipe they
    are inside gpu_driver already). Mirrors eqt::memory::finishTargetGap; the
    one Python definition is scripts/ci/owned_memory.py."""
    return owned_memory.owned(rss_anon, gpu_driver, gpu_buffers, gpu_buffers_outside)


def report_rss_target_gap(label_prefix, ledger, failures):
    """T491 addendum (Will, 2026-09-17): assert the 256 MB INTERIM CEILING and
    PRINT the gap to the 64 MB target, broken down by bucket, largest first.

    The target is not asserted: every zone is over it today, so gating it would
    make the gate uniformly red and tell nobody anything the printed gap does
    not. The ceiling is the failing bound, exactly as docs/decisions.md's T293
    envelope has it. The buckets are a PARTITION of RssAnon -- every category
    plus allocator slack plus unaccounted -- so the list below is the whole
    shrink problem, not just its heap-attributed part.
    """
    rss_anon = ledger.get("rssAnonBytes", 0)
    if not rss_anon:
        return
    # B933 (Will, 2026-09-29: "64 MB excludes driver memory") + B978 (Will,
    # 2026-10-01: GPU buffers count): the target is judged on willeq-OWNED
    # memory, RssAnon - gpu_driver + gpu_buffers (outside RssAnon only). Taken
    # from the components here, not from the JSON's overTargetBytes, so a
    # benchmark built before B978 prints the same figure. The ceiling stays on
    # RssAnon.
    gpu_driver = ledger.get("categories", {}).get("gpu_driver", {}).get("live", 0)
    owned, owned_anon, gpu_added, gpu_outside = owned_memory.owned_from_ledger_json(ledger)
    gpu_buffers = ledger.get("gpuBuffersBytes", 0)
    gpu_note = (f"gpu_buffers {gpu_buffers / 1048576:.1f} outside RssAnon, added" if gpu_outside
                else f"gpu_buffers {gpu_buffers / 1048576:.1f} inside gpu_driver, not added again")
    ceiling = ledger.get("rssCeilingBytes", RSS_CEILING_BYTES)
    target = ledger.get("rssTargetBytes", RSS_TARGET_BYTES)
    if rss_anon > ceiling:
        failures.append(
            f"{label_prefix}: RssAnon {rss_anon} is over the {ceiling / 1048576:.0f} MiB "
            f"interim ceiling by {(rss_anon - ceiling) / 1048576:.1f} MiB"
        )
    buckets = [(v.get("live", 0), k) for k, v in ledger.get("categories", {}).items()]
    buckets.append((ledger.get("heapUnchargedBytes", 0), "heap_uncharged"))
    buckets.append((ledger.get("mallocSlackBytes", 0), "malloc_slack"))
    buckets.append((ledger.get("unaccountedBytes", 0), "unaccounted"))
    if gpu_added:
        buckets.append((gpu_added, "gpu_buffers"))
    buckets = sorted(((b, n) for b, n in buckets if b and n != "gpu_driver"), reverse=True)
    label = owned_memory.OWNED_LABEL
    if owned <= target:
        print(f"  [rss-target] {label_prefix}: {label} {owned / 1048576:.1f} MiB "
              f"(RssAnon {rss_anon / 1048576:.1f}, gpu_driver {gpu_driver / 1048576:.1f}, "
              f"{gpu_note}) is WITHIN the {target / 1048576:.0f} MiB target")
        return
    gap = owned - target
    print(f"  [rss-target] {label_prefix}: {label} {owned / 1048576:.1f} MiB "
          f"(RssAnon {rss_anon / 1048576:.1f}, gpu_driver {gpu_driver / 1048576:.1f}, {gpu_note}), "
          f"target {target / 1048576:.0f} MiB, must shrink {gap / 1048576:.1f} MiB of owned memory "
          f"(whole-RssAnon gap {max(0, rss_anon - target) / 1048576:.1f} MiB, driver included); "
          f"ceiling {ceiling / 1048576:.0f} MiB on whole RssAnon "
          f"({'OVER' if rss_anon > ceiling else 'headroom ' + format((ceiling - rss_anon) / 1048576, '.1f') + ' MiB'})")
    for b, n in buckets[:8]:
        print(f"      {n:<18} {b / 1048576:9.2f} MiB  {100.0 * b / rss_anon:5.1f}% of RssAnon")
    print(f"      {'gpu_driver':<18} {gpu_driver / 1048576:9.2f} MiB  EXCLUDED from the gap "
          f"(driver memory, still reported)")


def assert_memory_ledger(label_prefix, memory, failures, max_unattributed_pct=None):
    """T491: assert the allocator ledger's two invariants on one memory dict.

    (i)  ledger.unattributedBytes / ledger.rssAnonBytes <= the bound;
    (ii) ledger.categories[c].live <= ledgerBudgets[c] for every budgeted c.

    A memory JSON without a "ledger" object comes from a benchmark built
    before T491 -- or with -DEQT_MEMORY_LEDGER=OFF -- and is a FAILURE, not a
    skip: the gate's memory section is meaningless without it. Same for
    ledgerEnabled == false.
    """
    if max_unattributed_pct is None:
        max_unattributed_pct = LEDGER_UNATTRIBUTED_MAX_PCT
    ledger = memory.get("ledger")
    if not isinstance(ledger, dict):
        failures.append(f"{label_prefix}: memory JSON has no \"ledger\" object "
                        f"(benchmark predates T491 or was built with EQT_MEMORY_LEDGER=OFF)")
        return
    if not ledger.get("ledgerEnabled", False):
        failures.append(f"{label_prefix}: memory ledger hooks are OFF in this benchmark "
                        f"(EQT_MEMORY_LEDGER=0); the gate requires them ON")
        return
    rss_anon = ledger.get("rssAnonBytes", 0)
    total = ledger.get("ledgerTotalBytes", 0)
    slack = ledger.get("mallocSlackBytes", 0)
    uncharged = ledger.get("heapUnchargedBytes", 0)
    cats = ledger.get("categories", {})
    top = sorted(((v.get("live", 0), k) for k, v in cats.items()), reverse=True)[:4]
    top_txt = ", ".join(f"{k} {v / 1048576:.1f} MiB" for v, k in top)
    slack_pct = (100.0 * slack / rss_anon) if rss_anon else 0.0

    # (1) the attribution ratchet: bytes charged with no scope on the thread.
    unowned = cats.get("unattributed", {}).get("live", 0)
    unowned_pct = (100.0 * unowned / total) if total else 0.0
    print(f"  [ledger] {label_prefix}: RssAnon={rss_anon} ledger={total} "
          f"unowned={unowned} ({unowned_pct:.3f}% of the ledger, bound "
          f"{max_unattributed_pct:.2f}%) glibc-internal={uncharged} "
          f"slack={slack} ({slack_pct:.1f}%) top: {top_txt}")
    if unowned_pct > max_unattributed_pct:
        failures.append(
            f"{label_prefix}: ledger has {unowned} bytes charged to no category "
            f"({unowned_pct:.3f}% of the ledger), over the {max_unattributed_pct:.2f}% bound "
            f"-- a subsystem is allocating without a MemoryScope"
        )
    # (2) the ledger must cover every RESIDENT heap byte. smaps' [heap] row is
    #     the resident anonymous heap; the ledger charges whole glibc chunks,
    #     so it is legitimately larger -- but never smaller.
    #
    #     T504: the resident heap is LIVE chunks plus FREED chunks glibc still
    #     holds (malloc_slack). Comparing the ledger alone against it was only
    #     ever true because a load freed almost nothing; once the load releases
    #     memory early (T504 fix 4 frees ~19 MB of raw texture bytes at archive
    #     load) the freed-but-resident pages were reported as "escaped the hooks"
    #     -- MEASURED qeynos2 coverage run: gap 11,982,608 B, slack 57,403,248 B.
    #     Slack is a named bucket with its own bound, so it counts as covered;
    #     what this check exists to catch is heap that is neither.
    heap_anon = _heap_anon_bytes(memory.get("mappings"))
    if heap_anon:
        covered = total + slack
        if covered < heap_anon:
            failures.append(
                f"{label_prefix}: ledger {total} + malloc_slack {slack} does not cover the "
                f"resident heap {heap_anon} -- {heap_anon - covered} bytes of heap escaped the hooks"
            )
        else:
            print(f"  [ledger] {label_prefix}: ledger + slack cover the resident heap "
                  f"({total} + {slack} >= [heap] anon {heap_anon})")
    # (3) non-heap anonymous RSS: what no allocator hook can ever see.
    nonheap = _nonheap_anon_bytes(memory.get("mappings"))
    if nonheap is not None:
        nh_pct = (100.0 * nonheap / rss_anon) if rss_anon else 0.0
        print(f"  [ledger] {label_prefix}: non-heap anon {nonheap} "
              f"({nh_pct:.1f}% of RssAnon, bound {LEDGER_NONHEAP_MAX_BYTES} bytes)")
        if nonheap > LEDGER_NONHEAP_MAX_BYTES:
            failures.append(
                f"{label_prefix}: non-heap anonymous RSS {nonheap} bytes is over the "
                f"{LEDGER_NONHEAP_MAX_BYTES}-byte bound (see the [mappings] table for the owner)"
            )
    # (4) allocator slack -- a glibc tuning bound, not an attribution one.
    if slack_pct > LEDGER_SLACK_MAX_PCT:
        failures.append(
            f"{label_prefix}: allocator slack {slack} bytes is {slack_pct:.1f}% of "
            f"RssAnon {rss_anon}, over the {LEDGER_SLACK_MAX_PCT:.1f}% bound"
        )
    report_rss_target_gap(label_prefix, ledger, failures)
    report_mapping_owners(label_prefix, memory.get("mappings"))
    budgets = memory.get("ledgerBudgets", {})
    checked, skipped = [], []
    for name, budget in budgets.items():
        live = cats.get(name, {}).get("live", 0)
        if not budget:
            skipped.append(name)
            continue
        checked.append(f"{name} {100.0 * live / budget:.0f}%")
        if live > budget:
            failures.append(
                f"{label_prefix}: ledger category {name} live {live} exceeds budget {budget}"
            )
    if checked:
        print(f"  [ledger-budget] {label_prefix}: " + ", ".join(checked))
    if skipped:
        print(f"  [ledger-budget] {label_prefix}: NO BUDGET CONFIGURED for " + ", ".join(skipped))


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

        # T293: budget assertions run on the coverage numbers regardless of
        # whether this section has ever been blessed -- this is the ONLY run
        # where placeableObjectsBytes/characterModelsBytes/eqe1MappedBytes are
        # nonzero, so it is the only run where their budgets mean anything.
        assert_memory_budgets(f"memory-coverage {zone}", memory, failures)
        # T491: the allocator ledger, on the heavier asset mix too.
        assert_memory_ledger(f"memory-coverage {zone}", memory, failures)

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
        # repeat runs under this exact asset mix (pre-B813 sample); B813 added
        # iceclad + timorous as the 4th and 5th zones, and both were
        # bit-identical across two coverage passes on this host's llvmpipe --
        # same discipline and same empirically-zero
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
                  f"untracked={untracked}  (T69 estimate gap, reported only; the ledger above is gated)")


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

            # (a) Budget assertions: usage <= the LIVE configured ceiling
            # (not the blessed one -- a budget is a config value, not a
            # measurement). T293 moved this out of the baseline-coupled loop
            # below; see assert_memory_budgets() for why that mattered.
            assert_memory_budgets(zone, memory, failures)

            # (d) T491: the allocator ledger -- unattributed bound + per-category
            # budgets. See assert_memory_ledger().
            assert_memory_ledger(zone, memory, failures)

            # (a') Exact-match the two originally-budgeted categories' usage
            # against their blessed values as well -- regression detection on
            # top of the budget check, as the texture cache has done since
            # H02. The T293 additions are omitted here because
            # MEMORY_BASELINE_FIELDS already exact-matches every one of them.
            for used_key, _limit_key, label in MEMORY_BUDGET_PAIRS:
                if used_key not in MEMORY_BUDGET_PAIRS_ALSO_BASELINED:
                    continue
                if used_key not in memory or used_key not in expected_mem:
                    continue  # field predates this bless (old manifest); skip rather than false-fail
                if memory[used_key] != expected_mem[used_key]:
                    failures.append(
                        f"{zone}: {label} bytes used mismatch (expected "
                        f"{expected_mem[used_key]}, got {memory[used_key]})"
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
                      f"untracked={untracked}  (T69 estimate gap, reported only; the ledger above is gated)")

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


# ====================================================== B1024: the Pi platform

def _pi_memory_fields(memory):
    """The per-zone memory block a Pi bless writes: the same fields bless()
    writes for x86 (the MEMORY_BUDGET_PAIRS usage+limit pairs present in the
    run, MEMORY_BASELINE_FIELDS, MEMORY_REPORT_ONLY_FIELDS) -- the Pi's own
    measurements of the same categories. p95CpuMsBaseline is deliberately
    NOT among them: Pi TIMING is H04's lane (scripts/pi/compare_pi.sh ->
    pi_bench_compare.py), not the pixel gate's."""
    mem = {}
    for used_key, limit_key, _label in MEMORY_BUDGET_PAIRS:
        if used_key in memory and limit_key in memory:
            mem[used_key] = memory[used_key]
            mem[limit_key] = memory[limit_key]
    for key in MEMORY_BASELINE_FIELDS:
        if key in memory:
            mem[key] = memory[key]
    for key in MEMORY_REPORT_ONLY_FIELDS:
        if key in memory:
            mem[key] = memory[key]
    return mem


def verify_pi(manifest, cfg, bench_cfg, production_args, goldens_dir=None):
    """B1024: verify the Pi set. ONE compare rule -- compare_images_exact,
    unchanged -- against the Pi's OWN goldens and section:

      pixels          every designated frame, pixel-exact vs
                      cfg.golden_root/<zone>/f<n>.png
      metrics         draws/stateChanges/polys exact vs section zones
      memory budgets  assert_memory_budgets (usage <= the live budget,
                      shared with the x86 verify)
      memory exact    MEMORY_BASELINE_FIELDS + MEMORY_BUDGET_PAIRS usage
                      exact vs the section's per-zone memory block
      ledger          assert_memory_ledger (the allocator ledger is a
                      platform fact, asserted on the Pi run too)
      maps proof      check_maps_text: libgallium under the shipped
                      LLVM-less prefix, libLLVM never mapped
      glVersion       the section's blessedUnder.glVersion is reported
                      (mismatch is a NOTE, exactly like the x86 verify --
                      a mesa point release is a re-bless bookkeeping, not a
                      regression)
    NOT asserted here: the p95CpuMsBaseline ceiling (Pi timing is H04's) and
    the memoryCoverage run (T80 is an x86-asset-mix measurement; the Pi
    coverage run is H04/B1006 territory, not this gate's).

    A section (manifest["platforms"]["pi"], or goldens_dir/manifest_pi.json
    when --goldens-dir names the captured set) that is missing fails NOT
    YET BLESSED with exit 1 -- there is no fallback to the x86 set.
    """
    failures = []
    # per-run scratch for the pi verify (diffs, maps proofs) -- never under
    # tests/goldens/, same rule as the x86 verify's ARTIFACTS_DIR
    out_dir = ARTIFACTS_DIR
    out_dir.mkdir(parents=True, exist_ok=True)

    section = cfg.section
    if section is None and goldens_dir is not None:
        candidate = Path(goldens_dir) / "manifest_pi.json"
        if candidate.exists():
            section = json.loads(candidate.read_text()).get("platforms", {}).get("pi")
    if section is None:
        print("[verify] pi: NOT YET BLESSED -- no platforms.pi section in the "
              "manifest (and no manifest_pi.json in --goldens-dir). The Pi "
              "set must be captured (--capture) and blessed before the Pi "
              "gate can verify; it never falls back to the x86 set.")
        return 1, failures

    # the section's zones (pi layout: zone configs live under section["zones"])
    zones = section.get("zones", [])
    by_zone = {z["zone"]: z for z in zones}
    manifest_zone_cfgs = {z["zone"]: z for z in manifest["zones"]}

    for zone_cfg in manifest_zone_cfgs.values():
        zone = zone_cfg["zone"]
        print(f"[verify] pi {zone} ...")
        if zone not in by_zone:
            failures.append(f"pi {zone}: NOT YET BLESSED -- no zone in the "
                            f"platforms.pi section")
            continue
        pi_cfg = by_zone[zone]
        if (pi_cfg.get("extraArgs") != zone_cfg.get("extraArgs", [])
                or pi_cfg.get("designatedFrames") != zone_cfg.get("designatedFrames", [0])):
            failures.append(
                f"pi {zone}: section poses/frames disagree with manifest zones "
                f"-- the Pi set must be the x86 gate's invocation, nothing else")
        try:
            png_paths, csv_rows, memory, stdout_text = run_benchmark(
                zone_cfg, bench_cfg, out_dir, production_args, cfg=cfg)
        except RuntimeError as e:
            failures.append(f"pi {zone}: {e}")
            continue

        # the Mesa-prefix proof (pi mode only; the x86 gate needs no proof)
        if cfg.maps_required:
            failures.extend(check_maps_text(out_dir / f"{zone}_maps.txt",
                                            mesa_prefix=cfg.maps_prefix))

        gl_version = extract_gl_version(stdout_text)
        expected_gl = section.get("blessedUnder", {}).get("glVersion")
        if gl_version and expected_gl and gl_version != expected_gl:
            print(f"  NOTE: pi GLES version differs from the Pi blessed baseline "
                  f"(blessed='{expected_gl}', current='{gl_version}') -- "
                  f"mesa moved, re-capture + re-bless the Pi set instead of "
                  f"treating diffs as regressions.")

        # 1. golden frames, pixel-exact against the Pi set
        for f, actual_path in png_paths.items():
            golden_path = golden_frame_path(cfg, zone, f)
            if not golden_path.exists():
                failures.append(f"pi {zone} frame {f}: no golden at {golden_path} "
                                f"(capture the Pi set first: --platform pi --capture DIR)")
                continue
            identical, diff_count, size = compare_images_exact(golden_path, actual_path)
            if not identical:
                diff_out = ARTIFACTS_DIR / f"pi_{zone}_f{f}_diff.png"
                save_diff_artifact(golden_path, actual_path, diff_out)
                failures.append(
                    f"pi {zone} frame {f}: PIXEL MISMATCH ({diff_count} differing pixels, "
                    f"size={size}). Artifacts: {actual_path}, {diff_out}")

        # 2. metrics (draws/stateChanges/polys) -- exact, per frame, the same
        #    rule as the x86 verify (which indexes csv_rows[f] directly). The
        #    frame must have a CSV row: a blessed section can only carry
        #    metrics for frames the run actually measured.
        for f_str, expected in pi_cfg.get("metrics", {}).items():
            f = int(f_str)
            if f >= len(csv_rows):
                failures.append(f"pi {zone} frame {f}: no CSV row to check "
                                f"metrics against")
                continue
            row = csv_rows[f]
            for key, csv_key in (("draws", "draws"), ("stateChanges", "state_changes"), ("polys", "polys")):
                actual_val = int(row[csv_key])
                if actual_val != expected[key]:
                    failures.append(
                        f"pi {zone} frame {f}: {key} mismatch (expected {expected[key]}, got {actual_val})")

        # 3. memory: budgets (shared rule) + exact match against the Pi section
        if memory:
            assert_memory_budgets(f"pi {zone}", memory, failures)
            assert_memory_ledger(f"pi {zone}", memory, failures)
            expected_mem = pi_cfg.get("memory")
            if not expected_mem:
                failures.append(f"pi {zone}: NOT YET BLESSED -- section zone has no "
                                f"memory block")
            else:
                for key, val in expected_mem.items():
                    if key in memory and memory[key] != val:
                        failures.append(
                            f"pi {zone}: memory {key} mismatch (blessed {val}, got {memory[key]})")
            # report-only line, same shape as the x86 verify
            rss = memory.get("processRssBytes")
            total = memory.get("totalEstimate")
            if rss:
                pct = (100.0 * total / rss) if (total is not None and rss) else 0.0
                print(f"  [memory] pi {zone}: tracked={total} RSS={rss} ({pct:.0f}%) "
                      f"(reported only; the ledger above is gated; Pi TIMING is H04's lane)")

    if failures:
        print("\n=== VISUAL GATE (pi): FAIL ===")
        for f in failures:
            print(f"  - {f}")
        return 1, failures

    print("\n=== VISUAL GATE (pi): PASS ===")
    return 0, failures


def capture_pi(manifest, cfg, bench_cfg, production_args, out_dir):
    """B1024: render the Pi set and stage the proposed bless. Writes into
    out_dir ONLY -- never under tests/goldens/:

      out_dir/<zone>/f<n>.png        the frames (the committed layout)
      out_dir/manifest_pi.json       {"platforms": {"pi": <the section>}} --
                                     blessedUnder (glRenderer/glVersion from
                                     the run) + per-zone metrics + memory
                                     (the x86 bless fields, minus
                                     p95CpuMsBaseline: Pi timing is H04's)
      out_dir/SHA256SUMS             every frame, "<sha256>  <zone>/<f>.png"
      out_dir/<zone>_maps.txt        the Mesa-prefix proof (Popen/poll_maps)
      out_dir/<zone>.log             the benchmark stdout per zone

    The orchestrator commits this staged set as tests/goldens/pi/<zone>/f<n>.png
    + manifest["platforms"]["pi"] from a captured set -- that commit is the
    Pi bless (Will's 2026-10-11 bless note on #344); --bless --platform pi is
    refused for that reason.
    """
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    failures = []
    section_zones = []
    gl_version = None
    gl_renderer = None

    for zone_cfg in manifest["zones"]:
        zone = zone_cfg["zone"]
        print(f"[capture] pi {zone} ...")
        # one run dir per zone: out_dir/<zone>/ (frames/, csv, mem, maps) --
        # run_benchmark computes the per-zone layout from out_dir, exactly as
        # verify_pi does, so the run's own artifacts sit beside the staged
        # frames and travel with the set
        try:
            png_paths, csv_rows, memory, stdout_text = run_benchmark(
                zone_cfg, bench_cfg, out_dir, production_args, cfg=cfg)
        except RuntimeError as e:
            failures.append(f"pi {zone}: {e}")
            continue

        # keep the run's stdout (the blessedUnder extraction lives in it)
        (out_dir / f"{zone}.log").write_text(stdout_text)

        failures.extend(check_maps_text(out_dir / f"{zone}_maps.txt",
                                        mesa_prefix=cfg.maps_prefix))

        v = extract_gl_version(stdout_text)
        if v and gl_version is None:
            gl_version = v
            m = re.match(r"\[([^\]]+)\]", v)
            gl_renderer = m.group(1) if m else v

        # The frames are ALREADY in the committed layout: run_benchmark's pi
        # dump prefix is out_dir/<zone>/, so <out_dir>/<zone>/f<n>.png is
        # exactly what the orchestrator commits as
        # tests/goldens/pi/<zone>/f<n>.png. Check every frame is present;
        # no copy step (a copy would be an unexplained rename).
        zone_dir = out_dir / zone
        for f in zone_cfg["designatedFrames"]:
            staged = zone_dir / f"f{f}.png"
            if not staged.exists():
                failures.append(f"pi {zone} frame {f}: staged frame missing "
                                f"{staged}")
                continue
            print(f"  staged {staged}")

        metrics = {}
        for f in zone_cfg["designatedFrames"]:
            row = csv_rows[f]
            metrics[str(f)] = {
                "draws": int(row["draws"]),
                "stateChanges": int(row["state_changes"]),
                "polys": int(row["polys"]),
            }
        section_zones.append({
            "zone": zone,
            "extraArgs": zone_cfg.get("extraArgs", []),
            "designatedFrames": zone_cfg.get("designatedFrames", [0]),
            "metrics": metrics,
            "memory": _pi_memory_fields(memory) if memory else {},
        })

    if failures:
        print("\n=== CAPTURE (pi): FAIL (set NOT staged) ===")
        for f in failures:
            print(f"  - {f}")
        return 1

    section = {
        "note": ("B1024: the Pi golden set's manifest section -- the same "
                 "per-zone fields bless() writes for x86, minus "
                 "p95CpuMsBaseline (Pi timing is H04's: scripts/pi/"
                 "compare_pi.sh). Committed by the orchestrator from a "
                 "--capture staging; a Pi golden moves only on a Pi re-bless "
                 "by Will. docs/ci.md 'Two golden sets: x86 and Pi (B1024)'."),
        "blessedUnder": {
            "glRenderer": gl_renderer or "(unknown -- see the zone logs)",
            "glVersion": gl_version or "(unknown -- see the zone logs)",
        },
        "zones": section_zones,
    }
    (out_dir / "manifest_pi.json").write_text(
        json.dumps({"platforms": {"pi": section}}, indent=2) + "\n")

    # SHA256SUMS over every staged frame, "<sha>  <zone>/<f>.png" (relative to
    # out_dir, so the file travels with the set)
    sums = []
    for zone_dir in sorted(out_dir.iterdir()):
        if not zone_dir.is_dir():
            continue
        for p in sorted(zone_dir.glob("f*.png")):
            h = hashlib.sha256(p.read_bytes()).hexdigest()
            sums.append(f"{h}  {p.relative_to(out_dir).as_posix()}")
    (out_dir / "SHA256SUMS").write_text("\n".join(sums) + "\n")

    print(f"[capture] pi set staged in {out_dir} "
          f"({len(section_zones)} zones, {len(sums)} frames). "
          f"NOT committed: the orchestrator blesses it into tests/goldens/pi/ "
          f"once Will's bless note lists these hashes.")
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


def select_zones(manifest, names):
    """B813 revert (Will, 2026-09-28): verify only the named subset of the
    manifest's zones, in BOTH the golden-image list and memoryCoverage.zones.

    .gitlab-ci.yml's one blocking `gate` job names all five manifest zones
    (from 2026-09-28 until B903/B904 fixed their overruns on 2026-09-29,
    iceclad + timorous ran in a separate non-blocking job; docs/decisions.md
    2026-09-28). It selects zones; it changes NO budget,
    NO baseline and NO assertion for a zone it runs. An unknown name is an
    error, never a silent no-op, and scripts/ci/test_visual_gate_ledger.py
    checks that the CI job's --zones list equals the manifest's zones
    exactly, so no zone can drop out of CI by editing the list.

    Returns (filtered_manifest, skipped_zone_names). Raises ValueError.
    """
    wanted = [n.strip() for n in names.split(",") if n.strip()]
    if not wanted:
        raise ValueError("--zones given an empty list")
    known = [z["zone"] for z in manifest["zones"]]
    unknown = [n for n in wanted if n not in known]
    if unknown:
        raise ValueError(f"--zones names zone(s) not in the manifest: {', '.join(unknown)} "
                         f"(manifest zones: {', '.join(known)})")
    out = dict(manifest)
    out["zones"] = [z for z in manifest["zones"] if z["zone"] in wanted]
    coverage = manifest.get("memoryCoverage")
    if coverage:
        out["memoryCoverage"] = dict(coverage)
        out["memoryCoverage"]["zones"] = [z for z in coverage["zones"] if z["zone"] in wanted]
    skipped = [n for n in known if n not in wanted]
    return out, skipped


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bless", action="store_true", help="Regenerate goldens + manifest + contact sheet")
    parser.add_argument("--zones", default=None,
                        help="Comma-separated subset of the manifest's zones to VERIFY "
                             "(default: all). Not valid with --bless. See select_zones().")
    parser.add_argument("--platform", choices=["x86", "pi"], default="x86",
                        help="B1024: which golden set the gate runs. x86 (default) is "
                             "today's gate, byte for byte (the CI job passes no "
                             "--platform). pi = the Pi set: tests/goldens/pi/ + "
                             "manifest platforms.pi, the --bench binary on the Orange "
                             "Pi, --drm, and the Mesa-prefix maps proof.")
    parser.add_argument("--bench", default=None,
                        help="B1024: the benchmark binary (REQUIRED with --platform pi: "
                             "the published ci-arm/<sha>/bin/gles2_zone_benchmark-nollvm).")
    parser.add_argument("--goldens-dir", default=None,
                        help="B1024 (VERIFY only): read the goldens from DIR instead of "
                             "the platform's committed location; for pi, DIR/manifest_pi.json "
                             "supplies the section when the manifest has none. This is the "
                             "run2-vs-run1 determinism check and QA re-run path.")
    parser.add_argument("--capture", default=None, metavar="DIR",
                        help="B1024: render the set and stage the proposed bless into DIR "
                             "(DIR/<zone>/f<n>.png + manifest_pi.json + SHA256SUMS + logs "
                             "+ maps proof). NEVER writes under tests/goldens/ -- the "
                             "orchestrator commits the captured set (that commit is the bless).")
    args = parser.parse_args()

    if args.bless and args.zones is not None:
        print("ERROR: --zones cannot be combined with --bless (a bless always covers every zone)")
        return 2
    if args.bless and args.platform == "pi":
        print("ERROR: --bless --platform pi is refused -- Pi goldens are committed by the "
              "orchestrator from a captured set; use --platform pi --capture DIR. "
              "(Bless is Will's; the orchestrator's commit of the blessed set is that bless.)")
        return 2
    if args.capture and args.goldens_dir is not None:
        print("ERROR: --capture and --goldens-dir are mutually exclusive (capture stages, "
              "verify reads)")
        return 2
    if args.capture and args.bless:
        print("ERROR: --capture and --bless are mutually exclusive")
        return 2
    if args.platform == "pi" and not args.bench:
        print("ERROR: --platform pi requires --bench PATH (the published "
              "ci-arm/<sha>/bin/gles2_zone_benchmark-nollvm)")
        return 2

    bench_bin = Path(args.bench) if args.bench else BENCH_BIN
    if not bench_bin.exists():
        if args.platform == "pi":
            print(f"SKIP: {bench_bin} not found on this host -- run the pi capture ON THE "
                  f"ORANGE PI (under pi_lock_take), where the published ci-arm/<sha>/ "
                  f"binary lives (scripts/pi/env.sh)")
            return 125  # ctest SKIP_RETURN_CODE (see scripts/ci/visual_gate.sh)
        print(f"SKIP: {bench_bin} not found (build-gles2/ absent -- run scripts/build-gles2-x86.sh first)")
        return 125  # ctest SKIP_RETURN_CODE (see scripts/ci/visual_gate.sh)

    manifest = load_manifest()
    bench_cfg = manifest["benchmark"]
    production_args = manifest.get("productionConfig", {}).get("args", [])

    if args.bless:
        return bless(manifest)

    cfg = platform_config(manifest, args.platform, args.bench, args.goldens_dir)

    if args.capture is not None:
        return capture_pi(manifest, cfg, bench_cfg, production_args, args.capture)

    if args.platform == "pi":
        rc, _failures = verify_pi(manifest, cfg, bench_cfg, production_args,
                                  goldens_dir=args.goldens_dir)
        return rc

    if args.zones is not None:
        try:
            manifest, skipped = select_zones(manifest, args.zones)
        except ValueError as e:
            print(f"ERROR: {e}")
            return 2
        print(f"[verify] --zones: verifying {', '.join(z['zone'] for z in manifest['zones'])}; "
              f"NOT verified by this invocation: {', '.join(skipped) or '(none)'}")
    return verify(manifest)


if __name__ == "__main__":
    sys.exit(main())
