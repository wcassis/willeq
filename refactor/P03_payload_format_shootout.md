# P03 — Flatpack payload-format shootout (ETC1 vs RGBA8888 vs RGB565/4444)

Will's directive: evaluate retiring ETC1 encoding while keeping the
flatpack container + offline pre-baking (his file-open cost point: ~200ms
base per open on the Pi — consolidation and pre-decode stay regardless).
This unit produces the DATA for that decision. No retirement happens here —
deliverable is a four-way measurement table + a drafted decision-doc entry.

Goal frame (Will's words): performant client, >30fps, low RAM. P02 REWROTE
the context — weigh all of this in the decision draft:
- The ETC1 path LEAKS CMA kernel-side (-14 to -81MB/run, reboot-only
  recovery) — the confirmed P01 panic mechanism. Root cause (our upload
  code vs lima kernel) still unknown. A leaking path is currently
  disqualifying for a long-running client regardless of speed.
- BUT EQE1 steady-state is FASTER than RGBA (30/18/29 vs 33/20/34 ms) —
  the compression bandwidth win is real. Retirement trades that away;
  the decision draft must say so honestly.
- EQE1 also shows episodic stall bursts uncorrelated with CMA.
- CMA-leak check is therefore a FIRST-CLASS measurement axis for the new
  formats: run the P02 CmaMonitor telemetry per config and report
  per-invocation deltas — 565/4444 should be leak-free like RGBA8888
  (same glTexImage2D family), but VERIFY, don't assume.
- Lighting context for the decision draft: lightweight lighting alone takes
  qeynos2 over 30fps (P02/6b); texture format is the supporting act.

## Part 1 — v2 pack format + builder + loader

1. **Container v2**: extend the EQE1 format (include/common or its format
   header + docs) with a version bump and a per-entry payloadFormat field:
   `ETC1` (v1 semantics), `RGB565` (opaque), `RGBA4444` (alpha textures —
   4-bit alpha is plenty for binary masks), `RGBA8888`. v1 packs remain
   readable (loader dispatches on version).
2. **Builder** (`eqe1_builder`): `--payload-format etc1|rgba8888|rgb565`
   where rgb565 mode emits 565 for opaque textures and 4444 for
   alpha-carrying ones (the builder already decodes and knows alpha
   presence). Mip chains included as v1 does.
3. **Loader/upload**: v2 payload upload via glTexImage2D with the matching
   GL format/type (565: GL_RGB/UNSIGNED_SHORT_5_6_5; 4444:
   GL_RGBA/UNSIGNED_SHORT_4_4_4_4; 8888 as today's RGBA). NO alpha
   companion for v2 entries; `isETC1Texture()` false → H02d's main-alpha
   shader variant applies. ETC1/v1 path byte-for-byte untouched.
4. Build packs (zone + _obj + _chr as the benchmark requires) for the three
   golden zones in all three new/old variants.
5. VM sanity: benchmark under llvmpipe with each v2 variant — rendering
   intact (foliage cutouts present, no magenta/garbage); pixel-exact match
   to goldens is NOT expected for 565/4444 (quantization) — document the
   visual delta qualitatively (and capture comparison PNGs for Will).

## Part 2 — Pi shootout (after P02's protocol: interleaved ordering,
CmaFree telemetry, session caps, scripted reboots, /tmp I/O rule)

Four configurations × three zones: raw S3D decode (no --eqe1-path),
flatpack-ETC1 (v1), flatpack-RGBA8888, flatpack-565/4444. Measure:
- **Cold zone-load time** (the file-open/pre-decode claim): drop caches
  (`echo 3 > /proc/sys/vm/drop_caches` via sudo) before each load-time
  trial; time the load phase (add a load-phase timing line to the benchmark
  output if absent — flag-gated); ALSO record warm-cache load. Note pack
  byte sizes per variant (bytes-over-NFS matters: RGBA is ~6-8x ETC1; 565
  halves RGBA).
- **Steady-state p95 + run-to-run variance** (the stability finding is the
  differentiator — interleave all four configs; 2+ samples each across
  sessions).
- **Memory**: texture cache bytes per config + CmaFree during run.

## Deliverables

- The 4×3 results table (load cold/warm, p95, variance, bytes, CmaFree).
- Comparison PNGs (llvmpipe) of 8888 vs 565/4444 for one texture-rich pose.
- A drafted decision entry (do NOT commit to docs/decisions.md — put the
  draft in this file's review for Will's sign-off): recommendation with
  numbers on whether ETC1 retires, and if so the staged migration
  (feature-flag off → archive tools/companion machinery → remove companion
  shader variant).
- Updated deferred.md entries left to the orchestrator.

## Verification

- v1/ETC1 behavior unchanged (Pi ETC1 runs still work; ctest + visual gate
  green on the VM; full default-build bar since loader/builder are
  production code).
- All Pi work under the P02 protocol; Pi pristine after sessions.

## Review

**STATUS: complete. All 4 configs measured on the Pi across 38 telemetered
invocations (24 shootout + 14 follow-up), zero panics, zero protocol aborts,
Pi left pristine. The headline REWRITES the P02 premise this unit was framed
around: under a controlled post-exit measurement, NO texture path — ETC1
included — leaks CMA kernel-side. P02's "reboot-only" CMA drain is page
cache borrowing movable CMA pages during large NFS reads, and it is fully
recovered by `drop_caches`, no reboot required. The decision draft below is
written against that corrected premise.**

### Part 1 — v2 format/builder/loader (committed)

- **Format** (`include/client/graphics/eqe1_format.h`): `EQE1_VERSION_V2 = 2`
  and `EQE1PayloadFormat {ETC1=0, RGB565=1, RGBA4444=2, RGBA8888=3}` carried
  in the index entry's former `padding` byte — v1 files (padding always 0)
  decode as ETC1 unmodified, so there is no migration step and readers
  dispatch per-entry on `payloadFormat`, not on the file version. Added
  `payloadBytesPerPixel/payloadDataSize/payloadMipChainSize` helpers.
- **Builder** (`tools/eqe1_builder.cpp`): `--payload-format etc1|rgba8888|rgb565`.
  Default `etc1` is **byte-for-byte identical to the old output** — verified by
  regenerating all three golden-zone packs and `cmp`-ing against the committed
  `/mnt/projects/EverQuest_ETC1` files: qeynos2/gfaydark/befallen all IDENTICAL.
  `rgb565` mode emits RGB565 for opaque and RGBA4444 for alpha-carrying
  textures (4-bit alpha; the builder already knows alpha presence); v2 modes
  never emit an `_alpha` companion (alpha is in-band) and share the ETC1
  path's `downsample2x` box filter so mip dimensions are identical across
  formats. Both S3D-archive and BMP-directory modes route through one shared
  `encodeTexture()` so they cannot drift.
- **Loader** (`eqe1_loader.cpp/.h`): accepts version 1 or 2, surfaces
  `payloadFormat` per entry. v2 entries with `hasAlpha=1` link no companion.
- **Upload** (`constrained_texture_cache.cpp`): `uploadCompressedV2()` —
  plain `glTexImage2D` per mip with `GL_RGB/UNSIGNED_SHORT_5_6_5`,
  `GL_RGBA/UNSIGNED_SHORT_4_4_4_4`, or `GL_RGBA/UNSIGNED_BYTE`; LINEAR /
  LINEAR_MIPMAP_LINEAR filtering (main-unit policy); budget-checked with the
  payload's true byte size. `CachedTexture::isETC1` stays **false**, so H02d's
  `isETC1Texture()` routing sends v2 alpha-test materials to the main-alpha
  RGBA shader variant — no companion, no unit 1. `uploadTexture()` dispatches
  on `payloadFormat==ETC1` → the v1 function, else v2.
- **v1/ETC1 path untouched**: `uploadCompressedETC1()`'s body is unchanged;
  the only diff in it is the guard macro spelling (see next bullet), which
  preprocesses identically on the ARM build (both macros defined there). The
  dispatch call site sends `payloadFormat==0` (every v1 entry) down exactly
  the old path.
- **One guard widening (flagging explicitly for review)**: the EQE1 upload
  block (GL includes + both upload functions) was gated `#ifdef EQT_HAS_DRM`,
  which is only defined when `EQT_DRM=ON` (ARM). The x86 GLES2 build
  (`EQT_DRM=OFF, EQT_GLES2=ON`) therefore had the whole EQE1 upload path
  compiled out — **ETC1 packs never actually uploaded on llvmpipe before this
  unit** (silent "non-GLES2 build" WARN, blank textures). The plan's VM-sanity
  step requires the path on the VM, so the gate is now
  `defined(EQT_HAS_DRM) || defined(EQT_HAS_GLES2)`
  (`EQT_EQE1_UPLOAD_AVAILABLE`). ARM preprocessed output is bit-identical
  (both macros defined); the default desktop build defines neither (still
  compiled out); `uploadTexture()`'s DRM-only filter-override block for the
  raw path stays under `EQT_HAS_DRM` alone, so visual-gate goldens are
  unaffected (gate re-run PASS ×2, byte-identical artifacts).
- **verify_eqe1**: validates v2 sizes per `payloadFormat`, expects no
  companion for v2 alpha entries, prints the format per entry. 9/9 v2 packs
  in each new directory pass; existing v1 packs unaffected.
- **Tests**: `test_eqe1_loader` gained a synthetic v2 round-trip case
  (565 + 4444-with-in-band-alpha + legacy etc1 entry in one v2 file) and a
  v1-decodes-as-ETC1 regression case. 11/11 pass.

Pack builds: zone+_obj+_chr for all three golden zones in both v2 variants →
`/mnt/projects/EverQuest_RGBA8888`, `/mnt/projects/EverQuest_RGB565`
(9 files each). v2 builds run in seconds per archive (no Etc2Comp); the ETC1
regen for the byte-identity check took ~2-8 min per archive.

### VM sanity + quality (llvmpipe, gfaydark foliage pose 820.0,84.9,110.0,0,-5)

PNGs: `refactor/artifacts/P03/gfaydark_{etc1,rgba8888,rgb565}_f0.png`
(gitignored, on disk for Will). All three variants render intact — foliage
cutouts present, no magenta/garbage, draws/polys identical. Quantitative
(decoded-pixel compare):

| pair | MAD | PSNR | note |
|---|---|---|---|
| golden(raw-S3D) vs rgba8888 pack | 0.000 | inf | **pixel-identical — the v2 container round-trip is lossless** |
| rgba8888 vs rgb565/4444 | 0.53 | 46.4 dB | negligible; slight banding on smooth gradients if you hunt for it |
| rgba8888 vs etc1 | 0.92 | 29.5 dB | the real quality cost lives in ETC1 block compression, not in 16-bit quantization |

### Part 2 — Pi shootout results

Protocol: P02 machinery reused wholesale (`run_session`, CmaFree telemetry +
abort thresholds, session cap 8 with scripted reboots + binary re-staging,
randomized/interleaved (zone,config) order, /tmp I/O rule, governor pin).
Cold trials dropped Pi page caches via sudo immediately before the run; warm
trials ran back-to-back after. 60 frames/invocation, manifest poses.
`--report-load-time` (new, flag-gated, default-off) prints `LOAD_PHASE_MS`.
Raw data: `benchmarks/pi_runs/p03_investigation/p03_shootout{1,2}/` (gitignored).

**Steady-state p95 ms (cold-rep / warm-rep)** — etc1 rows are the *fair*
9-file-directory reruns (see confound note below); ± extra reps from the
leak block where run:

| zone | raw S3D | etc1 (v1) | rgba8888 | rgb565/4444 |
|---|---|---|---|---|
| qeynos2 | 34.76 / 34.80 | **30.50 / 30.53** | 34.80 / 50.19* | 32.14 / 32.14 |
| gfaydark | 41.12 / 41.14 (41.12, 41.09) | **38.88 / 39.16** (38.95, 39.00) | 41.14 / 41.06 (41.09, 41.09) | 39.98 / 40.02 (40.00, 40.04) |
| befallen | 44.73 / 44.56 | **42.89 / 42.94** | 44.44 / 44.33 | 43.41 / 43.41 |

Speed order is unambiguous and consistent: **etc1 fastest** (−3.5% to −12.4%
frame time vs rgba8888), **rgb565 second** (−2.3% to −7.6%), rgba8888 == raw
(same GPU format). Variance: rgb565 and raw are metronomic (≤0.04 ms p95
spread across all reps); etc1 in the fair configuration is EQUALLY stable
(8/8 invocations, spread ≤0.28 ms, zero stall bursts) — see the instability
revision below. *qeynos2/rgba8888 warm hit genuine 50-64 ms stall bursts —
its 95 MB pack set heap-loads whole on the 512 MB board (see below).

**Cold / warm zone-load ms** (LOAD_PHASE_MS; cold = after Pi cache drop):

| zone | raw S3D | etc1 (v1, fair) | rgba8888 | rgb565/4444 |
|---|---|---|---|---|
| qeynos2 | 10029 / 2692 | 9371 / 2027 | 17625 / 10619* | 13992 / 2453 |
| gfaydark | 10260 / 2110 | 11285 / 1981 | 16160 / 2840 | 11968 / 2109 |
| befallen | 10271 / 2391 | 11099 / 2379 | 12083 / 2596 | 10882 / 2423 |

Cold load is NFS-transfer-bound (≈12 MB/s on the Pi's 100 Mbit link): the
ordering tracks pack bytes, so etc1 ≈ raw ≈ rgb565 < rgba8888. Pre-decoding
does NOT buy cold-load time at these texture sizes — palettized-BMP decode
is cheap and the flatpacks are bigger than the S3Ds, so transfer eats the
decode saving. Warm load: everything ~2.0-2.7 s except the two anomalies
marked * (memory pressure, below).

**Pack bytes (zone+_obj+_chr) and GPU texture-cache bytes:**

| zone | s3d | etc1 pack | rgba8888 pack | rgb565 pack | | raw GPU | etc1 GPU | 8888 GPU | 565 GPU |
|---|---|---|---|---|---|---|---|---|---|
| qeynos2 | 9.2M | 16.8M | 95.0M | 49.3M | | 42.2M | **5.3M** | 42.2M | 21.1M |
| gfaydark | 8.1M | 15.4M | 68.7M | 37.5M | | 11.2M | **1.5M** | 11.2M | 5.6M |
| befallen | 5.0M | 9.9M | 35.2M | 20.1M | | 2.9M | **0.4M** | 2.9M | 1.4M |

ETC1's 8× GPU-memory advantage is intact and large: qeynos2 rgba8888 uses
66% of the 64 MB constrained texture budget; etc1 uses 8%.

**Determinism control:** draws/state-changes/polys are identical across all
four configs on every pose of every zone (cross-checked in both runs) — the
comparison is pure texture-path, no scene drift.

### CMA verdict — the leak premise does not survive controlled measurement

Two-layer result:

1. **True-leak check** (gfaydark — P02's worst "leaker" — ×2 reps per
   config; protocol: drop_caches → sample → run → process exit →
   drop_caches → sample; only unrecovered kB count):
   raw **+4/+4 kB**, etc1 **+60/+12 kB**, rgb565 **+16/+12 kB**, rgba8888
   **−144 kB** (its other rep read +4744 kB, but that was the
   first-DRM-run-after-reboot sample — P02 already documented a one-time
   ~3-9 MB scanout allocation there; the second rep confirms nothing
   persists). **No texture path leaks CMA kernel-side.**
2. **What P02 actually saw**: per-invocation CmaFree telemetry in this
   unit's shootout shows big transient drains on EVERY config that reads a
   lot of bytes — etc1 −29.1 MB worst, **rgba8888 −31.6 MB worst** (the
   supposedly leak-free control!), rgb565 −15.2 MB, raw ≈ neutral — i.e.
   the drain is proportional to bytes read through the page cache, which
   borrows movable CMA pages, and is unrelated to `glCompressedTexImage2D`.
   Direct refutation of "reboot-only recovery" in the phase-1 telemetry:
   CmaFree sat at 52,060 kB after prior runs, and a plain `drop_caches`
   restored it to ~102 MB (befallen/etc1/r0cold pre→post sequence). P02
   never dropped caches before concluding only reboot recovers.

**EQE1 instability revised too:** P02's episodic EQE1 stall bursts
reproduced here ONLY in configs that heap-load large pack sets — the v1
loader reads every `.eqe1` fully into heap, and the ETC1 directory carries
139 MB of global archives that the benchmark opens (config list) but never
samples. Rerun against a 9-file zone-only directory, etc1 produced **zero
stall bursts in 8/8 invocations** (previously 3/3 warm runs stalled with
the full directory). And rgba8888 — no companion, no compressed upload —
stalled on qeynos2, whose 95 MB pack set is the largest heap load in the
matrix. The instability is **memory pressure from whole-file heap loading
on a 512 MB board**, not the ETC1 texture path. This also retroactively
explains P02's "worst offender gfaydark" (global set + zone set churn) and
why its small/no-drain EQE1 runs existed (page-cache state dependent).

The panic-safe protocol stays right anyway: CMA exhaustion (whatever fills
it) was the P01 panic mechanism, and the telemetry/abort/caps are cheap.

### Decision draft (NOT committed to docs/decisions.md — for Will's sign-off)

> **Decision: keep ETC1 as the GLES2/Mali texture payload; adopt the v2
> flatpack format for its per-entry payloadFormat field; fix the loader's
> whole-file heap loading. Do not retire ETC1 encoding.**
>
> Basis (P03 shootout, 4 configs × 3 zones, Pi, panic-safe protocol):
> - The disqualifying finding against ETC1 — the P02 kernel-side CMA leak —
>   does not reproduce under a controlled post-exit measurement: every
>   config, ETC1 included, returns CMA to within 60 kB once page caches are
>   dropped; the P02 drain was page-cache CMA borrowing proportional to
>   bytes read (RGBA8888 drained more than ETC1 in this unit: −31.6 vs
>   −29.1 MB worst-case), and `drop_caches` — not reboot — recovers it.
> - The P02 stall-burst instability is also not the ETC1 path: it tracks
>   total heap-loaded pack bytes (v1 loader reads whole files; the ETC1 dir
>   heap-loads 139 MB of never-sampled global archives per benchmark run;
>   RGBA8888 qeynos2 = 95 MB stalls identically). With a zone-only pack
>   dir, ETC1 ran 8/8 invocations with zero bursts and ≤0.3 ms p95 spread.
> - On the merits ETC1 wins three of four axes: fastest steady state
>   (30.5/38.9/42.9 ms p95 vs RGBA8888's 34.8/41.1/44.4 — the bandwidth
>   win is real and qeynos2 crosses 32 fps), 8× smaller GPU footprint
>   (5.3 vs 42.2 MB on qeynos2 — matters at the 64 MB budget and directly
>   serves the low-RAM KPI), smallest flatpack bytes over NFS (16.8 vs
>   49.3/95.0 MB). Its one loss is quality (29.5 dB PSNR vs lossless;
>   565/4444 is 46.4 dB).
> - RGB565/4444 is the designated fallback, now fully implemented and
>   measured: if the ETC1 toolchain (Etc2Comp build times, companion-alpha
>   shader machinery) ever needs to go, 565/4444 costs only +2.3-7.6%
>   frame time vs ETC1, halves RGBA's bytes everywhere, needs no companion
>   (H02d main-alpha variant), and is visually transparent. RGBA8888 is
>   not a contender (worst frame time, 6× ETC1 pack bytes, 8× GPU bytes,
>   stalls on large zones; its one virtue — bit-exactness — is a debug
>   property, and v2 packs give it to us as a diagnostic config for free).
> - Follow-up work this decision creates:
>   1. Loader: stop heap-loading entire packs (open-on-demand or mmap;
>      at minimum stop opening the 33 global archives the benchmark never
>      samples). This kills the real instability for every format.
>   2. Keep CmaFree telemetry in long-session monitoring; add a
>      `drop_caches` (or CMA-pressure check) to zone transitions on the
>      constrained preset as cheap insurance against P01-style exhaustion.
>   3. The staged ETC1-retirement path (feature-flag off → archive
>      eqe1_builder's Etc2Comp dependency + companion machinery → remove
>      companion shader variant) remains documented and viable via
>      `--payload-format rgb565` should (1) fail to hold up in production
>      sessions — but the measurements here do not justify starting it.

### Deviations / honesty notes

- **The plan's Pi matrix said "flatpack-ETC1 (v1)" — the first pass ran it
  against the full production ETC1 directory** (4.7 GB, incl. 139 MB of
  global archives the benchmark opens). That made etc1's load times and
  stall behavior incomparable to the v2 dirs (9 files each). Detected via
  the load-time asymmetry, fixed by rerunning etc1 against a zone-only
  symlink dir (`/mnt/projects/EverQuest_ETC1_zones`); both datasets are
  reported (the confounded one is itself the instability evidence). The
  production ETC1 directory was never modified.
- **The guard widening** (`EQT_HAS_DRM` → `EQT_HAS_DRM || EQT_HAS_GLES2`
  for the EQE1 upload block) is a behavior change for the x86-GLES2 dev
  build only: eqe1 uploads now work there (they silently no-opped before).
  Required by the plan's VM-sanity step; ARM preprocessed output identical;
  visual gate unaffected (PASS ×2 byte-identical, goldens untouched).
  Called out here rather than buried because "v1 byte-for-byte untouched"
  deserved the explicit caveat.
- The Pi shootout ran cold/warm as paired back-to-back invocations of the
  same (zone,config) with the PAIR order randomized (cold must precede
  warm); the plan's "2+ samples across sessions" is met via the phase-1 +
  phase-2 + leak-block reps for etc1/rgb565/rgba8888/raw on gfaydark
  (4-6 samples each) and 2-4 samples elsewhere, spread across 5 sessions
  and 4 scripted reboots.
- The two warm-run anomalies (qeynos2 etc1-full-dir 48.6 ms, qeynos2
  rgba8888 50.2 ms p95 with 10.6 s warm loads) are reported, not excluded
  from the tables — they are the memory-pressure finding, marked *.
- `LOAD_PHASE_MS` measures the benchmark's whole loadZone() (file open +
  decode + upload + mesh build), not file-open time alone; Will's ~200 ms
  per-open point wasn't separately instrumented. Cold-load conclusions are
  therefore about the whole pipeline, which is what the KPI cares about.
- The NAS's own RAM cache is not controlled by the Pi-side `drop_caches`;
  cold numbers include whatever server-side caching NFS did (visible as
  gfaydark raw cold varying 7.2-10.3 s across sessions). Consistent across
  configs within a session, so the cross-format ordering stands.
- Comparison PNGs are llvmpipe (per plan), not Lima captures; Pi-side
  correctness is evidenced by identical draw/poly counts and the texture
  cache byte accounting per format.
- Full ctest on the final tree: 1224/1225, sole failure the pre-existing
  documented ALSA flake (`AudioManagerTest.PreloadCommonSounds`);
  `RDPServerFrameTest` passed on this run. Visual gate PASS inside ctest
  and twice standalone (byte-identical). ARM cross-build clean from this
  worktree (main untouched). `check_file_sizes.sh` exit 0 — the two WARN
  files were over the 1500-line threshold before this unit.
- Ad hoc orchestration (`p03_run.py`, `p03_run2.py`) stayed in the session
  scratchpad per P02/H02d precedent; only production/format/tool changes
  are committed. `benchmarks/pi_runs/p03_investigation/` (raw CSVs, logs,
  telemetry, results.json) is on disk, gitignored.
- Updated deferred.md entries left to the orchestrator per the plan.
- No kernel panic at any point; kmsg follower shows only normal boots for
  the unit's 5 reboots (4 scripted + 1 final pristine-state reboot).
  Pi final state: fresh boot, CmaFree 102,056 kB, /tmp empty, governor
  ondemand ×4, NFS mounted.
