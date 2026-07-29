# P37 — Entity textureMasked: masked character textures render masked

**Slot: slot1. x86 only — no Pi. OWNS EQEMU (honor the .eqemu-lock
protocol). Sole agent this dispatch.**

P21's audit finding (deferred.md): the entity/equipment mesh builders
— `RaceModelLoader::buildMeshFromGeometry` AND
`buildCompositeMeshFromGeometry` (`mesh_building.cpp`) plus
`equipment_model_loader.cpp` — hardcode `EMT_SOLID` for every material
and never consult `textureMasked()`. WLD-masked character textures
exist and render OPAQUE today (named ground truth: `zomch0001`/
`zomlg0001` befallen zombies, `beahe0008` qeynos2 bone helm). This is
era-authentic transparency (zombie flesh gaps, helm holes) — the fix
routes them through P21's already-wired classification.

## Trusted context

- P21 landed `entity_renderer.cpp::applyAlphaTestMaterialType()`
  (masked+ETC1 → companion bound on unit 1 / masked+RGBA → RGBA
  variant) and the 3-arg `setShaderMaterialTypes()` — currently
  unreachable dead code because upstream never assigns
  `EMT_TRANSPARENT_ALPHA_CHANNEL_REF` to entity materials. Your
  upstream change is what makes it live. Do NOT reimplement the
  classification — feed it.
- `citywlivy` class exists among character textures too, possibly:
  WLD-masked but painted opaque → renders solid BY DESIGN. Check each
  ground-truth texture's actual alpha/color-key data before promising
  visual change (P20's follow-up set the standard here).
- Composite entities (P08d) merge all body parts into ONE buffer/ONE
  material — deferred.md's warning: per-material alpha-test may not
  compose. Befallen zombies are plausibly composite-ELIGIBLE, so this
  is central, not an edge case.

## Work

1. **Ground truth sweep**: enumerate WLD-masked materials across the
   test zones' `_chr` archives (befallen/qeynos2/gfaydark) + equipment
   models; for each, decode and check REAL alpha/color-key data
   (report the citywlivy-class ones separately). Confirm live opacity
   today with before stills (same-pose discipline) of at least the
   befallen zombies and the qeynos2 bone-helm wearer.
2. **Non-composite path**: make `buildMeshFromGeometry` +
   `equipment_model_loader` assign
   `EMT_TRANSPARENT_ALPHA_CHANNEL_REF` for masked materials (mirroring
   `ZoneMeshBuilder`'s ground-truth check), so P21's classification
   fires. Verify the texture-source side delivers alpha on BOTH
   pipelines: RGBA/DDS (reads own alpha — should just work) and ETC1
   sheets (does the entity sheet pipeline carry companion alpha pages
   for masked parts? VERIFY — if not, the RGBA variant is only correct
   for the no-`--eqe1-path` config; report what production on the Pi
   would need).
3. **Composite path — investigate before changing**: (a) do composite
   sheets preserve the masked parts' alpha data? (b) is
   whole-composite alpha-test safe (opaque texels pass the discard
   test, so fully-opaque parts should be unaffected — verify on real
   sheets, don't assume)? Then EITHER apply alpha-test to composites
   whose source parts include masked textures, OR exclude
   masked-texture races from composite eligibility (correctness over
   draw-count; quantify how many races that excludes). If the correct
   fix requires REGENERATING deployed sheets (builder change + NFS
   deploy), STOP after the investigation and report — sheet
   redeployment is a Will-visible operation with its own
   backup/deploy protocol.
4. **Evidence for Will's bless** (production-visible): same-pose
   before/after stills — befallen zombies (flesh gaps) and the
   qeynos2 bone helm — per the P20b bar (identical scripts, stated in
   Review; stills to `/mnt/projects/fable/videos/p37_entity_masked/`
   + committed manifest under `benchmarks/audits/p37_entity_masked/`).
   Any tree in frame staying odd, any entity texture that is
   citywlivy-class: name it, don't gloss. You do NOT declare visual
   acceptability.
5. Note (report-only): whether the fix changes composite eligibility
   or draw counts anywhere — if yes, flag that the next Pi
   measurement should be attributed here, not treated as mystery.

## Rules

- Territory: `mesh_building.cpp` (both entity build paths),
  `equipment_model_loader.cpp`, `race_model_loader.cpp` if the flag
  must thread through, entity_renderer only if P21's wiring needs a
  call-site fix. NOT zone_geometry, NOT the driver, NOT the sheet
  builder tool or deployed sheets (STOP rule above).
- Goldens untouched (run the gate; STOP if it moves — golden frames
  are zone-only but verify, don't assume).
- Behavior scope: masked entity materials only; non-masked entities
  byte-identical (spot-verify: one unmasked race renders identically
  before/after — screenshot compare).
- eqemu per protocol (lock, sanctioned scripts, Testchar restore to
  zone_id=36, x=-77.9232, y=21.9086, z=3, h=190, clean shutdown, lock
  removed).
- STOP conditions: composite fix needs sheet regeneration (report the
  full requirements instead); masked textures turn out to lack real
  alpha across the board (then this is a citywlivy-class no-op —
  report, don't force); gate moves.

## Verification

- Full current bar: GLES2 tree build + ctest at current baseline
  (~1406/1407, ALSA exception; report actual), build-gles2-x86.sh
  clean, visual gate PASS goldens untouched, ARM build clean, file
  sizes clean.
- The unmasked-race byte-identical spot check.
- The step-4 evidence pairs + ground-truth table (texture, masked
  flag, real alpha?, path [composite/multibuffer/equipment], result).

## Review

(executor fills in: ground-truth table, per-path changes, the
composite investigation outcome + choice rationale, evidence paths,
verification numbers, deviations)
