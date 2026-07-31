# P48 — Composite-sheet alpha support: recover the P37 perf cost, keep the blessed masked look

**Slot: slot1. OWNS THE PI (P02 protocol; ≤6 invocations, for the
final re-measure only). Authorized by Will 2026-07-31 (option 1 of
the P37 regress-response).**

## Goal

P37 excluded masked races from composite eligibility because sheets
carry zero alpha (builder hardcodes hasAlpha=0). P45 measured that
cost: +18.3ms p95 / +207% draws at a ZOM-heavy real roster. This unit
gives sheets companion alpha so masked races composite again — the
perf comes back AND the P47-blessed masked cutouts render on
composited entities.

## Background (read first)

refactor/P37_*.md (exclusion + per-variant sheets), P45 Review (the
cost tables + real-roster invocation), P46/P46d Reviews (palette-0
decode + cache keying), P47 Review (the magenta viewer + regression
pin — your verification instrument), P16 Review (sheet
regeneration/deploy precedent incl. backup convention), P08f
(builder zone-archive support), deferred.md's P37/P46d entries.

## Work

1. **Builder alpha**: entity_composite_builder + eqe1 pipeline emit a
   companion alpha page for sheets containing WLD-masked source
   textures (mirror the existing zone ETC1 companion convention:
   R=G=B=alpha, palette-0 → 0). Unmasked sheets byte-identical to
   current output (verify by hash on a sample).
2. **Eligibility**: replace P37's blanket masked-race exclusion with
   a sheet-capability gate — masked races composite when their sheet
   has the alpha companion; the exclusion stays for sheets without it
   (safety for stale/global sheets not yet regenerated).
3. **Rendering**: composite meshes whose sheet is masked get
   EMT_TRANSPARENT_ALPHA_CHANNEL_REF + companion binding through the
   SAME production promotion path entity_renderer already uses (P46b
   established it is paired — do not fork a new path).
4. **Sheet regeneration + deploy**: regenerate affected sheets (the
   ~15 masked races' variants; global + zone archives per P08f).
   Deploy to /mnt/projects/EverQuest_ETC1 (NFS) WITH a dated backup
   dir first (P16 convention). Audit tool must pass on the new
   sheets.
5. **Visual verification (the P47 instrument — no crop-squinting)**:
   extend/reuse the magenta viewer to render composited masked races
   (ETC1 leg, composites active): cutouts must show as enclosed
   magenta, textures correct at full size. Add the composite leg to
   the P47 regression test (masked composited >0, control ==0).
   Produce a small contact sheet for Will — this is NEW blessed
   territory (masked composites were never seen before); final bless
   is his.
6. **Pi re-measure (the payoff)**: repeat P45's real-roster A/B
   invocation shape on the canonical befallen e205 replay: current
   tip (masked races composited w/ alpha sheets) vs P45's recorded
   tip numbers (57.650ms p95 / draws 348.3). Target: recovery toward
   the pre-P37 39.3ms / 113.6-draw level. Also 1 canonical-replay
   spot (KPI guard, vs 32.24-32.5ms band). DRM mode-line confirmation
   ("Found matching mode 800x600") recorded per run (P43 rule).

## Rules

- Deploy only with backup; NFS deploy is part of this unit (P16
  precedent) but NO preset changes.
- Goldens untouched (gate PASS); production promotion path reused,
  not duplicated.
- Full bar: GLES2 ctest baseline (~1617/1619 + extended regression),
  visual gate PASS, ARM cross-build clean, file sizes.
- Pi protocol absolute (CmaFree gate, /tmp-only timed writes,
  drop_caches never reboot, pristine exit).
- STOP-with-memo: sheet format change breaks existing consumers; or
  eligibility/rendering plumbing cascades absurdly; or the Pi
  re-measure REGRESSES the canonical KPI spot.
- Sibling units run in slot2 (DRM device file) and slot3
  (mesh_building/animated_mesh_creation mask plumbing) — do NOT touch
  docker/irrlicht-drm/CIrrDeviceFB.cpp, and coordinate-free overlap
  with slot3 is expected to be nil; if you find you must edit
  mesh_building.cpp or animated_mesh_creation.cpp, note it
  prominently in your report for merge-order attention.

## Review

**Delivered: builder emits real ETC1 alpha companion pages for masked composite sheets; eligibility is sheet-capability-gated (P37's blanket exclusion replaced); rendering reuses entity_renderer.cpp's existing production promotion path unchanged; 27 masked sheets regenerated and deployed with a dated backup; audit 86/86 clean; composite-leg viewer extension + regression test confirm real alpha-tested composite rendering for SKE/ZOM/BAT/ORC (all logged "composite ADMITTED"); Pi real-roster recovery is 97.4% of the way back to the pre-P37 level. IMPORTANT CAVEAT: the plan's canonical-replay KPI guard came back out of band and over the 33.3ms line — investigated and PROVEN (via a 4th Pi invocation, isolated pre-P48 binary) to be a pre-existing regression on `fable` tip unrelated to this unit's own diff. Flagged prominently below and in benchmarks/audits/p48_sheet_alpha/EVIDENCE_MANIFEST.txt for orchestrator/Will escalation — not silently absorbed.**

### Builder / eligibility / rendering design

- `tools/entity_composite_builder.cpp`: `BodyPartTexture`/`PlacedTexture` gained an `isMasked` field from `part->textureMasked()` (same parallel-array signal P37 wired, mirroring the existing `isAnimated` detection). `decodeBMP()`/`decodeTextureFromArchive()` gained an `applyPaletteMask` parameter (default false, byte-identical for every existing call site) that punches palette-index-0 to alpha=0 for masked placements only — mirrors `tools/eqe1_builder.cpp`'s existing zone-texture convention rather than reinventing it. `writeEQE1()` gained a `hasAlpha` parameter (default false, byte-identical): when set, the main EQE1 entry becomes `EQE1_TYPE_ETC1_RGBA`/`hasAlpha=1` and a second `{baseName}_alpha` grayscale-RGB companion entry follows it — the exact layout `ConstrainedTextureCache`'s existing ETC1 upload path already knows how to find and bind on unit 1, unchanged. The sheet JSON gained a `"hasAlpha"` key (`CompositeTextureData::hasAlpha`, defaults false when absent — safe for pre-P48 sheets).
- `race_model_loader.cpp`'s `preloadModelData()`: the eligibility gate is now `compositeHasMaskedTexture && !compositeData->hasAlpha` (was: blanket `compositeHasMaskedTexture`). New log lines `composite EXCLUDED ... (P37/P48 gate)` / `composite ADMITTED ... (P48)` make the gate's decision directly observable.
- `mesh_building.cpp`'s `buildCompositeMeshFromGeometry()`: `MaterialType` is now `compositeData.hasAlpha ? EMT_TRANSPARENT_ALPHA_CHANNEL_REF : EMT_SOLID` (both buffer-creation sites, including the >65535-vertex overflow branch) instead of unconditional `EMT_SOLID`. **No entity_renderer.cpp change was needed**: its existing `createEntity()` promotion loop already iterates every buffer material and promotes `EMT_TRANSPARENT_ALPHA_CHANNEL_REF` generically, regardless of whether the buffer came from the composite or multi-buffer path — confirmed by reading the call site, not assumed (P46b's "never fork the promotion path" instruction is satisfied by construction, not by matching it manually).

### Sibling-territory deviation (flagged per the plan's instruction)

`mesh_building.cpp` (slot3's named territory this dispatch) HAD to be touched: `buildCompositeMeshFromGeometry()` is the only place a composite buffer's `MaterialType` is assigned, so the hasAlpha-gated promotion has nowhere else to live. The edit is minimal — 2 small hunks inside that one function, ~10 functional lines, comment-heavy — and does not touch `buildMeshFromGeometry()` (the non-composite path) or `animated_mesh_creation.cpp` at all. **Flagged for merge-order attention with slot3's P50 (mask plumbing).**

### Sheets regenerated + deploy

Invocation (matches P08f/P16's own shape exactly): `--zone befallen --zone qeynos2 --zone gfaydark --sheet-size 2048 --eqe1` + `--race SKE --all-variants --eqe1`. 86 sheets built (81 zone-native + 5 global SKE); 27 got a real alpha companion (BAT×2, BEA×3, BGM×1, BRF×1, CAZ×1, DRI×1, FAF×1, FEM×2, FIS×4, ORC×3, SKE×5, TRE×1, ZOM×2). Byte-identity verified for the 59 unmasked sheets in this batch: 56 are md5-identical to live (BMP+EQE1; JSON differs only by the added `hasAlpha:false` key, diffed with that key stripped → zero remaining diff); the other 3 (min_hs_def_0_0, gnn_hs_def_0_1/0_3) differ from live for reasons proven pre-existing (isolation rebuild with P48's diff `git stash`-ed out reproduces the identical byte-level diff against live).

- Backup: `/mnt/projects/EverQuest_ETC1_backup_20260731_p48` (full directory, 4.8G, 2504 files, taken before any live file was touched).
- Deploy: only the 27 genuinely-changed sheets' 3 files each (81 files) copied from `/mnt/projects/EverQuest_ETC1_staging_p48` into `/mnt/projects/EverQuest_ETC1` — the byte-identical/pre-existing-drift files were left untouched (P16's "genuinely new/changed only" convention). Per-file md5 verified post-deploy.
- Rollback: re-copy the same 27 basenames from the backup dir.
- Audit: `refactor/scratch/p08f_sheet_audit.py` — 86/86 regenerated sheets clean, 0 broken (`benchmarks/audits/p48_sheet_alpha/p48_structural_audit.json`).
- Known gap (reported, not fixed): DEN's befallen default-variant composite sheet resolved to `denlg0001.bmp` (unmasked), not `denlg0002.bmp` (the texture P37's ground-truth table named) — a different texture for the same slot; DEN stays on the P37 fallback (still alpha-correct via P46/P46d's per-part pipeline). CAZ's live variants 1-4 (qeynos2) were not reproduced by this run's variant discovery (only variant 0 discovered) and remain on the pre-P48 fallback until regenerated by whoever produces a matching 5-variant set. Full detail in the manifest.

### Visual verification — composite leg

Extended the P47 magenta viewer: new `--entity-viewer-composite` flag (requires `--eqe1-path`) routes the viewer's entity through the REAL composite eligibility gate instead of forcing multi-buffer. Extended `test_entity_viewer_regression.cpp` with `CompositeLegMaskedRacesShowAlphaGapHumanDoesNot`, which additionally asserts on the `composite ADMITTED`/`composite EXCLUDED` log line (not just the magenta count) — this discriminates the composite path from the per-part FALLBACK path, which is ALSO alpha-correct (P46/P46d) and would otherwise let the test pass even if the composite path itself silently regressed to always-excluding.

Post-deploy composite-leg results (all four masked races confirmed "composite ADMITTED", frame 26, befallen zone context):

| race | front | right | back | left | admitted |
|---|---|---|---|---|---|
| SKE (21) | 0 | 251 | 688 | 0 | yes |
| ZOM (70) | 0 | 0 | 29 | 0 | yes |
| BAT (34) | 98 | 166 | 55 | 0 | yes |
| ORC (17) | 0 | 1395 | 2008 | 0 | yes |
| HUM (1, unmasked control) | 0 | 0 | 0 | 0 | yes (admitted but hasAlpha=false → EMT_SOLID, correctly zero) |

Pre/post-deploy A/B on race 70 (ZOM) proves the gate is live, not a no-op: before deploy it logged `composite EXCLUDED ... no alpha companion` (magenta via the per-part FALLBACK, numerically identical to P47's own original per-part number); after deploy the identical invocation logs `composite ADMITTED ... alpha companion (P48)` with the same visual result now coming from the composite path.

**Executor's own full-size visual verdict** (no crop-squinting, per the plan): viewed all 20 PNGs at 800×600. SKE shows a clear rib-gap magenta lattice; ZOM a small jagged boot-top sliver (matches its documented "flesh-gap" ground truth, zomch0001's chest correctly stays opaque/citywlivy-class); BAT shows unambiguous wing-membrane magenta streaks (the clearest single image); ORC shows loincloth-fringe/gauntlet-edge gaps (largest raw signal). All textures read correctly part-by-part on every masked race — no wrong-texture or duplicated-part artifact (the exact P46b failure class this whole evidence chain guards against). HUM stays fully opaque at every angle. This is the executor's own verification only — final bless on this NEW visual territory (masked composites were never seen before this unit) is Will's, not the executor's.

Contact sheet (self-contained, base64-embedded): `benchmarks/contact_sheets/p48_sheet_alpha/index.html`. Stills: `/mnt/projects/fable/videos/p48_sheet_alpha/composite_etc1/` (NFS). Full evidence: `benchmarks/audits/p48_sheet_alpha/EVIDENCE_MANIFEST.txt`.

### Pi re-measure (the payoff — and a caveat)

**Real-roster recovery (the mission's headline number): SUCCESS.**

| leg | total_p95 | draws_avg |
|---|---|---|
| P45 e205_realroster_tip (P37 active, no alpha) | 57.650ms | 348.3 |
| P45 e205_realroster_preP37 (pre-P37, target) | 39.304ms | 113.6 |
| **P48 e205_realroster_p48tip (this unit)** | **39.781ms** | **111.8** |

**Recovery: 57.650ms → 39.781ms, 97.4% of the way back to the pre-P37 target** (39.304ms); draws_avg 348.3 → 111.8, essentially matching pre-P37's 113.6. This is the P37 cost P45 measured, recovered almost completely by giving composite sheets real alpha.

**Canonical KPI-guard spot: OUT OF BAND, but proven NOT caused by this unit.**

| run | binary | total_p95 | vs 32.24-32.5ms band / 33.3ms line |
|---|---|---|---|
| e205_shipped_kpiguard | P48-tip | 34.321ms | out of band, over 33.3ms line |
| e205_shipped_kpiguard_repeat | P48-tip (confirm) | 34.624ms | out of band, over 33.3ms line |
| e205_shipped_kpiguard_preP48isolation | pre-P48 (branch base, `git stash`-isolated) | 34.509ms | **same regression, P48 diff absent** |

draws_avg=41.9 identical across all three (deterministic replay, composite not silently no-op'd — this is a pure frame-timing regression, not a structural one). The default race cycle (IDs 1-12) contains none of the races this unit regenerated, and every one of their sheets has (and always had) hasAlpha=false, so P48's new branch evaluates to the exact pre-P48 outcome for all of them — argued AND proven: a binary built from this branch's own base commit (9a61ed3, P48's 6-file diff `git stash`-ed out) reproduces the identical ~34.3-34.6ms regression. **This is a real, currently-unattributed regression on `fable` tip, most plausibly inherited from P46/P46b/P46c/P46d/P47 (landed between P45's a805e01 measurement and this unit's start) adding real entity shader-consistency promotion cost to the same default-cycle composite entities — but it is not this unit's territory to fix, and not caused by this unit's diff.** Full attribution methodology in the evidence manifest. **Escalating to orchestrator/Will: the blessed 32.24-32.5ms KPI band is not currently being met on `fable` tip, independent of P48.**

DRM mode confirmed (`Found matching mode 800x600@75Hz for requested 800x600`) on all 3 successful timing runs.

**CmaFree SessionAbort (P02 protocol working as designed):** fired once, during the isolation run's post-invocation sample (260 kB, a -74,352 kB single-step drain from 74,612 kB) — the same episodic EQE1-correlated CMA leak P02 already root-caused, not new, not P48-caused. Data was not lost (already rsync'd). Recovered via `scripted_reboot_and_wait()` (not counted as an invocation, P45 precedent) — CmaFree restored to exactly 102,056 kB (P02's documented fresh-boot value), NFS remount verified, governor `ondemand`×4, `/tmp` swept, no benchmark process, DRM device free.

**Invocation count: 4 of ≤6** (e205_realroster_p48tip, e205_shipped_kpiguard, e205_shipped_kpiguard_repeat, e205_shipped_kpiguard_preP48isolation) + 1 scripted reboot (uncounted) + 1 pristine-state verification (ssh-only, no benchmark invocation). Pi left pristine.

### Verification bar

| Step | Result |
|---|---|
| `cmake --build build -j24` (full) | PASS, clean |
| `ctest -j8` | 1628/1629 (sole exception: `AudioManagerTest.PreloadCommonSounds`, documented ALSA host quirk) — includes both `EntityViewerRegressionTest` cases (per-part leg + new composite leg) passing |
| `bash scripts/ci/visual_gate.sh` (via ctest `visual_gate` target) | PASS, goldens untouched (zone-only golden frames, entity/composite-only diff) |
| `bash scripts/build-gles2-x86.sh` | PASS, clean |
| `bash scripts/build-arm-noble.sh` | PASS, clean; `gles2_zone_benchmark` 54,491,508B / `willeq` 354,093,260B — non-stub, close to P45's recorded sizes |
| `bash scripts/refactor/check_file_sizes.sh --files <changed set>` | `entity_composite_builder.cpp` WARN (1861 lines, was 1723 WARN at P16 baseline — no new FAIL); `gles2_zone_benchmark.cpp` WARN (2967 lines, was 2934 WARN at P47 baseline — no new FAIL); all other changed files `ok` |

### Deviations

1. `mesh_building.cpp` edit — see "Sibling-territory deviation" above.
2. DEN and CAZ variants not fully alpha-enabled this pass — see "Sheets regenerated" above; both stay correctly on the safe fallback path, not a correctness issue, flagged for follow-up.
3. The canonical KPI-guard spot regressed against the 32.24-32.5ms band and the 33.3ms line — investigated and attributed to `fable` tip's post-P45 state (P46-family units), NOT this unit's diff (proven via isolated pre-P48 Pi measurement). Reported per the plan's own STOP-condition text rather than silently treating the bar as clean; this unit's own real-roster payoff (97.4% recovery) stands independent of this finding. Recommend the orchestrator open a follow-up unit to re-attribute the KPI drift across P46/P46b/P46c/P46d/P47.
4. entity_composite_builder.cpp's decodeBMP/writeEQE1/generateMipChain changes mirror `tools/eqe1_builder.cpp`'s existing conventions rather than reusing shared code (the two tools have always had independently-duplicated BMP-decode/ETC1-encode implementations, pre-existing, not something this unit introduced or was asked to consolidate).
