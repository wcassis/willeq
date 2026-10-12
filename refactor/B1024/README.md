# B1024 (#344): per-platform Pi goldens — visual gate gains `--platform pi|x86`

MR: https://gitlab.goldfarm.local/willeq/willeq/-/merge_requests/148

Will's ruling on #299 (B980), 2026-10-11: **"per-platform Pi goldens, go with
option (a)"**. The BLESS note (#344, Will 2026-10-11): the first Pi golden
set this card captures is PRE-BLESSED — the orchestrator commits it on the
branch once the summary lists the per-frame sha256 and the Pi memory fields
and the Pi compare reports 15/15 exact. Later Pi golden moves need a fresh
bless. This card never commits anything under `tests/goldens/`.

## Design (from #344, implemented exactly)

`scripts/ci/visual_gate.py` gains `--platform x86|pi` (default `x86`) and ONE
resolver, `platform_config(manifest, platform, bench, goldens_dir)`, returning
the goldens root, the manifest section, the benchmark binary, the EQ path and
the extra benchmark args.

| | x86 (default) | pi |
|---|---|---|
| goldens | `tests/goldens/<zone>_f<n>.png` | `tests/goldens/pi/<zone>/f<n>.png` |
| section | `manifest["zones"]` | `manifest["platforms"]["pi"]` (own `blessedUnder`, per-zone `metrics`/`memory`) |
| binary | `BENCH_BIN` (`build-gles2/bin/gles2_zone_benchmark`) | `--bench PATH` (REQUIRED, the published `-nollvm` binary) |
| EQ path | `EQ_PATH` | `PI_EQ_PATH` (default `/mnt/projects/EverQuestP1999`, `scripts/pi/env.sh:15`) |
| extra args | none | `--drm` |
| maps proof | no | yes — `libgallium` under the shipped LLVM-less prefix, `libLLVM` never mapped |
| p95 CPU ceiling | asserted (x2.0 baseline) | NOT asserted (Pi TIMING is H04's lane, `scripts/pi/compare_pi.sh`) |

Same poses, frames, warmup, resolution, `productionConfig` args and
`--occluder-budget 16` pin — the Pi set is the x86 gate's invocation on the
Pi's GPU, nothing else. ONE compare rule: `compare_images_exact`, unchanged.
No `platforms.pi` section → `NOT YET BLESSED`, exit 1, never falls back to the
x86 set. `--capture DIR` stages the proposed bless (`DIR/<zone>/f<n>.png` +
`manifest_pi.json` + `SHA256SUMS` + logs + maps proofs), never writes under
`tests/goldens/`. `--goldens-dir DIR` verifies against a captured set (the
run2-vs-run1 determinism check). `--bless --platform pi` is refused: the
orchestrator's commit of a captured set is the bless.

The benchmark's native dump output is FLAT (`<prefix>_f<n>.png`, the `"_f"`
hard-coded in its filename, `tools/hwcaps/zone_bench_options.cpp:357` +
`gles2_zone_benchmark.cpp:1018`); the pi run moves the flat frames into the
nested committed layout in `run_benchmark()` (its `png_paths` return value is
the moved paths), which is why `--dump-png-prefix <out>/<zone>` + a move — and
not a nested prefix — is the layout's only mechanism.

### Field classification (what `verify_pi()` checks, which class, with its site)

| Field | Class | Site |
|---|---|---|
| pixels (every designated frame) | exact vs `cfg.golden_root/<zone>/f<n>.png` (`compare_images_exact`) | `verify_pi` loop 1 |
| `draws` / `stateChanges` / `polys` (per frame) | exact vs section `zones[].metrics` | `verify_pi` loop 2 (mirrors the x86 verify; a blessed frame with no CSV row FAILS) |
| memory: usage ≤ live budget | `assert_memory_budgets` (shared with the x86 verify) | `verify_pi` step 3 |
| memory ledger (unattributed + per-category budgets) | `assert_memory_ledger` (shared) | `verify_pi` step 3 |
| memory exact: `MEMORY_BUDGET_PAIRS_ALSO_BASELINED` usage + `MEMORY_BASELINE_FIELDS` | exact vs section `zones[].memory` (the x86 verify's loops (a')/(b)) | `verify_pi` step 3 |
| memory report-only (`processRssBytes`, `untrackedBytes`) | RECORDED in the section, never asserted (the x86 verify reports them too, T69; not cross-run deterministic — the Pi's own run2-vs-run1 verify measured ~1% drift in exactly these two) | `verify_pi` step 3 + `_pi_memory_fields()` |
| maps proof | `check_maps_text` (nollvm prefix green; `libLLVM` / system `libgallium` red) | `verify_pi` via `cfg.maps_required` |
| `blessedUnder.glVersion` | NOTE only (a mesa point release is a re-bless bookkeeping, not a regression) — same as the x86 verify | `verify_pi` |
| `p95CpuMsBaseline` | NOT asserted (Pi timing is H04's: `scripts/pi/compare_pi.sh` → `pi_bench_compare.py`) | `verify_pi` deliberately omits it |

## RED (step 2) — recorded, and re-derived for this README

Test half registered in `tests/cmake/hygiene.cmake` beside
`VisualGateLedgerAssertions` (label `hygiene`), run against the pre-implementation
gate (step 1's commit `42ebe684`, re-derived in a throwaway worktree):

```
$ python3 scripts/ci/test_visual_gate_platform.py --check-pi
FAIL: visual_gate.py has no platform_config (pre-B1024)
RED_EXIT=1
```

## x86 fixture (step 1, before ANY `visual_gate.py` edit)

`tests/data/b1024/x86_gate_plan.txt` — 20 lines, one per manifest zone
(benchmark command, `<OUT>`/`<REPO>`-normalized) + one per designated frame
(x86 golden path, repo-relative):

```
$ wc -l tests/data/b1024/x86_gate_plan.txt
20 tests/data/b1024/x86_gate_plan.txt
$ sha256sum tests/data/b1024/x86_gate_plan.txt
f139c8839ba9600f40f137149b1d093828855eea4e4a7af654f8cb7580bd983e
```

After every implementation step (3, 3b, 3c, 3d) `--verify-x86` re-derived the
plan and it stayed byte-identical — the CI `gate` job's invocation is
unchanged (it passes no `--platform`).

## GREEN

```
$ ctest --test-dir build -R 'VisualGatePlatform|VisualGateLedgerAssertions' --output-on-failure
100% tests passed, 0 tests failed out of 2
$ ctest --test-dir build -L hygiene
hygiene    = 158.65 sec*proc (29 tests)   # all passed
```

## The Pi run (step 4 — the only Pi step, under the Pi lock)

- Lock: `pi_lock_take t_a049cf2b` → `t_a049cf2b 2026-10-12T00:55:45-04:00 from hermes-ext`
  (step 4a); released after the runs (`pi_lock_show` empty, exit 1).
- Binary (published, IMMUTABLE dir):
  `/mnt/projects/fable/ci-arm/ce7ecd669ffa549d022803748fa6ebf4fafca539/bin/gles2_zone_benchmark-nollvm`,
  sha256 `a040baf32338e7f5540e5958f4bc54583461b2ebda91fe72054965a8c65347df`
  (on the Pi). BUILD_INFO at read time: `sha=ce7ecd669ffa549d022803748fa6ebf4fafca539`,
  `mesa_nollvm_pi_prefix=/home/user/mesa-nollvm-25.2.8`
  (`/mnt/projects/fable/ci-arm/by-branch/fable/BUILD_INFO` on the Pi).
  `git merge-base --is-ancestor ce7ecd66… HEAD` → OK (the run used the fable
  tip that is this branch's merge-base; this card changes no renderer code, so
  the fable tip's publish is the right build).
- The staged gate on the Pi was `git archive` of HEAD `scripts/ci` +
  `tests/goldens/manifest.json` — byte-identical (matching sha256) to the
  worktree's copy at each step.
- run1 (capture, all five zones):
  `python3 /tmp/kb-b1024/scripts/ci/visual_gate.py --platform pi --bench <bin> --capture /tmp/kb-b1024/run1`
  → exit 0, 5 zones, 15 frames staged.
- run2 (same into `/tmp/kb-b1024/run2`): `run1/SHA256SUMS == run2/SHA256SUMS`
  (`diff` clean → `SHA256SUMS_IDENTICAL`).
- run2-vs-run1 verify:
  `--platform pi --bench <bin> --goldens-dir /tmp/kb-b1024/run1` → exit 0,
  `=== VISUAL GATE (pi): PASS ===` (15/15 frames pixel-exact; metrics and the
  exact-match memory fields all matched; the maps proof green for all five
  zones). Re-verified against the NFS copy
  (`--goldens-dir /mnt/projects/fable/review/B1024/goldens`) → exit 0, PASS.
- Maps proof (every zone, `run1/<zone>_maps.txt`): `libgallium` resolved under
  `/home/user/mesa-nollvm-25.2.8/lib/` (e.g. `so:libgallium-25.2.8.so` in the
  run's own `[mappings]` census), `libLLVM` never mapped.
- Evidence on the Pi (NFS): `/mnt/projects/fable/review/B1024/goldens` (run1),
  `…/goldens-run2` (run2), `…/pi-logs` (the capture + verify logs).
  `sha256sum -c SHA256SUMS` on the NFS copy → 15/15 OK; `diff -r` tmp vs NFS
  run1 → identical. scp'd back to
  `/home/user/kb-evidence/b1024-pi-goldens-platform/pi_goldens/` (sha256sum -c
  → 15/15 OK there too). `/tmp/kb-b1024` removed from the Pi (tmpfs 242 MB).

### The 15 frames — sha256 (run1 == run2, per `SHA256SUMS`)

| zone | frame | sha256 (run1) | run1 == run2 |
|---|---|---|---|
| befallen | f0.png | 3056b5ff7c2c0ccae64fa82a59449dc5050ec4fc56bb8ef7f2331e082c61ad24 | yes |
| befallen | f1.png | 78f975d1ec604f93d0abb62b461c81020d49960d7fab8dfb8e25f15e6f846954 | yes |
| befallen | f2.png | 916c3162913642c70b6ed7ce9e2ac36914d2134eb21997c35af688f9e590e207 | yes |
| gfaydark | f0.png | 31bb41e2af65071186526b0c8147a184b84bb4a4b5ba1c7f48b60de6a3ae6b73 | yes |
| gfaydark | f1.png | 2f81e30efd80209aaaccf15cd94dd98d862dbc5e35c45903078386e64a767005 | yes |
| gfaydark | f2.png | e74cc482601d85b2468aaac82b50aa2c76c06b944db355171bc83b8d76a9627e | yes |
| iceclad | f0.png | 6cabe13c57deb0ba9ae598bca2dd44ac24730801d4c42a8b8c57deedeb8b9b26 | yes |
| iceclad | f1.png | d8672cbd23fc136d0e82d9d4423e4a05aed4a0cb61bd941d79a5b07677ee4077 | yes |
| iceclad | f2.png | 3134521c895095b640d2ca86e92796e84d88e849b71e899413a9d3ddc00a67fc | yes |
| qeynos2 | f0.png | f41701157e21946dc9c446adb259b75a26afbc088d19e754b8a6410e2bc1e1c0 | yes |
| qeynos2 | f1.png | bd9cb18d1fe8d46614c0bb5026632e1ed2e3238837b26c2ecd7b4928641e8d1e | yes |
| qeynos2 | f2.png | f42f26d40972caf5743c56181d3058e307438cd5a1b681bc80bfa537b75b3051 | yes |
| timorous | f0.png | 968bf00887ef40a108a5d2c1be826cced7af6aa96d115563059fb75814c18e98 | yes |
| timorous | f1.png | 448d5eed4c8624b79c3a74093f450aeaa3b75ef0cbac5158d66ef870e0db55ff | yes |
| timorous | f2.png | 4b44edfabdf7466887e405d406c092c3ce8e28cb6e9d85771a1138d7b3ae4339 | yes |

### Pi memory fields per zone (from `manifest_pi.json`, the exact-match set)

Budget-pair usage + `MEMORY_BASELINE_FIELDS` (bytes; `framebufferBytes` is
2880000 = 800x600x24bpp for every zone — the Pi's EGL depth=24, the same reason
a cross-platform pixel compare can never be exact):

| zone | textureCacheBytesUsed | gpuTextureBytes | zoneGeometryBytes | s3dZoneSourceBytes | vboEboBytes | characterModelsBytes | eqe1MappedBytes | totalEstimate | textureCount |
|---|---|---|---|---|---|---|---|---|---|
| qeynos2 | 21932691 | 16515072 | 1269654 | 501079 | 1269654 | 0 | 0 | 22435459 | 126 |
| gfaydark | 11184800 | 8454144 | 4991634 | 593321 | 4991634 | 0 | 0 | 21910733 | 32 |
| befallen | 2883581 | 2228224 | 1729050 | 963999 | 1729050 | 0 | 0 | 9530323 | 9 |
| iceclad | 3866619 | 2965504 | 6926622 | 9522300 | 6926622 | 0 | 0 | 29221048 | 15 |
| timorous | 3183945 | 2453504 | 12758982 | 4545175 | 12758982 | 0 | 0 | 35396643 | 35 |

`placeableObjectsBytes` = 0 and `eqe1ArchiveCount` = 0 for every zone (the
golden-image invocation never passes `--eqe1-path/--objects` — the same zero
the x86 set carries, by the T80 design). `blessedUnder`: `glVersion` =
`OpenGL ES 2.0 Mesa 25.2.8` (from the run's own `[GLES2 Driver: …]` line,
`extract_gl_version`), `glRenderer` = `OpenGL ES 2.0 Mesa 25.2.8`.

## Not verified / left open

- **Acceptance 3 — the orchestrator's commit of the pre-blessed set + its
  15/15 re-run on the Pi** — pending. The orchestrator commits
  `tests/goldens/pi/<zone>/f<n>.png` + `manifest["platforms"]["pi"]` from
  `/mnt/projects/fable/review/B1024/goldens/` (+ `manifest_pi.json`). After
  that commit, on the Pi:
  `python3 scripts/ci/visual_gate.py --platform pi --bench
  /mnt/projects/fable/ci-arm/ce7ecd669ffa549d022803748fa6ebf4fafca539/bin/gles2_zone_benchmark-nollvm`
  (no `--goldens-dir`) is expected to report `15/15 exact` — the same
  invocation already reports `=== VISUAL GATE (pi): PASS ===` against the
  NFS-staged set (`--goldens-dir …/review/B1024/goldens`, exit 0).
- The Pi set's first verify against the COMMITTED (in-repo) goldens has not
  run — nothing is committed under `tests/goldens/` by this card (that is the
  orchestrator's, per the bless note).
- A re-publish of the fable ARM build has appeared on the Pi since the run
  (BUILD_INFO `sha=a1d14b1f…`); this run deliberately used the immutable
  `ce7ecd66…` dir (the branch's merge-base, `git merge-base --is-ancestor`
  OK). A Pi re-bless against a newer binary is Will's call (a driver/mesa
  point move is a re-bless bookkeeping, not a regression — `verify_pi`'s
  glVersion NOTE rule).
