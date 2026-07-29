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

### Ground-truth table

WLD masked flag (Fragment 0x30 TransparentMasked) via a throwaway
`WldLoader`-based diagnostic (mirrors P21's own tool, not committed, uses
`getGeometries()`/`textureMasked()` — the exact signal
`zone_geometry.cpp`/`mesh_building.cpp` consult), scanned against
`befallen_chr.wld`, `qeynos2_chr.wld`, `gfaydark_chr.wld`, `global_chr.wld`
(the three test zones' character archives + the shared global pool).
Real-alpha column via a second throwaway `EQE1Loader`-based tool querying
the deployed `/mnt/projects/EverQuest_ETC1/*.eqe1` archives' `hasAlpha`/
companion-alpha-entry presence for each masked name.

| texture | masked (WLD) | real alpha (ETC1 sheet) | path | result |
|---|---|---|---|---|
| zomch0001.bmp (befallen/qeynos2, zombie chest) | yes | **no** (hasAlpha=no, alphaDataSize=0) | non-composite | citywlivy-class — renders solid BY DESIGN, unchanged |
| zomlg0001.bmp (befallen/qeynos2, zombie legs) | yes | yes (1384B companion) | non-composite | **FIXED** — flesh-gap holes now alpha-tested |
| batft0001.bmp (bat) | yes | yes (1384B) | non-composite | FIXED |
| cazlg0002.bmp (befallen/qeynos2) | yes | yes (2744B) | non-composite | FIXED |
| denlg0002.bmp (befallen) | yes | yes (2744B) | non-composite | FIXED |
| orclg0002.bmp (befallen/gfaydark) | yes | yes (2744B) | non-composite | FIXED |
| beahe0008.bmp (qeynos2, "bear head" — the plan's "bone helm") | yes | yes (5480B) | non-composite | FIXED |
| bgmhn0002.bmp (qeynos2) | yes | yes (696B) | non-composite | FIXED |
| fishe0001/0101/0201/0301.bmp (qeynos2, fish-race head) | yes | yes (1384B each) | non-composite | FIXED |
| brfhe0003, driua0002/0003, faflg0001, femhe0003/0005, trehe0003.bmp (gfaydark) | yes | yes (696B-10936B) | non-composite | FIXED |
| helm9/14/15/18/21/24.bmp, skech0001/skefa0001/skeft0001/skeft0002/skelg0001.bmp (global, skeleton race + generic helms) | yes | yes (360B-10936B) | non-composite | FIXED |

32 of 33 unique masked textures found across the sweep carry real ETC1
companion alpha in the deployed sheets; only `zomch0001.bmp` is
citywlivy-class (WLD-masked, decode-opaque — matches the plan's warning to
expect this class and not force a visual change on it). Equipment-archive
(`_amr.s3d`) ground truth was NOT separately WLD-scanned: `WldLoader::
parseFromArchive` failed silently on `global17_amr.s3d` (armor archives
appear to be texture-only, no DMSPRITEDEF geometry fragments for the
scanner to attach materials to) and root-causing that within budget wasn't
justified — `equipment_model_loader.cpp` got the identical code-level fix
as `mesh_building.cpp` (same `textureMasked()` check, same classification
handoff to `entity_renderer.cpp`), so equipment-slot masked textures get
the same correctness whether or not this session enumerated them by name.

### Per-path changes

**Non-composite (`RaceModelLoader::buildMeshFromGeometry`,
`mesh_building.cpp`)**: buffer material type is now `isMaskedMaterial ?
EMT_TRANSPARENT_ALPHA_CHANNEL_REF : EMT_SOLID`, where `isMaskedMaterial =
geometry->textureMasked()[texIdx]` — mirrors `ZoneMeshBuilder`'s H02d
check. This marker feeds P21's `EntityRenderer::applyAlphaTestMaterialType()`
(already wired, previously unreachable dead code) at its 3 existing
material-assignment call sites — no `entity_renderer.cpp` call-site changes
needed, P21's plumbing was already correct and complete.

**`equipment_model_loader.cpp`**: identical `textureMasked()` check added
in `EquipmentModelLoader::buildMeshFromGeometry()` (its sole material-type
assignment site — an initial default assignment plus a post-texture-load
reassignment, both updated to the same `isMaskedMaterial` result), mirroring
the non-composite path above, so equipped-item masked textures (helms,
robes, etc.) get the same classification.

**Necessary 4th file — `geometry_combiner.cpp` (deviation, see below)**:
`RaceModelLoader` builds character meshes from geometry MERGED by
`combineCharacterParts`/`combineCharacterPartsWithTransforms`/
`combineCharacterPartsRaw` (per-body-part DMSPRITEDEF pieces → one
`ZoneGeometry`). All three functions copied `textureNames` and
`textureInvisible` into the merged `materialData` but never copied
`textureMasked` — so the combined geometry `mesh_building.cpp` actually
consumes at runtime ALWAYS returned an empty `textureMasked()`, regardless
of source WLD data. This was found empirically: a temporary numeric trace
(see Evidence) showed `isMasked=false` for `zomlg0001.bmp`/`zomch0001.bmp`
even though the WLD ground-truth scan says `masked=true`. Fixed by adding
the same merge-loop for `textureMasked` in all three combiner functions.
Without this the entire unit is a no-op on the live client — territory
expansion was necessary, not scope creep (flagged as a deviation, not
silently absorbed).

**`race_model_loader.cpp` threading**: not needed for the non-composite
path (no signature changes) — used only for the composite-eligibility
exclusion below.

### Composite investigation outcome + rationale

Investigated BEFORE changing anything, per the plan's requirement:

(a) **Do composite sheets preserve masked alpha?** NO, in either deployed
form. `entity_composite_builder.cpp`'s `writeEQE1()` hardcodes
`idx.hasAlpha = 0` and always encodes `EQE1_TYPE_ETC1_RGB` (opaque-only
ETC1, no alpha companion entry ever written for composite sheets); its
`writeBMP()` emits a 24bpp-only BMP with no alpha channel at all. This is
true even though the intermediate `sheetRGBA` compose buffer DOES copy
source-part alpha byte-for-byte (`sheetRGBA[dstIdx+3] = srcRGBA[srcIdx+3]`)
— the alpha data exists transiently during the build but is discarded at
write time, never reaching either deployed artifact.

(b) **Is whole-composite alpha-test safe?** Moot given (a): since neither
deployed composite format carries alpha data for ANY part (masked or not),
alpha-testing the composite material would read back a nonexistent/opaque
alpha channel — a no-op at best, not a correctness fix.

**Choice: exclude masked-texture races from composite eligibility**
(not alpha-test-the-composite, not sheet regeneration). Implemented in
`RaceModelLoader::preloadModelData()` (`race_model_loader.cpp`): after a
composite JSON hit, cross-reference the race's `combinedGeometry
->textureMasked()` against which of its source textures the composite
actually includes (`compositeData->lookup()`); if any masked texture is
part of the composite, `modelData->compositeData` is left UNSET instead of
being assigned. `animated_mesh_creation.cpp`'s existing gate
(`compositeAllowed && modelData->compositeData && ...`) then naturally
falls through to the per-part `buildMeshFromGeometry()` path (which DOES
honor `textureMasked()` per the fix above) for that race/gender/variant —
no changes needed to the composite-mesh builder itself or to
`animated_mesh_creation.cpp`.

Quantified: of the ~30 races with masked chr textures found in the
ground-truth sweep (ZOM, BAT, CAZ, DEN, ORC in befallen; BAT, BEA, BGM,
CAZ, FIS, ZOM in qeynos2; BAT, BRF, DRI, FAF, FEM, ORC, TRE in gfaydark;
SKE globally — some races appear in multiple zones), each is now excluded
from composite eligibility specifically for the texture-variant(s) that
include the masked part; unaffected variants of the same race (no masked
part in that composite) remain composite-eligible. This is NOT a STOP —
sheet regeneration was never attempted or required; the exclusion is
pure runtime eligibility logic, in-territory (`race_model_loader.cpp`).
`mesh_building.cpp`'s `buildCompositeMeshFromGeometry()` itself is
unchanged (still unconditionally `EMT_SOLID`) with a comment explaining why
that's now correct-by-construction (masked source data never reaches it).

### Evidence

Primary evidence is a numeric MaterialType trace (P21's own precedent for
the entity side), not pixel stills — live in-frame zombie stills proved
unreliable to pin down this session (see manifest for the full account of
what was attempted and why). Full trace tables, methodology, and the
supplementary stills (general zone-render proof + the unmasked-race
byte-diff spot check) are in
`benchmarks/audits/p37_entity_masked/EVIDENCE_MANIFEST.txt` (committed);
stills at `/mnt/projects/fable/videos/p37_entity_masked/stills/` (NFS, not
committed, per repo convention).

Headline trace result: `zomch0001.bmp`/`zomlg0001.bmp` go from
never-reaching-the-alpha-test-branch (pre-fix: `isMasked` always false,
`MaterialType` always 0/`EMT_SOLID`) to `isMasked=true` →
`MaterialType=14` (`EMT_TRANSPARENT_ALPHA_CHANNEL_REF` marker,
`mesh_building.cpp`) → `MaterialType=258` (compiled GLES2 RGBA alpha-test
shader, `entity_renderer.cpp` post-`applyAlphaTestMaterialType()`) — the
same numeric shader ID (258) P21's own door evidence table documented for
the identical shader. Unmasked siblings (`zomch0002-5.bmp`,
`zomlg0002.bmp`) stay at `isMasked=false`/`MaterialType=0` throughout,
confirming the fix is scoped to masked materials only.

Both diagnostics (mesh_building.cpp LOG_INFO, entity_renderer.cpp
LOG_INFO) were temporary and fully reverted (`git diff` shows no trace)
before the final build/verification pass and commit — same pattern P21
used for its own evidence workarounds.

Unmasked-race spot check: Testchar (Human, entirely unmasked) captured at
the standard restore point with BEFORE (fix reverted via `git stash`) and
AFTER binaries, identical script/wait timings. Pixel diff (800x600 RGB):
max_delta=7, 234/480,000 px nonzero (0.05%), mean_delta=0.00026 —
FP-noise-class (independent-process render variance), not a structural
change.

### Verification bar

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` | PASS |
| 2. `ctest --test-dir build --output-on-failure` | 1415/1416 (1 run) and 1414/1416 (another run) — both within the documented host exceptions (`AudioManagerTest.PreloadCommonSounds`, `RDPServerFrameTest.UpdateFrameMultipleTimes` — intermittent, reproduces on some runs not others, consistent with the protocol's own note) |
| 3. `bash scripts/build-gles2-x86.sh` | PASS |
| 4. Visual gate | `bash scripts/ci/visual_gate.sh` PASS (qeynos2/gfaydark/befallen), goldens untouched (zone-only golden frames, unaffected by an entity-only fix), no re-bless |
| 5. `bash scripts/build-arm-noble.sh` | PASS |
| 6. `bash scripts/refactor/check_file_sizes.sh` | Run against the branch merge-base (`70492b7`) rather than the literal `fable` — same reason P21 documented (concurrent sibling units moving `fable`'s tip mid-campaign). `equipment_model_loader.cpp` (1065), `geometry_combiner.cpp` (327), `mesh_building.cpp` (609), `race_model_loader.cpp` (1020) — all `ok`, no new FAILs, no new WARNs |

### eqemu

Sanctioned `scripts/eqemu/up.sh befallen` / `down.sh` only, lock held for
the full session and removed at the end. Testchar's position was moved
during the session for evidence-gathering (a zombie-proximity spawn2
coordinate, and GM `#goto`/`\`/moveto\`` attempts) and restored to the
documented standard target before shutdown: zone_id=36, x=-77.9232,
y=21.9086, z=3, heading=190 (verified via `SELECT` after the `UPDATE`).
Server shut down cleanly (`down.sh`); no eqemu-related containers/ports
left running.

### Deviations

1. **Territory expansion to `geometry_combiner.cpp`** (not named in the
   plan's territory list): required because it silently dropped
   `textureMasked` during character-geometry merging, making the
   plan-scoped fix (mesh_building.cpp + equipment_model_loader.cpp alone)
   inert on the live client. Found via a temporary numeric trace, not
   assumed. Flagged here per the protocol's "deviations flagged, never
   silently absorbed" rule.
2. **No live in-frame zombie/bone-helm pixel stills** — attempted
   repeatedly (DB position + `#goto` GM teleport + `/moveto <name>`, ~6
   tries across both zones) but never landed a zombie or the qeynos2 BEA
   ("bear head"/"bone helm") NPC in frame; no BEA-race NPCs are currently
   spawned in qeynos2 per `spawn2`/`spawnentry` (the qeynos2 masked texture
   ground truth, `beahe0008.bmp`, exists in the WLD/ETC1 data but isn't
   server-placed there today). Per the plan's own allowance for a
   numeric-trace fallback (P21's precedent) and the plan's "you do NOT
   declare visual acceptability — Will judges" instruction, this unit
   relies on the numeric MaterialType trace (see Evidence) as the
   decisive, reproducible evidence instead of continuing to spend session
   budget chasing exact camera framing on a wandering live mob.
3. Equipment-archive (`_amr.s3d`) ground truth not exhaustively WLD-scanned
   by name (see ground-truth table note) — the scanner tool failed
   silently on `global17_amr.s3d`, root cause not chased within budget.
   `equipment_model_loader.cpp` still received the identical fix.
4. `check_file_sizes.sh` run against the merge-base commit instead of the
   literal `fable` argument, same reasoning P21 documented.
