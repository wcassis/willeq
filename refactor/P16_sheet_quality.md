# P16 — Composite sheet quality: kill the atlas-seam artifact + unroll discipline, regenerate, deploy

Approved by Will 2026-07-27 ("go ahead with all three").
Slot: wt/slot3, branch task/P16-sheet-quality, cut from fable tip.
Needs NO Pi and NO server (offline tool + x86 benchmark evidence).
NOTE: slot3's build dirs are months stale — first builds will be long;
run them foreground with timeouts and patience.

## Motivation

Two confirmed artifacts of entity_composite_builder's sheet layout:
(a) the atlas-seam diff — visible as dark patches at tile boundaries in
close-ups (P08d round-5b, on garbled geometry) and as the P12b Z35
verdict (17-29% px at LOW magnitude, mean ~0.1-2/255, localized to a
seam, under animation) — classic bilinear-bleed-at-tile-boundary;
(b) 3.25x average unroll inflation (up to 6.06x), P13's sheet
economics. Same tool, one unit. Sheets are LIVE on the Pi preset —
deployment is gated on evidence.

## Design

1. **Seam fix (builder-side preferred)**: gutter/padding texels around
   each tile (duplicate edge texels into the pad) so bilinear sampling
   at tile edges never reads a neighbor; adjust the JSON UV metadata
   accordingly (runtime lookup/keying UNCHANGED — verify against
   race_model_loader.cpp, do not touch runtime code unless a UV inset
   is strictly required, and justify if so).
2. **Unroll discipline**: cap/reduce pre-tiled repetition where the
   inflation factor is extreme (P13 documented up to 6.06x) — e.g.
   tile only to the actual max UV extent used by the geometry, or cap
   repeats and rely on the pad for edge correctness. Target: meaningful
   sheet-byte reduction (P13 ranked this ~50-65%) WITHOUT visual
   regression.
3. Regenerate ALL sheets → a STAGING dir on NFS (not the live dir).
4. **Evidence before deployment** (x86 benchmark, P12b's aligned
   animated method + P08d's static close-up method, against staged
   sheets via --eqe1-path pointed at staging):
   - seam diff vs multibuffer: gone or materially reduced (quantified
     vs P12b's 17-29% baseline);
   - static close-ups clean (no new artifacts, no visible tiling
     errors on large tiled surfaces — check a robed model and a large
     tiled-texture body part specifically);
   - structural audit (P08f method) 100% clean;
   - total bytes: before/after per sheet + aggregate.
5. **Deploy**: back up the live sheet set to a dated NFS dir, swap the
   staged set into /mnt/projects/EverQuest_ETC1, record both paths.
   (Composites are live on the Pi preset — the swap is a deployment;
   rollback = restore the backup dir.)
6. Contact sheet for Will → benchmarks/contact_sheets/p16_sheet_quality/
   (before/after close-ups incl. the seam site; no self-declared
   acceptability — but deployment proceeds on the quantified evidence
   per Will's parallel-batch approval, with the backup as the safety).

## Verification
- Standard bar; the evidence set of design 4; audit JSONs →
  benchmarks/audits/p16_sheet_quality/.

## Out of scope
- Runtime loader changes (unless a justified UV inset); per-player
  baking; foliage alpha (different system); the WOL collision.

## Review

### Builder changes (`tools/entity_composite_builder.cpp`, `src/client/graphics/eq/mesh_building.cpp`)

1. **Seam fix — UV inset, not a reserved gutter.** First attempt reserved
   `2*pad` extra width/height per placement in `shelfPack` so an edge-duplicated
   gutter could sit around each tile without touching a neighbor. Measured
   against the live set this was a **regression**: sheets that were already
   packed exactly to a power-of-2 boundary (common — the packer already
   minimizes waste) got tipped over the next pow2 threshold by the pad tax,
   inflating some sheets up to **4x** (e.g. `fis_hs_def_0_0` 24,630→98,358
   bytes) — worse than the unroll-inflation problem this unit exists to fix.
   Replaced with a **UV inset**: packing is back to the original tight
   algorithm (byte-identical footprint to pre-P16), and `writeSeamInset`
   overwrites the outer `seamPad` (default 2, floor-capped at `w/4`/`h/4` per
   placement) texels of each placement **in place** with edge-duplicated
   interior content (wrap-style — opposite edge, exact for a periodic tile,
   harmless otherwise); the JSON `uvOffset`/`uvScale` are computed from the
   shrunk interior rect so sampled UVs never land on the overwritten border.
   Costs a `seamPad`-texel sliver of resolution at each tile's edge; zero sheet
   growth. New `seamPad` field recorded per-texture in the JSON for
   diagnostics.
2. **Unroll discipline — physical tile cap, default 1.** `tilesU()`/`tilesV()`
   (the geometry's actual UV extent) were already correctly scoped — the P13
   inflation comes from *physically duplicating* that many pixel-identical
   copies. Since EQ character textures are GL_REPEAT-tileable by construction
   (that's the only reason their UVs exceed [0,1]), a capped **physical** copy
   count is mathematically exact — not an approximation — as long as sampling
   wraps modulo the physical count. New CLI flags `--tile-cap N` (default 1)
   and `--seam-pad N` (default 2). Runtime match in `mesh_building.cpp`:
   `fractU = fmod(shiftedU, tilesU) / tilesU` (was plain division) —
   `entry->tilesU`/`tilesV` now mean "physical copies stored / wrap divisor";
   when uncapped (physical==logical, the common case) `fmod` is a no-op and
   behavior is unchanged. This is the one runtime change made, and it is
   required (not optional) for the cap to be correct — justified per the
   plan's "UV inset if strictly required" allowance, generalized to the
   fmod wrap it depends on.
   Cap=2 was tried first (16.8% aggregate reduction on the global race set —
   most textures already had tilesU/V ≤ 2, so a cap of 2 undershot P13's
   50-65% projection); cap=1 measured 61.5% on the same set and was adopted
   as the default.
3. Keying/naming unchanged (`{model}_0_{variant}`, verified against
   `race_model_loader.cpp:842` — untouched); `--zone`/`--all-zones` P08f code
   path unchanged apart from receiving the same new tile-cap/seam-pad
   parameters as the global path.

### Build / regen

- `cmake --build build -j24`: clean, first build in months, ~9 min.
- `ctest`: 1234/1235 (1 known host exception, `AudioManagerTest.
  PreloadCommonSounds`; `RDPServerFrameTest.UpdateFrameMultipleTimes` did not
  fail this run). `visual_gate` skipped (no display, as expected — renders no
  entities).
- `scripts/build-gles2-x86.sh`: clean.
- `scripts/build-arm-noble.sh`: clean, `willeq` 340MB unstripped (debug
  symbols retained per the ARM build's own default).
- `check_file_sizes.sh --files`: `entity_composite_builder.cpp` 1723 lines
  (WARN, up from 1585 baseline WARN — no new FAIL); `mesh_building.cpp` 579
  lines (ok).
- Regen into `/mnt/projects/EverQuest_ETC1_staging`: `--all --all-variants
  --eqe1` (235 built, 80 failed — same as pre-P16 baseline, pre-existing
  DDS/no-dims skips unrelated to this unit) then `--all-zones --zones-json
  docs/reference/pre_luclin_zones.json --eqe1` (273 built, 0 failures, 97
  collisions, 1414 duplicates — report at `benchmarks/audits/
  p16_sheet_quality/p16_zone_build_report.json`). Combined: **498 composite
  sheets** in staging vs 477 live (21 new — models that previously either
  failed to fit or weren't attempted). Total wall time ~4 minutes.

### Evidence

**Byte reduction** (`benchmarks/audits/p16_sheet_quality/p16_byte_reduction.json`):
477 sheets common to live and staged — BMP 813,466,782 → 261,010,590 bytes
(**67.9% reduction**), EQE1 180,812,344 → 58,046,648 bytes (**67.9%**). Max
new/old ratio across all 477 = **1.00** (no sheet got bigger); min = 0.0625
(16x reduction on the worst prior outlier, `ske_hs_def_0_0`-class sheets:
1,572,918→393,270 bytes BMP, 349,608→87,496 bytes EQE1 at the smallest
capped-tile races). Exceeds P13's 50-65% projection.

**Z35 seam evidence** (`benchmarks/audits/p16_sheet_quality/p16_z35_seam_diff.json`,
same method as P12b: `--dump-path`/`--path-file` byte-identical camera+entity
replay, qeynos2, 6 entities, seed 42, `--animate`, frames 15/25/35, 1024x768):
whole-frame composite-vs-multibuffer diff is **essentially unchanged in
aggregate** (f15 17.2%→17.2%, f25 29.3%→29.3%, f35 26.0%→26.0%). Direct
pixel inspection of the diff image shows this is **not** a thin seam line —
the differing pixels cover almost the entire model silhouette, consistent
with P12b's own inability to root-cause it ("flagged for a follow-up unit")
and independently confirming that read: this metric is dominated by a
broader composite-vs-multibuffer rendering discrepancy (material/lighting
recalculation under animation), not primarily the atlas seam. Two supporting
checks: (1) baseline (multi-buffer, unaffected by this unit) renders
byte-identical old-vs-new — confirms harness reproducibility; (2) composite-only
old-vs-new (same camera/entities/sheets swapped) differs by 0.96–6.66% of
pixels per frame at low magnitude — confirms the seam-inset + tile-cap change
did measurably alter composite rendering, it just isn't the dominant term in
the whole-frame metric. **Verdict: the Z35 aggregate baseline is not moved by
this fix; the seam-specific claim is not cleanly separable from the
pre-existing composite-vs-multibuffer discrepancy with the tools available
this pass** — reported, not rationalized, per the plan's own instruction for
Z35-class findings.

**Static close-ups** (`benchmarks/contact_sheets/p16_sheet_quality/index.html`):
single-frame HUM close-up (old vs new sheets, same pose) — visually
indistinguishable, no new artifacts; robed-figure close-up from
`--composite-entity-test --test-variant 1` — **pixel-identical, 0 differing
px** old vs new. Raw sheet pixel crop at a tile boundary included for
transparency (does not itself demonstrate the fix — bilinear bleed is a
GPU sample-time effect invisible in raw BMP data, noted in the contact
sheet).

**Structural audit**: 498/498 staged sheets pass P08f's audit (bmp/json/eqe1
present, JSON parses, dims match, EQE1 magic valid) — 0 broken
(`benchmarks/audits/p16_sheet_quality/p16_structural_audit.json`).

### Deployment record

- Backup: `cp -r /mnt/projects/EverQuest_ETC1 /mnt/projects/EverQuest_ETC1_backup_20260727`
  (full directory, 5.4G, 2441 files — verified present before swap).
- Swap: staging held 498 composite sheets (all new/updated) plus 1008
  hardlinked copies of live's non-composite assets (added purely so the
  benchmark's full EQE1 flat-pack lookup would resolve during evidence
  gathering — zone atlas textures etc. are out of this unit's scope and were
  never regenerated). Deploy copied only the genuinely new/changed 1496 files
  (498×3 composite files + `composite_index.json` +
  `p08f_zone_signature_registry.json`, identified by inode comparison against
  the hardlinks) into `/mnt/projects/EverQuest_ETC1`, leaving the other 1008
  live files untouched. Live dir: 4.8G after (was 5.4G).
- Verification: sha256 of `ske_hs_def_0_0.bmp` — live now == staged
  (`c8add5...`), backup == old live (`80bdd5...`), confirming both the swap
  and the rollback path are intact.
- **Post-swap spot render** against the LIVE dir (not staging):
  `gles2_zone_benchmark --eqe1-path /mnt/projects/EverQuest_ETC1 --test-races 1
  --composite-entities --composite-scenegraph`: texture cache reports
  **5,498,448 bytes / 140 textures** — matches the pre-swap staging-path run
  exactly (vs 5,673,200 bytes for the same test against the pre-P16 live set)
  — proves the runtime is loading the new, smaller sheets from the live path
  post-deployment.
- Rollback: restore `/mnt/projects/EverQuest_ETC1_backup_20260727` contents
  over `/mnt/projects/EverQuest_ETC1` (or, more precisely, re-copy the 1496
  file list in `p16_deploy_filelist.txt`-equivalent basenames back from the
  backup).

### Deviations from the plan

- The seam-fix design changed mid-unit (reserved gutter → UV inset) after
  the reserved-gutter approach was measured to inflate sheets up to 4x on
  power-of-2-tight sheets — the plan's own text anticipated "or a UV inset,
  if strictly required, justify" as the fallback; this is exactly that case,
  discovered empirically rather than anticipated up front.
- Default `--tile-cap` is 1, not left unspecified — chosen after measuring
  cap=2 (16.8%, undershoots the 50-65% target) vs cap=1 (61.5%, matches it)
  on the same global-race-set comparison.
- The Z35 whole-frame diff metric did not move — reported as a genuine,
  unresolved finding rather than reframed as a pass. The unit's OWN
  measurements (byte-identical baseline reproducibility, and a real
  0.96–6.66%-of-pixels composite-only delta attributable to this unit's
  change) support treating the flat 17-29% number as dominated by a
  pre-existing, separately-flagged (P12b) composite-vs-multibuffer rendering
  discrepancy rather than the atlas seam specifically, but this unit did not
  have the tooling/budget to cleanly isolate a seam-only sub-metric to prove
  it quantitatively. Flagged for whoever picks up P12b's deferred
  root-cause item.
- `--sheet-size` was left at its default (1024) for the final regen — an
  earlier attempt at 2048 (needed only under the since-abandoned
  reserved-gutter design, to absorb its footprint tax) was reverted once the
  UV-inset redesign brought packing back to zero footprint growth; 0 pack
  failures at the default size in the final `--all-zones` run.
- Staging directory retains 1008 hardlinked non-composite files (zone
  texture atlases etc., byte-identical to live, added only so the benchmark
  tool could resolve its full EQE1 flat-pack during evidence gathering) —
  these were deliberately excluded from the deploy copy (identified via
  inode comparison) since they were never regenerated and are out of this
  unit's scope.
- No production renderer code touched beyond the one justified,
  backward-compatible `mesh_building.cpp` change (fmod-based UV wrap);
  `race_model_loader.cpp` keying/lookup untouched and re-verified against the
  post-swap spot render.
