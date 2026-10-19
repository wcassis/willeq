#!/usr/bin/env python3
"""B1024 (#344) -- the visual gate's per-platform resolution, pinned.

Three jobs, one file, no benchmark, no build artefact, no display:

--write-plan PATH
    Record what the x86 gate DOES (the x86 fixture, generated in step 1 on
    the UNTOUCHED tree, before any edit of visual_gate.py). gate.subprocess.
    run and .Popen are patched to RECORD the exact command and to write the
    files the gate expects (PNGs, CSV, memory JSON); run_benchmark() is
    called for every manifest zone; one line per zone (the command, temp dir
    -> <OUT>, repo root -> <REPO>) and one line per designated frame (the
    x86 golden path, repo-relative) are written to PATH.

--verify-x86
    Re-run the same recording on the CURRENT tree and compare byte for byte
    with the committed fixture (tests/data/b1024/x86_gate_plan.txt): the x86
    golden paths and the benchmark command must be byte-identical to
    origin/fable's -- the CI `gate` job passes no --platform, so the
    platform work changed nothing for it (acceptance 1).

--check-pi
    The pi half (RED before the implementation, GREEN after): platform_config
    returns the tests/goldens/pi/<zone>/f<n>.png paths + manifest["platforms"]
    ["pi"] + the --bench binary + the Pi EQ path + --drm while the x86 paths
    stay untouched; a verify with no platforms.pi section fails exit 1 with
    the NOT YET BLESSED message and never falls back to the x86 set;
    --bless --platform pi is refused; --platform pi without --bench is a
    usage error; the Mesa-prefix maps check accepts the nollvm prefix and
    refuses libLLVM or a system libgallium; --capture writes
    DIR/<zone>/f<n>.png + manifest_pi.json + SHA256SUMS + logs and never
    writes under tests/goldens/; a blessed Pi set verifies 15/15-exact
    (stubbed) with no p95 assertion.

--check-pi also runs check_mac() (B1044): the Mac set's own section.

No-arg mode: --verify-x86 AND --check-pi (the ctest entry point).
"""
import argparse
import hashlib
import importlib.util
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest.mock as mock
from contextlib import redirect_stdout
from pathlib import Path

HERE = Path(__file__).resolve().parent
GATE = HERE / "visual_gate.py"
REPO = HERE.parent.parent
FIXTURE = REPO / "tests" / "data" / "b1024" / "x86_gate_plan.txt"

PI_MESA_PREFIX = "/home/user/mesa-nollvm-25.2.8"
# Per-uid (B1087): the ThinkPad runs this test as gitlab-runner (CI) AND as
# user (scripts/hermes/rbuild); a shared /tmp dir left by one is unwritable
# for the other.
PI_BENCH = Path(f"/tmp/b1024_fake_pi-{os.getuid()}/gles2_zone_benchmark-nollvm")
PI_GL = "OpenGL ES 3.0 Mesa 25.2.8 (Lima)"


def load_gate():
    """The load_gate() precedent from scripts/ci/test_visual_gate_ledger.py:
    load visual_gate.py as a module by path, so this file drives the real
    gate's functions without a build artefact."""
    spec = importlib.util.spec_from_file_location("visual_gate", GATE)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {GATE}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _png_bytes():
    """A valid 1x1 red PNG (PIL decodes it; the pixels are irrelevant to the
    plan -- the plan records paths and commands, not image bytes)."""
    from PIL import Image
    buf = io.BytesIO()
    Image.new("RGB", (1, 1), (255, 0, 0)).save(buf, format="PNG")
    return buf.getvalue()


def _csv_text(frames):
    """Rows 0..frames-1, so csv_rows[f] is valid for every designated frame
    and the cpu_proc_ms column cpu_ms_series() demands is present. Values are
    fixed sentinels: what is under test is the INVOCATION, not the numbers."""
    lines = ["frame,draws,state_changes,polys,cpu_proc_ms,cpu_ms"]
    for i in range(frames):
        lines.append(f"{i},{100 + i},{10 + i},{1000 + i},1.5,2.5")
    return "\n".join(lines) + "\n"


def _mem_json_text():
    return json.dumps({
        "textureCacheBytesUsed": 1, "textureCacheBytesLimit": 1,
        "textureCount": 1,
        "eqe1MappedBytes": 0, "eqe1ArchiveCount": 0,
        "zoneGeometryBytes": 1, "placeableObjectsBytes": 1,
        "s3dZoneSourceBytes": 1, "gpuTextureBytes": 1, "vboEboBytes": 1,
        "characterModelsBytes": 1, "totalEstimate": 1,
        "processRssBytes": 1, "untrackedBytes": 0,
        "ledger": {"ledgerEnabled": True, "rssAnonBytes": 0,
                   "ledgerTotalBytes": 0, "unattributedBytes": 0,
                   "mallocSlackBytes": 0, "heapUnchargedBytes": 0,
                   "categories": {}, "ledgerBudgets": {}},
        "ledgerBudgets": {},
    })


def install_fake_subprocess(gate, recorded, *, maps_text=None):
    """Patch gate.subprocess.run / .Popen to RECORD the command and write
    the files run_benchmark() expects them to have produced. FakePopen plays
    the Pi launch: it exits 0 and its maps proof (maps_text, or a good
    nollvm-prefix default) is written where poll_maps() will find it."""
    png = _png_bytes()

    def fake_run(cmd, *a, **kw):
        recorded.append(list(cmd))
        args = list(cmd)
        if "--dump-png-prefix" in args:
            prefix = args[args.index("--dump-png-prefix") + 1]
            for f in args[args.index("--dump-frames") + 1].split(","):
                Path(f"{prefix}_f{f}.png").write_bytes(png)
        if "--csv" in args:
            Path(args[args.index("--csv") + 1]).write_text(_csv_text(60))
        if "--memory-json" in args:
            Path(args[args.index("--memory-json") + 1]).write_text(_mem_json_text())
        return subprocess.CompletedProcess(cmd, 0,
                                           stdout=f"[{PI_GL}]", stderr="")

    def fake_popen(cmd, *a, **kw):
        recorded.append(list(cmd))
        p = mock.Mock()
        p.pid = 987654
        p.returncode = 0
        p.poll.return_value = 0
        if maps_text is not None:
            mf = Path(kw.get("maps_path") or f"/tmp/b1024_maps_{id(cmd)}.txt")
            mf.parent.mkdir(parents=True, exist_ok=True)
            mf.write_text(maps_text)
            p.maps_path = str(mf)
        return p

    gate.subprocess.run = fake_run
    gate.subprocess.Popen = fake_popen


# ============================================================ the x86 fixture

def write_plan(path):
    """Record the x86 invocation for every manifest zone into PATH."""
    gate = load_gate()
    manifest = gate.load_manifest()
    recorded = []
    tmp = Path(tempfile.mkdtemp(prefix="b1024_plan_"))
    try:
        install_fake_subprocess(gate, recorded)
        bench_cfg = manifest["benchmark"]
        production_args = manifest.get("productionConfig", {}).get("args", [])
        lines = []
        for zone_cfg in manifest["zones"]:
            recorded.clear()
            gate.run_benchmark(zone_cfg, bench_cfg, tmp, production_args)
            assert len(recorded) == 1, \
                f"expected exactly one subprocess call for {zone_cfg['zone']}, got {len(recorded)}"
            cmd = recorded[0]
            norm = " ".join(
                ("<OUT>" if c.startswith(str(tmp)) else
                 "<REPO>" if c.startswith(str(gate.PROJECT_DIR)) else c)
                for c in cmd)
            lines.append(f"zone {zone_cfg['zone']} {norm}")
            for f in zone_cfg["designatedFrames"]:
                g = gate.GOLDENS_DIR / f"{zone_cfg['zone']}_f{f}.png"
                lines.append(f"frame {zone_cfg['zone']} {f} {g.relative_to(gate.PROJECT_DIR).as_posix()}")
        out = Path(path)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text("\n".join(lines) + "\n")
        print(f"[plan] wrote {out} ({len(lines)} lines)")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def verify_x86_plan():
    """Re-record on the current tree; must equal the committed fixture
    byte for byte."""
    gate = load_gate()
    with tempfile.TemporaryDirectory(prefix="b1024_verify_") as td:
        plan = Path(td) / "plan.txt"
        write_plan(plan)
        cur = plan.read_bytes()
    if not FIXTURE.exists():
        print(f"FAIL: fixture missing: {FIXTURE} (generate it with --write-plan)")
        return 1
    want = FIXTURE.read_bytes()
    if cur != want:
        print("FAIL: x86 gate plan differs from the fixture")
        cur_lines = cur.decode().splitlines()
        want_lines = want.decode().splitlines()
        for i in range(max(len(cur_lines), len(want_lines))):
            c = cur_lines[i] if i < len(cur_lines) else "<missing>"
            w = want_lines[i] if i < len(want_lines) else "<missing>"
            if c != w:
                print(f"  line {i + 1}:\n    now : {c}\n    was : {w}")
        return 1
    print(f"ok: x86 gate plan byte-identical to {FIXTURE}")
    return 0


# =============================================================== the pi half

def _pi_section(manifest):
    """A blessed-style platforms.pi section for EVERY manifest zone: the same
    per-zone fields bless() writes for x86, minus p95CpuMsBaseline, plus its
    own blessedUnder. metrics match the stubbed CSV rows (100+i / 10+i /
    1000+i per designated frame), so the exact-match checks pass when they
    run."""
    zones = []
    for z in manifest["zones"]:
        metrics = {str(f): {"draws": 100 + f, "stateChanges": 10 + f, "polys": 1000 + f}
                   for f in z["designatedFrames"]}
        zones.append({
            "zone": z["zone"],
            "extraArgs": z.get("extraArgs", []),
            "designatedFrames": z["designatedFrames"],
            "metrics": metrics,
            "memory": {"textureCacheBytesUsed": 100,
                       "textureCacheBytesLimit": 200,
                       "textureCount": 1},
        })
    return {
        "glRenderer": "Mali-400 (Lima)",
        "glVersion": PI_GL,
        "zones": zones,
    }


def _stub_benchmark(gate, zone_cfg, out_dir):
    """What a real benchmark invocation would have produced for ONE zone:
    the RUN-layout PNGs (pi: <out_dir>/<zone>/f<n>.png -- the committed
    layout, since run_benchmark's pi dump prefix is <out_dir>/<zone>),
    matching CSV rows, a memory JSON with the stub section's memory values
    (so the exact-match memory check passes when it runs), and the zone maps
    proof (the real one comes from Popen/poll_maps; here the content is the
    good nollvm text)."""
    zone = zone_cfg["zone"]
    frames = zone_cfg["designatedFrames"]
    prefix = Path(out_dir) / zone
    prefix.mkdir(parents=True, exist_ok=True)
    pngs = {}
    for f in frames:
        p = prefix / f"f{f}.png"
        p.write_bytes(_png_bytes())
        pngs[f] = p
    (Path(out_dir) / f"{zone}_maps.txt").write_text(_good_maps_text())
    csv = [{"frame": str(i), "draws": str(100 + i), "state_changes": str(10 + i),
            "polys": str(1000 + i), "cpu_proc_ms": "1.5", "cpu_ms": "2.5"}
           for i in range(60)]
    mem = json.loads(_mem_json_text())
    mem.update({"textureCacheBytesUsed": 100, "textureCacheBytesLimit": 200,
                "textureCount": 1,
                # the report-only fields, present in every real run: check
                # 10 relies on them being in the RUN's memory so that a
                # disagreeing SECTION value must not fail the verify
                "processRssBytes": 99999, "untrackedBytes": 88888})
    return pngs, csv, mem, f"[{PI_GL}]"


def _good_maps_text():
    # /proc/pid/maps line format: start-end perms offset dev inode path
    # (6 fields -- the path is field 6, so the fixtures must carry all six)
    return (f"00400000-00410000 r-xp 00010000 fe:00 1234 {PI_MESA_PREFIX}/lib/libgallium.so.0.0.0\n"
            f"00410000-00411000 r-xp 00000000 fe:00 1235 {PI_MESA_PREFIX}/lib/libEGL.so.1\n")


def check_pi():
    gate = load_gate()
    failures = []
    if not hasattr(gate, "platform_config"):
        print("FAIL: visual_gate.py has no platform_config (pre-B1024)")
        return 1

    manifest = gate.load_manifest()
    PI_BENCH.parent.mkdir(parents=True, exist_ok=True)
    try:
        PI_BENCH.write_bytes(b"")

        # 1. the resolvers: x86 unchanged, pi per the design
        x86 = gate.platform_config(manifest, "x86", None, None)
        if x86.golden_root != gate.GOLDENS_DIR:
            failures.append(f"x86 golden_root {x86.golden_root} != {gate.GOLDENS_DIR}")
        if x86.section is not manifest["zones"]:
            failures.append("x86 section is not manifest['zones']")
        if x86.bench != gate.BENCH_BIN:
            failures.append(f"x86 bench {x86.bench} != {gate.BENCH_BIN}")
        if x86.eq_path != gate.EQ_PATH:
            failures.append(f"x86 eq_path {x86.eq_path} != {gate.EQ_PATH}")
        if x86.extra_bench_args != []:
            failures.append(f"x86 extra_bench_args {x86.extra_bench_args} != []")
        if x86.maps_required:
            failures.append("x86 maps_required should be False")
        if gate.golden_frame_path(x86, "qeynos2", 0) != gate.GOLDENS_DIR / "qeynos2_f0.png":
            failures.append(f"x86 golden frame path moved: {gate.golden_frame_path(x86, 'qeynos2', 0)}")

        pi = gate.platform_config(manifest, "pi", str(PI_BENCH), None)
        if pi.golden_root != gate.GOLDENS_DIR / "pi":
            failures.append(f"pi golden_root {pi.golden_root} != {gate.GOLDENS_DIR / 'pi'}")
        if pi.section is not manifest.get("platforms", {}).get("pi"):
            failures.append("pi section is not manifest['platforms']['pi']")
        if pi.bench != PI_BENCH:
            failures.append(f"pi bench {pi.bench} != {PI_BENCH}")
        if pi.eq_path != Path("/mnt/projects/EverQuestP1999"):
            failures.append(f"pi eq_path {pi.eq_path} != /mnt/projects/EverQuestP1999")
        if pi.extra_bench_args != ["--drm"]:
            failures.append(f"pi extra_bench_args {pi.extra_bench_args} != ['--drm']")
        if not pi.maps_required:
            failures.append("pi maps_required should be True")
        if gate.golden_frame_path(pi, "qeynos2", 0) != gate.GOLDENS_DIR / "pi" / "qeynos2" / "f0.png":
            failures.append(f"pi golden frame path {gate.golden_frame_path(pi, 'qeynos2', 0)}")

        # 2. --platform pi without --bench: usage error (exit 2)
        rc, out = _run_main(gate, ["--platform", "pi"])
        if rc != 2:
            failures.append(f"pi without --bench: rc={rc} out={out!r}")

        # 3. --bless --platform pi: refused (exit 2, names the orchestrator + --capture)
        rc, out = _run_main(gate, ["--platform", "pi", "--bench", str(PI_BENCH), "--bless"])
        if rc != 2 or "orchestrator" not in out or "--capture" not in out:
            failures.append(f"--bless --platform pi: rc={rc} out={out!r}")

        # 4. no platforms.pi section -> NOT YET BLESSED, exit 1, no x86
        #    fallback. BLESS-STATE-SAFE: since 55ec17b3 the real manifest
        #    carries a blessed platforms.pi, so this path is exercised from
        #    a manifest WITHOUT the section (the pre-bless state the check
        #    was written for), not from the real manifest.
        unsectioned = json.loads(json.dumps(manifest))
        unsectioned.get("platforms", {}).pop("pi", None)
        with mock.patch.object(gate, "load_manifest", return_value=unsectioned):
            rc, out = _run_main(gate, ["--platform", "pi", "--bench", str(PI_BENCH)])
        if rc != 1 or "NOT YET BLESSED" not in out:
            failures.append(f"missing platforms.pi: rc={rc} out={out!r}")
        if "tests/goldens/qeynos2_f" in out or "no golden at" in out:
            failures.append("missing platforms.pi fell back to the x86 set")

        # 5. --goldens-dir supplies the goldens + section when the manifest
        #    lacks one: the run2-vs-run1 determinism check and QA re-runs use
        #    it. BLESS-STATE-SAFE: the real manifest carries platforms.pi
        #    since 55ec17b3, and verify_pi() only falls back to
        #    <goldens-dir>/manifest_pi.json when cfg.section is None -- so
        #    the run above a manifest WITHOUT the section, which is exactly
        #    the state in which a --goldens-dir capture supplies its section.
        #    The stub section still has to match the stub's metrics, so the
        #    real verify path (pixels, metrics, memory, maps) runs against
        #    the goldens-dir manifest, and a broken verify still FAILs.
        with tempfile.TemporaryDirectory(prefix="b1024_goldens_") as td:
            section = _pi_section(manifest)
            for z in manifest["zones"]:
                (Path(td) / z["zone"]).mkdir()
                for i in z["designatedFrames"]:
                    (Path(td) / z["zone"] / f"f{i}.png").write_bytes(_png_bytes())
            (Path(td) / "manifest_pi.json").write_text(
                json.dumps({"platforms": {"pi": section}}))
            cfg = gate.platform_config(manifest, "pi", str(PI_BENCH), td)
            if cfg.golden_root != Path(td):
                failures.append(f"pi --goldens-dir golden_root {cfg.golden_root} != {td}")
            unsectioned = json.loads(json.dumps(manifest))
            unsectioned.get("platforms", {}).pop("pi", None)
            with mock.patch.object(gate, "load_manifest", return_value=unsectioned):
                rc, out = _run_main(gate, ["--platform", "pi", "--bench", str(PI_BENCH),
                                           "--goldens-dir", td])
            if rc != 0:
                failures.append(f"pi --goldens-dir verify (blessed stub): rc={rc} out={out!r}")
            elif "FAIL" in out:
                failures.append(f"pi --goldens-dir verify reported FAIL: {out!r}")

        # 6. --capture: everything lands in DIR, nothing under tests/goldens/
        with tempfile.TemporaryDirectory(prefix="b1024_cap_") as td:
            outdir = Path(td) / "cap"
            written = []
            real_write_bytes = Path.write_bytes

            def spy(self, data, *a, **kw):
                written.append(str(self))
                return real_write_bytes(self, data, *a, **kw)

            with mock.patch.object(Path, "write_bytes", spy):
                rc, out = _run_main(gate, ["--platform", "pi", "--bench", str(PI_BENCH),
                                           "--capture", str(outdir)])
            if rc != 0:
                failures.append(f"pi --capture: rc={rc} out={out!r}")
            for p in (outdir / "qeynos2" / "f0.png", outdir / "manifest_pi.json",
                      outdir / "SHA256SUMS", outdir / "qeynos2_maps.txt"):
                if not p.exists():
                    failures.append(f"pi --capture missing {p}")
            # the capture writes NO frames/ sub-layout: the staged frames are
            # the committed layout <zone>/f<n>.png, unrenamed (the benchmark's
            # own writeImageToFile does not mkdir, so a different prefix used
            # to fail "FAILED to save" on the Pi)
            if (outdir / "qeynos2" / "frames").exists():
                failures.append("pi --capture wrote a frames/ sub-layout "
                                "(the committed layout is <zone>/f<n>.png)")
            for w in written:
                if str(gate.GOLDENS_DIR) in w:
                    failures.append(f"pi --capture wrote under tests/goldens/: {w}")
            if (outdir / "qeynos2" / "f0.png").exists():
                sha = hashlib.sha256((outdir / "qeynos2" / "f0.png").read_bytes()).hexdigest()
                sums = (outdir / "SHA256SUMS").read_text()
                if f"{sha}  qeynos2/f0.png" not in sums:
                    failures.append(f"SHA256SUMS lacks the frame: {sums!r}")
            mpi = json.loads((outdir / "manifest_pi.json").read_text())
            pi_sec = mpi.get("platforms", {}).get("pi", mpi)
            if "p95CpuMsBaseline" in pi_sec or any(
                    "p95CpuMsBaseline" in z for z in pi_sec["zones"]):
                failures.append("manifest_pi.json carries p95CpuMsBaseline (it must not)")
            if pi_sec.get("blessedUnder", {}).get("glVersion") != PI_GL:
                failures.append(f"manifest_pi.json blessedUnder: {pi_sec.get('blessedUnder')!r}")
            for k in ("metrics", "memory", "designatedFrames"):
                if k not in pi_sec["zones"][0]:
                    failures.append(f"manifest_pi.json zone lacks {k}")

        # 7. the Mesa-prefix maps proof (pure text check)
        os.environ["WILLEQ_MESA_NOLLLVM_PREFIX"] = PI_MESA_PREFIX
        with tempfile.TemporaryDirectory(prefix="b1024_maps_") as td:
            cases = (
                (_good_maps_text(), False, "nollvm prefix accepted"),
                (_good_maps_text() + f"00500000-00510000 r-xp 00000000 fe:00 1236 {PI_MESA_PREFIX}/lib/libLLVM-20.so\n",
                 True, "libLLVM mapped"),
                (f"00400000-00410000 r-xp 00010000 fe:00 1234 /usr/lib/arm-linux-gnueabihf/libgallium.so.0.0.0\n",
                 True, "system libgallium"),
            )
            for text, expect_fail, why in cases:
                mp = Path(td) / "m.txt"
                mp.write_text(text)
                got = gate.check_maps_text(mp)
                if (len(got) > 0) != expect_fail:
                    failures.append(f"maps check ({why}): {got!r}")

        # 8. a blessed manifest verifies: exact pixels + section equality, and
        #    the p95 ceiling is NOT asserted on the Pi (stub cpu_ms = 1.5,
        #    section carries no p95CpuMsBaseline at all)
        with tempfile.TemporaryDirectory(prefix="b1024_blessed_") as td:
            gd = Path(td) / "goldens"
            section = _pi_section(manifest)
            for z in manifest["zones"]:
                (gd / z["zone"]).mkdir(parents=True)
                for i in z["designatedFrames"]:
                    (gd / z["zone"] / f"f{i}.png").write_bytes(_png_bytes())
            tmp_manifest = {"benchmark": {"resolution": {"width": 800, "height": 600},
                                          "warmup": 5, "frames": 60},
                            "blessedUnder": {"glRenderer": "llvmpipe", "glVersion": "x"},
                            "zones": manifest["zones"], "platforms": {"pi": section}}
            (Path(td) / "manifest.json").write_text(json.dumps(tmp_manifest))
            with mock.patch.object(gate, "load_manifest", return_value=tmp_manifest):
                rc, out = _run_main(gate, ["--platform", "pi", "--bench", str(PI_BENCH),
                                           "--goldens-dir", str(gd)])
            if rc != 0 or "FAIL" in out:
                failures.append(f"blessed pi verify: rc={rc} out={out!r}")
            if "p95" in out:
                failures.append(f"pi verify asserted the p95 ceiling: {out!r}")

            # 9. the metrics check actually runs: a blessed section whose
            #    metrics disagree with the run's CSV must FAIL with the
            #    mismatch named (pre-fix the guard was dead and metrics were
            #    silently never checked)
            bad_manifest = json.loads(json.dumps(tmp_manifest))
            bz = bad_manifest["platforms"]["pi"]["zones"][0]
            bf = str(bz["designatedFrames"][0])
            bz["metrics"][bf]["draws"] += 1
            with mock.patch.object(gate, "load_manifest", return_value=bad_manifest):
                rc, out = _run_main(gate, ["--platform", "pi", "--bench", str(PI_BENCH),
                                           "--goldens-dir", str(gd)])
            if rc != 1 or "draws mismatch" not in out:
                failures.append(f"pi verify did not catch the metrics "
                                f"mismatch: rc={rc} out={out!r}")

            # 10. memory exact-match follows the x86 verify's rule: the
            #     REPORT-ONLY fields (processRssBytes, untrackedBytes) are
            #     recorded in the section but never asserted -- they are not
            #     cross-run deterministic (the Pi's own run2-vs-run1 verify
            #     measured a ~1% drift in exactly these two). A section whose
            #     report-only fields disagree with the run must still PASS.
            rss_manifest = json.loads(json.dumps(tmp_manifest))
            rz = rss_manifest["platforms"]["pi"]["zones"][0]
            rz["memory"]["processRssBytes"] = 1
            rz["memory"]["untrackedBytes"] = 1
            with mock.patch.object(gate, "load_manifest", return_value=rss_manifest):
                rc, out = _run_main(gate, ["--platform", "pi", "--bench", str(PI_BENCH),
                                           "--goldens-dir", str(gd)])
            if rc != 0 or "processRssBytes mismatch" in out \
                    or "untrackedBytes mismatch" in out:
                failures.append(f"pi verify asserted the report-only memory "
                                f"fields: rc={rc} out={out!r}")
            # ...while a BASELINE field still exact-matches (the rule did not
            # just go soft): textureCount in the stub section (1) vs a
            # section value of 2 must FAIL.
            tc_manifest = json.loads(json.dumps(tmp_manifest))
            tz = tc_manifest["platforms"]["pi"]["zones"][0]
            tz["memory"]["textureCount"] = 2
            with mock.patch.object(gate, "load_manifest", return_value=tc_manifest):
                rc, out = _run_main(gate, ["--platform", "pi", "--bench", str(PI_BENCH),
                                           "--goldens-dir", str(gd)])
            if rc != 1 or "textureCount mismatch" not in out:
                failures.append(f"pi verify did not exact-match the baseline "
                                f"memory field: rc={rc} out={out!r}")

    finally:
        try:
            PI_BENCH.unlink(missing_ok=True)
            PI_BENCH.parent.rmdir()
        except OSError:
            pass

    if failures:
        print("FAIL: pi platform checks")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("ok: pi resolution, bless refusal, NOT YET BLESSED, --goldens-dir, "
          "--capture, maps proof, blessed verify")
    return 0


def _run_main(gate, argv, *, maps_text=None):
    """Invoke gate.main() with the benchmark layer stubbed (subprocess
    recording + a stubbed run_benchmark + stubbed coverage run); return
    (exit_code, printed). poll_maps is stubbed the way a LIVE pi process
    with the nollvm prefix up would behave: it writes the maps proof and
    reports success (maps_text overrides, for a bad-proof run)."""
    gate.BENCH_BIN = Path(tempfile.mkdtemp(prefix="b1024_bin_")) / "gles2_zone_benchmark"
    gate.BENCH_BIN.write_bytes(b"")
    gate.ARTIFACTS_DIR = Path(tempfile.mkdtemp(prefix="b1024_art_"))
    recorded = []
    install_fake_subprocess(gate, recorded, maps_text=_good_maps_text())

    def fake_poll_maps(proc, maps_path, *a, **kw):
        mp = Path(maps_path)
        mp.parent.mkdir(parents=True, exist_ok=True)
        mp.write_text(maps_text if maps_text is not None else _good_maps_text())
        return True

    with mock.patch.object(gate, "run_benchmark",
                           lambda zc, bc, out, pa=None, cfg=None: _stub_benchmark(gate, zc, out)), \
         mock.patch.object(gate, "poll_maps", fake_poll_maps), \
         mock.patch.object(gate, "run_memory_coverage_benchmark", lambda *a, **kw: None), \
         mock.patch.object(gate, "assert_memory_ledger", lambda *a, **kw: None), \
         mock.patch("PIL.Image.open",
                    side_effect=lambda p: mock.Mock(size=(16, 16),
                                                    convert=lambda m: mock.Mock(size=(16, 16)))), \
         mock.patch("PIL.ImageChops.difference",
                    side_effect=lambda *a, **kw: mock.Mock(getbbox=lambda: None)):
        buf = io.StringIO()
        old_argv = sys.argv
        sys.argv = ["visual_gate.py"] + argv
        try:
            with redirect_stdout(buf):
                try:
                    rc = gate.main()
                except SystemExit as e:
                    rc = e.code if isinstance(e.code, int) else 2
        finally:
            sys.argv = old_argv
    return rc, buf.getvalue()


def check_mac():
    """B1044 (#364, Will's 2026-10-14 ruling, option 1): the Mac set is its
    own. --capture stages DIR/manifest_mac.json with the Mac's own metrics and
    memory rows (phys_footprint included) and REPORTS an x86 metric
    difference without failing; a verify with no mac section is NOT YET
    BLESSED (exit 1); a verify against a blessed section whose metrics differ
    fails."""
    gate = load_gate()

    def stub(zc, bc, out, pa=None, cfg=None):
        pngs, csv, mem, text = _stub_benchmark(gate, zc, out)
        mem["physFootprintBytes"] = 123456789
        mem["ledger"]["ownedPhysFootprintBytes"] = 123000000
        return pngs, csv, mem, text

    # BLESS-STATE-SAFE (mirrors check_pi step 4): since the 2026-10-19 bless
    # the real manifest carries platforms.mac, so every case below runs over
    # a manifest WITHOUT the section (the pre-bless state the check was
    # written for) -- the NOT YET BLESSED path, and the --goldens-dir cases,
    # which take the section from DIR/manifest_mac.json only when the
    # manifest has none.
    unsectioned = json.loads(json.dumps(gate.load_manifest()))
    unsectioned.get("platforms", {}).pop("mac", None)

    def run(argv):
        gate.BENCH_BIN = Path(tempfile.mkdtemp(prefix="b1044_bin_")) / "gles2_zone_benchmark"
        gate.BENCH_BIN.write_bytes(b"")
        gate.ARTIFACTS_DIR = Path(tempfile.mkdtemp(prefix="b1044_art_"))
        with mock.patch.object(gate, "load_manifest", return_value=unsectioned), \
             mock.patch.object(gate, "run_benchmark", stub), \
             mock.patch.object(gate, "run_memory_coverage_benchmark",
                               lambda *a, **kw: {"physFootprintBytes": 222}), \
             mock.patch.object(gate, "assert_memory_budgets", lambda *a, **kw: None), \
             mock.patch.object(gate, "assert_memory_ledger", lambda *a, **kw: None):
            buf = io.StringIO()
            old_argv = sys.argv
            sys.argv = ["visual_gate.py"] + argv
            try:
                with redirect_stdout(buf):
                    rc = gate.main()
            finally:
                sys.argv = old_argv
        return rc, buf.getvalue()

    fails = []
    rc, out = run(["--platform", "mac"])
    if rc != 1 or "NOT YET BLESSED" not in out:
        fails.append(f"mac verify without a section: rc={rc}, want 1 + NOT YET BLESSED")
    cap = Path(tempfile.mkdtemp(prefix="b1044_cap_"))
    rc, out = run(["--platform", "mac", "--capture", str(cap)])
    staged = cap / "manifest_mac.json"
    if rc != 0 or not staged.exists():
        fails.append(f"mac capture: rc={rc}, manifest_mac.json exists={staged.exists()}\n{out}")
    else:
        sec = json.loads(staged.read_text())["platforms"]["mac"]
        z0 = sec["zones"][0]
        if z0["memory"].get("physFootprintBytes") != 123456789 or \
                z0["memory"].get("textureCacheBytesLimit") != 200 or \
                z0["memory"].get("coveragePhysFootprintBytes") != 222:
            fails.append(f"mac section memory rows wrong: {z0['memory']}")
        if not sec["x86Divergence"] or "[x86-diff]" not in out:
            fails.append("mac capture did not report the x86 metric difference")
        rc2, out2 = run(["--platform", "mac", "--goldens-dir", str(cap)])
        if rc2 != 0:
            fails.append(f"mac verify against its own captured set: rc={rc2}\n{out2}")
        z0["metrics"][next(iter(z0["metrics"]))]["draws"] += 1
        staged.write_text(json.dumps({"platforms": {"mac": sec}}))
        rc3, out3 = run(["--platform", "mac", "--goldens-dir", str(cap)])
        if rc3 != 1 or "!= blessed mac" not in out3:
            fails.append(f"mac verify with a differing blessed metric: rc={rc3}")
    if gate.platform_config({"zones": []}, "mac").golden_root != gate.GOLDENS_DIR / "mac":
        fails.append("mac golden root is not tests/goldens/mac")
    for f in fails:
        print(f"FAIL: {f}")
    if fails:
        return 1
    print("ok: mac NOT YET BLESSED, --capture manifest_mac.json (own metrics, phys_footprint, "
          "x86 difference reported), blessed-section verify")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write-plan", metavar="PATH")
    ap.add_argument("--verify-x86", action="store_true")
    ap.add_argument("--check-pi", action="store_true")
    a = ap.parse_args()
    if a.write_plan:
        write_plan(a.write_plan)
        sys.exit(0)
    do_x86 = a.verify_x86 or not a.check_pi
    do_pi = a.check_pi or not a.verify_x86
    rc = 0
    if do_x86:
        rc = max(rc, verify_x86_plan())
    if do_pi:
        rc = max(rc, check_pi())
        rc = max(rc, check_mac())
    sys.exit(rc)


if __name__ == "__main__":
    main()
