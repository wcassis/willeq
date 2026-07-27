# P13b — Fix the confirmed double-load: per-part textures become lazy when a composite sheet hits

Authorized in advance by Will 2026-07-27 ("go ahead with the top fix
from the ranked list"). Slot: wt/slot1, branch task/P13b-perpart-lazy,
cut from fable (post-P13 tip). P13's top-ranked fix (surgical,
confirmed waste); includes a BOUNDED side-quest on the unreproduced
71-eviction scenario.

## Motivation (P13 findings)

CONFIRMED double-load: RaceModelLoader::preloadModelData's per-part
decode/upload loop is unconditional — entity_perpart uploads were
identical (268) with composites on or off, so every composite sheet is
pure net addition (+3.1-3.7MB in qeynos2) with zero savings realized.
The whole point of sheets is to REPLACE per-part textures for eligible
entities.

**Correctness constraint (the reason this is lazy, not a blanket
skip)**: eligibility is per-ENTITY (P08d gate: players and
per-slot-override NPCs use the multi-buffer path and NEED per-part
textures) — a race can have both eligible and ineligible entities in
one zone. Per-part textures for a race must therefore load ON DEMAND
(first time an ineligible entity of that race actually builds a
multi-buffer mesh / needs the texture-name swap), not never.

## Design

1. In preloadModelData (and any other unconditional per-part
   decode/upload site P13's ledger callsites identify —
   loadZoneStep8c_VariantTextures / 8d_EquipmentPrep included): when a
   composite sheet HIT occurs for a (race, gender, variant), DEFER the
   per-part decode/upload — record enough metadata to do it later
   (archive/name list), do not touch the GPU.
2. On-demand path: the multi-buffer mesh build (and the equipment
   texture-name swap machinery) triggers the deferred per-part
   load for that race when an ineligible entity needs it. Thread-
   correctness per the existing loading/background-pool rules (uploads
   via the same paths they use today — no new GL-on-render-thread).
3. Composite MISS (no sheet): behavior byte-identical to today (eager
   per-part), so races without sheets are unaffected.
4. Ledger-verified outcome (the P13 instrumentation is the acceptance
   harness): re-run the P13 qeynos2 composites-ON scenario —
   entity_perpart uploads should DROP for fully-eligible races (report
   before/after counts and bytes; expected: several MB saved and
   per-part uploads only for races with ineligible entities present);
   composites-OFF run byte-identical to P13's (271-line diff of
   ledgers acceptable only in ordering).
5. Visual safety: an eligible-entity render and an ineligible-entity
   render (equipment-wearing NPC or player) both correct post-change —
   spot captures; the P08d contact-sheet method at 2 poses.

## Side-quest (bounded): reproduce the 71-eviction scenario

6. P13's controlled single-zone-in runs (15.68MB, 0 evictions) did NOT
   reproduce P10b's 71 evictions. Two experiments MAX, with the ledger
   on:
   a. Multi-zone session in ONE process (character in befallen → zone
      to qeynos2, or login-flow default): does cross-zone texture
      accumulation (no full teardown between zones) drive eviction?
   b. Replicate P10b's exact session shape (its Review/README documents
      the repro configs — same preset copies, same entry path).
   Whatever reproduces (or doesn't): record the ledger + eviction
   trace, update the deferred item; do NOT chase further fixes here.

## Verification

- Standard bar: build, ctest (known host exceptions), gles2-x86 build,
  visual gate PASS goldens untouched, ARM build, file-size check.
- Design-4 ledger before/after tables; design-5 visual spot checks.
- Sanctioned server flow; clean shutdown; configs/character restored.

## Out of scope

- Ranked fixes #2 (builder unroll discipline) and #3 (lazy sheet
  loading) — separate units if the data keeps pointing at them.
- Eviction fixes beyond the bounded repro attempt.
- ETC1-sheets-everywhere; budget changes.

## Review

Executed on `wt/slot1`, branch `task/P13b-perpart-lazy`, cut from `fable` at
`9cff5da` (the plan's own commit, verified via `git log`).

### Design implemented

**1. Deferral (composite HIT → skip eager per-part decode).**
`RaceModelLoader::preloadModelData`'s T05 loop (`race_model_loader.cpp`) is now
gated on `modelData->compositeData` (set immediately above, by the composite
JSON lookup that already existed): when set (a HIT this call), the per-part
decode/upload loop is skipped entirely — no `constrainedTextureCache_->getOrLoad`
call, no GPU touch — and every texture name that *would* have been decoded is
recorded into a new member, `deferredPerPartTextureNames_`
(`race_model_loader.h`, a `std::set<std::string>` guarded by the existing
`preparedDataMutex_`). On a MISS (`compositeData` null — no sheet, or
composites off entirely) the loop is byte-identical to before P13b.

**2. On-demand trigger — turned out to be TWO pre-existing lazy-load sites,
not one, and fixing the fix in progress uncovered a second bug.**
`buildMeshFromGeometry` (`mesh_building.cpp`, the multi-buffer path
`compositeAllowed=false` forces) already had a per-texture fallback:
`meshBuilder_->getOrLoadTexture(name)` → miss → `findTextureData(name)` (reads
raw bytes straight out of `modelData->textures`, no dependency on my new set)
→ `meshBuilder_->loadTextureFromBMP(name, data)` → `constrainedTextureCache_
::getOrLoad`/`uploadTexture`. This is the SAME mechanism equipment-texture-
variant overrides already used unconditionally (`bodyTextureVariant>0`
branch) — nothing new had to be built for it, it just needed the T05 gate
above to actually leave something un-decoded for it to catch. Confirmed via
the qeynos2 ledger acceptance run (below): human/Qeynos-citizen base ("00"
page) per-part textures show up with `callsite=loadTextureFromBMP`, decoded
on demand, only for the entities that actually needed the multi-buffer path.

**A second, real bug found while validating this** (not anticipated by the
plan, found because the first ledger acceptance run's `entity_perpart` count
went UP, not down): `getAnimatedMeshForRace` called `getMeshForRace(raceId,
gender)` first thing, purely to null-check ("ensure the model is loaded")
and then discarded the returned `IMesh*` — but building that discarded mesh
ran `ZoneMeshBuilder::buildTexturedMesh()`, which **unconditionally decodes
every per-part texture the race's static mesh needs**, for every entity,
composite-eligible or not. This silently defeated the whole deferral: a
composite-HIT race's per-part set got skipped by T05, then immediately
decoded anyway by this side effect, before `getAnimatedMeshForRace` even
reached the composite-vs-multi-buffer branch. Fixed by extracting the
"search `loadedModels_`, load from S3D archives if missing" logic (previously
inline in `getMeshForRace`) into a new method, `ensureModelDataLoaded()`
(declared in `race_model_loader.h`, defined in `race_model_loader.cpp`) that
does NOT build a mesh — `getMeshForRace` now calls it then proceeds to build
+cache its static mesh exactly as before (unaffected, still eager, still
correct for its real callers — `entity_renderer.cpp`'s static-mesh path,
`variant_loading.cpp`); `getAnimatedMeshForRace` (`animated_mesh_creation.cpp`)
now calls `ensureModelDataLoaded()` directly instead, skipping the wasted
build. This is the fix that makes the deferral's savings real rather than
merely relabeled — see the acceptance numbers below (a second full ledger run
after this fix, not just the first).

**3. Composite MISS / composites off: byte-identical.** Verified by the Run B
acceptance below — 656 uploads / 13,148,607 bytes, digit-for-digit identical
to P13's own committed `run_b_qeynos2_composites_off.txt` category totals.

**4. Threading.** No new GL call site anywhere — both the T05 skip (pure
bookkeeping) and the on-demand trigger (the pre-existing `loadTextureFromBMP`
fallback, called from whichever thread `buildMeshFromGeometry`/
`createAnimatedNodeWithEquipment` already runs on today — the loading thread
during zone load for the initial entity population, or the render thread for
later entity spawns, exactly as equipment-variant overrides already worked)
touch the GPU. `getMeshBuilder_`'s `uploadTexture()`/`addTexture()` calls are
the same calls that already ran on these threads before this unit.

**A correctness hazard found and closed (not anticipated by the plan, found
by reading `renderer_zone_stream.cpp` before shipping):** `releaseTextureData
AfterUpload` (on for the `orangepi` tier — `totalMemoryBudgetBytes=128MB` in
`constrained_renderer_config.cpp`'s `OrangePi` case) calls
`RaceModelLoader::releaseRawTextureData()` once, at "progressive loading
complete," which clears `TextureInfo::data` for every texture in
`globalTextures_`/`numberedGlobalTextures_`/`zoneTextures_`/
`otherChrCaches_[*].textures` — **unconditionally**, including the raw bytes
a deferred per-part texture's on-demand decode depends on. Races persist in
`loadedModels_` across zone transitions (confirmed via `clearMeshCaches()`'s
own comment on this exact hazard for a different reason), so a deferred
race's per-part textures could still be needed in a LATER zone visit, long
after this release pass runs. Fixed: `releaseRawTextureData()`
(`model_loading.cpp`) now skips any name present in
`deferredPerPartTextureNames_` — the "record enough metadata to do it later"
the plan's design point 1 asked for turned out to be load-bearing for this
reason, not just bookkeeping. `deferredPerPartTextureNames_` is never
cleared (same lifetime as `loadedModels_`) — a name stays protected for the
`RaceModelLoader`'s whole life once first deferred.

### Ledger acceptance (design point 4)

Real x86 GLES2 client (`build-gles2/bin/willeq`, EGL surfaceless llvmpipe),
sanctioned eqemu automation (`scripts/eqemu/up.sh qeynos2`/`seed.sh`/`down.sh`),
account `willeq_test`/character `Testchar` (GM), `800x600`, `--eqe1-path
/mnt/projects/EverQuest_ETC1`. Testchar's `character_data` row pointed at
qeynos2's `safe_x/safe_y/safe_z` (`zone_id=2, x=-74, y=428, z=3`) before each
run, restored to its documented original (`zone_id=36, x=-77.9232,
y=21.9086, z=3, heading=190`) after all runs — confirmed via `SELECT`, same
pattern P13 used. Script: `wait 60` (zone load + settle) → `/camp`.

- **Run A — qeynos2, composites ON** (shipped `orangepi` preset,
  unmodified): **558 uploads, 16,018,887 bytes (16.02MB), 0 evictions** (vs
  P13's own Run A: 670 uploads, 16,440,863 bytes/15.68MB, 0 evictions).
  Ledger: `benchmarks/audits/p13b_perpart_lazy/run_a_qeynos2_composites_on.txt`.
- **Run B — qeynos2, composites OFF**: temporary preset
  `orangepi_p13b_nocomposites` (byte-identical to `orangepi` except
  `enableCompositeEntities:false`, `compositeEntityPath:""`), added to
  `config/constrained_presets.json` for this run only, reverted immediately
  after (`git checkout --`, confirmed clean via `git status`). **656
  uploads, 13,148,607 bytes (12.54MB), 0 evictions** — digit-for-digit
  identical to P13's own committed Run B (656 uploads, 13,148,607 bytes) —
  category totals match exactly too (`zone_region` 126/5,280,160, `object`
  84/3,627,704, `uncategorized` 173/3,164,994, `entity_perpart`
  268/1,039,856, `sky` 4/35,552, `placeholder` 1/341). Composites-OFF is
  provably byte-identical, satisfying design point 3.
  Ledger: `benchmarks/audits/p13b_perpart_lazy/run_b_qeynos2_composites_off.txt`.

**Before/after category table** (`benchmarks/audits/p13b_perpart_lazy/
category_totals.csv`):

| category | P13 Run A (ON, pre-fix) | P13b Run A (ON, post-fix) | Δ |
|---|---|---|---|
| entity_perpart | 268 tex / 1.084MB | 95 tex / 0.374MB | **-65% uploads, -0.71MB** |
| composite_sheet | 14 tex / 3.047MB | 14 tex / 3.195MB | unchanged (same 14 sheets found) |
| uncategorized | 173 tex / 3.018MB | 234 tex / 3.506MB | +61 tex / +0.49MB |
| zone_region / object / sky / placeholder | unchanged | unchanged | 0 |
| **TOTAL** | **670 tex / 15.68MB** | **558 tex / 16.02MB** | **-112 uploads, +0.34MB** |

**Reading this honestly (not overclaiming):** `entity_perpart` — the category
P13's audit tagged as the confirmed double-load mechanism (T05,
`loadZoneStep8c_VariantTextures`, `loadZoneStep8d_EquipmentPrep`) — dropped
65%, exactly what design point 4 asked to demonstrate: the eager,
unconditional per-race decode is genuinely gone for composite-hit races.
BUT total zone-in bytes did **not** drop by a comparable amount, and actually
rose slightly (+0.34MB) — because `uncategorized` (where the ON-DEMAND
decode for deferred textures lands — see "categorization is deliberately
NOT `entity_perpart`" below) picked up +61 textures. Root cause, confirmed
via `Composite entity adoption: 76/197 entities eligible` in this run's
verbose log (`--log-level=DEBUG --log-module=GRAPHICS:DEBUG`): **121/197
(61%) of this zone's entities are ineligible** (players, per-slot-override
NPCs) — with composite coverage now effectively 14/14 qeynos2 roster races
(100%, confirmed by 0 `preloadModelData:T05` callsite rows in the whole
ledger — every race reaching that gate got a hit), most composite-hit races
still have at least one ineligible member present, so most per-part sets
still get loaded — just once, on demand, instead of twice (once eagerly via
T05, once more never-used-but-still-charged) as before. **The mechanism is
demonstrably correct** (composite-hit races' per-part textures are decoded
exactly 0 or 1 times now, never on top of the sheet) but **this specific
zone population's savings are modest** because P08d's eligibility gate is
strict and this population's ineligible fraction is high. P13's own ranked
fix list already flagged this dynamic ("savings scale directly with
`getCompositeSheetHits()`... as sheet coverage grows, this fix's payoff
grows with it") — this unit's data confirms the inverse is also true: as
INeligible-entity share grows, the ceiling on this fix's savings shrinks,
independent of composite sheet coverage.

**Categorization is deliberately NOT `entity_perpart` for the on-demand
loads.** Tried tagging `RaceModelLoader::meshBuilder_` with
`setUploadCategory("entity_perpart")` so the deferred-then-loaded textures
would show up in that bucket; reverted (see Deviations) because `meshBuilder_`
is shared with the PRE-EXISTING equipment-variant-override lazy load
(`bodyTextureVariant>0`), which has always used this same call path,
unconditionally, composite-independent — tagging it would have reclassified
those too, making `entity_perpart`'s count look like it *increased* (268→323
in the reverted attempt) despite the fix genuinely working. Leaving
`meshBuilder_`'s category at its P13-era default keeps `entity_perpart` an
apples-to-apples comparison against P13's own numbers. The genuine on-demand
per-part loads are verifiable in the ledger by name pattern (grep for
`{race}{part}00\d\d\.bmp` with `callsite=loadTextureFromBMP` in the
`uncategorized` rows) — confirmed present for `hum`/`qcf`/`huf`/`ham` (human,
Qeynos-citizen) races in Run A.

### Visual safety (design point 5)

`benchmarks/contact_sheets/p13b_perpart/` (`index.html` + 4 PNGs):

- **Eligible composite entities**: `gles2_zone_benchmark --zone qeynos2
  --composite-scenegraph --orbit-radius 15` (the P08d-established harness for
  apples-to-apples composite quality evidence — static/non-animated, per
  P08d's own precedent after its `--animate` garbling finding), 2 poses
  (f20, f30). Both render correctly: full detail, no missing/gray material,
  no tile-boundary artifacts.
- **Ineligible: player character**: Testchar (human, race 1 — one of the
  races this run deferred and later loaded on demand — see the "composite
  sheet hit for race=1" log line) in a real qeynos2 client session,
  third-person, fully textured (shirt/vest, pants, boots all correct).
- **Ineligible: equipment-wearing NPC**: `Guard_Erabb000` (qeynos2, full
  armor + helm — per-slot equipment override makes it ineligible per the
  P08d gate), real client session, renders correctly.

No visual regression in either eligibility class.

### Side-quest: 71-eviction repro (bounded, 2 experiments, ledger on)

**Neither experiment reproduced evictions.** Both recorded with the ledger on
per the plan's instruction; not chased further.

- **(a) Multi-zone, one process (befallen → qeynos2, no teardown).** Server
  started with `befallen` as the zone process; Testchar positioned at
  befallen's safe spot; connected, waited for zone-in, then `#zone qeynos2`
  (GM teleport-zone, reachable via `/say` for a GM-status account, same
  mechanism H03 already established for `#npctypespawn`). Befallen's own
  zone-in: **351 uploads, 0 evictions** (ledger dumped and confirmed —
  `run=befallen` header). The zone transition to qeynos2 itself then hit a
  **pre-existing, unrelated crash**: `[FATAL] [GRAPHICS] loadZoneGraphics
  OnThread: snapshot has empty zone name — aborting`, SIGSEGV — before
  qeynos2's own zone-in-complete marker could fire a second ledger dump, so
  this experiment could not observe qeynos2's cumulative (post-befallen)
  eviction count. Not investigated further (out of this bounded side-quest's
  scope per the plan's explicit "do NOT chase fixes beyond recording") —
  flagged as a real, reproducible bug in the `#zone`-GM-command /
  zone-transition-snapshot path for whoever picks up eviction reproduction
  next; it blocks exactly the multi-zone repro shape P13's own Deviations
  section flagged as the most promising lead ("evictions accumulating across
  MULTIPLE zone visits in one process lifetime").
- **(b) P10b's exact session shape.** Temporary preset
  `orangepi_p13b_sidequest_b` (identical to shipped `orangepi` plus
  `mergeZoneDraws: true`, matching P10b's own review: "`--constrained`
  presets mirroring orangepi with `mergeZoneDraws:true`"), single qeynos2
  zone-in, same script as the main acceptance runs. **555 uploads, 0
  evictions.** Added to `config/constrained_presets.json` for this run only,
  reverted immediately after (confirmed via `git status`).

**Verdict**: still not reproduced, now under FOUR independent measured
configurations across two units (P13's Run A/B, this unit's (a)/(b)) — all
single-zone-in-and-settle qeynos2/befallen sessions land comfortably under
the 64MB budget regardless of composite state, `mergeZoneDraws`, or which of
two custom-vs-shipped presets is used. Experiment (a)'s crash is the most
informative new data point: it's the first concrete evidence of a
**mechanical blocker to the multi-zone-accumulation hypothesis** in this
environment, not just an untried idea — the next attempt at this repro needs
`loadZoneGraphicsOnThread`'s empty-zone-name crash fixed (or a different
zone-transition mechanism than `#zone`, e.g. a real client-walked zone line)
before it can even attempt to observe cumulative eviction behavior.
`refactor/deferred.md` updated (see Deviations) to carry this forward.

### Verification

1. `cmake --build build -j24`: clean, 0 errors.
2. `ctest --output-on-failure` (build/): 1233/1235 passed — the exact two
   documented host exceptions (`AudioManagerTest.PreloadCommonSounds`,
   `RDPServerFrameTest.UpdateFrameMultipleTimes`), re-confirmed after the
   `ensureModelDataLoaded` refactor (ran ctest twice, once before and once
   after that fix — both clean modulo the same two).
3. `scripts/build-gles2-x86.sh`: clean (pre-existing `-Wformat-truncation`
   warnings only, same as documented in P08d/P13).
4. `scripts/ci/visual_gate.sh`: `=== VISUAL GATE: PASS ===`, goldens
   untouched (`git status --porcelain` shows no golden/benchmark diffs from
   this — the gate renders no entities, unaffected by this unit's changes by
   construction).
5. `scripts/build-arm-noble.sh`: clean cross-compile, `build-arm-noble/bin/
   willeq` produced, re-run after the `ensureModelDataLoaded` refactor.
6. `scripts/refactor/check_file_sizes.sh --files <exact changed set>`
   (absolute paths, per the same environment constraint P08d/P13 documented
   — no `cd`, cwd resets between Bash calls): `race_model_loader.cpp` 967
   (ok), `race_model_loader.h` 498 (ok), `animated_mesh_creation.cpp` 635
   (ok), `model_loading.cpp` 1695 (WARN, > 1500) — `model_loading.cpp` was
   already WARN before this unit (1677 lines, per P08d's own review of the
   same file); this unit added 18 lines (the `deferredPerPartTextureNames_`
   skip in `releaseRawTextureData()`), no new FAIL anywhere.
7. Sanctioned server flow throughout (`scripts/eqemu/up.sh`/`seed.sh`/
   `down.sh`), Testchar position restored to its documented original
   (`zone_id=36, x=-77.9232, y=21.9086, z=3, heading=190`, confirmed via
   `SELECT` after the last run), server brought down cleanly
   (`docker ps -a` shows no akk-stack containers after the final `down.sh`).

### File hashes (final state)

```
ef70ad3939ff18c4333109665efb7e37ada94c31f7c345ddfdb4c8d892e41bc6  src/client/graphics/eq/race_model_loader.cpp
a98ad3313b71488b8385860d86fe93e00c47d8eb1ca4865c3e1462f2e5a7eb83  include/client/graphics/eq/race_model_loader.h
fb799fbf1fad3d554be6ab7f222ed647bf7b905ac1faacc52c1e2933342344aa  src/client/graphics/eq/model_loading.cpp
c58864fd0d15f6b036340fde3c2fad34cc36ac484825edc92ed3d317eb518dfb  src/client/graphics/eq/animated_mesh_creation.cpp
```

### Deviations from the plan

1. **`meshBuilder_->setUploadCategory("entity_perpart")` tried, then
   reverted** — see "Categorization is deliberately NOT `entity_perpart`"
   above. Kept the ledger comparison honest rather than making the headline
   number look better by relabeling pre-existing uncategorized uploads.
2. **A second production bug found and fixed beyond the plan's literal
   design section**: `getAnimatedMeshForRace`'s wasteful `getMeshForRace()`
   call (→ `buildTexturedMesh()`'s eager per-part decode as an unused side
   effect) was silently defeating the T05 deferral for EVERY entity
   (eligible or not), not just failing to help ineligible ones. Found
   entirely from the ledger acceptance run's own numbers looking wrong (see
   above), fixed via the `ensureModelDataLoaded()` extraction
   (`race_model_loader.h`/`.cpp`, `animated_mesh_creation.cpp`). Necessary
   for the fix to have any real effect at all — not optional, not deferred.
   Minor, disclosed behavior nuance: `getMeshForRace`'s old null-check also
   implicitly required `modelData->combinedGeometry` to be non-null (an
   unrelated field animated-mesh building doesn't use);
   `ensureModelDataLoaded()` only requires the model to exist in
   `loadedModels_`, so a hypothetical race with `combinedGeometry==nullptr`
   but a valid `rawGeometry` would now proceed further into
   `getAnimatedMeshForRace` than before (which itself still separately
   guards on `rawGeometry`/`skeleton` downstream) — not observed to matter
   in any measured run, flagged for completeness.
3. **`deferredPerPartTextureNames_` (design point 1's "archive/name list"
   metadata) turned out to be load-bearing, not just bookkeeping** — see
   "A correctness hazard found and closed" above (`releaseTextureDataAfter
   Upload`). Without it, this fix would have shipped a real regression on
   the Pi tier (raw texture data for a deferred race silently gone by the
   time an ineligible entity of that race needed it, later in the same
   session or a later zone visit) that would not have shown up in any of
   this unit's own x86 measurement runs (`releaseTextureDataAfterUpload`
   only engages when `totalMemoryBudgetBytes>0`, which the hardcoded
   `OrangePi` C++ preset sets but this unit's x86 measurement runs never
   exercised the RELEASE point within the ~60s single-zone-in-and-settle
   script — found by reading the code, not by reproducing the failure).
4. **Side-quest experiment (a) hit a new crash** (`loadZoneGraphicsOnThread:
   snapshot has empty zone name`) rather than cleanly completing the
   multi-zone repro — recorded and reported per the plan's instruction, not
   fixed (out of this bounded side-quest's scope).
5. **`data/captures/*.png`** (the raw screenshot/frame captures backing the
   contact sheet) are gitignored, per H03's established convention; only the
   copies under `benchmarks/contact_sheets/p13b_perpart/` are committed.
6. Config-file temp presets (`orangepi_p13b_nocomposites`,
   `orangepi_p13b_sidequest_b`) were each added to
   `config/constrained_presets.json` for exactly one run and reverted via
   `git checkout --` immediately after — confirmed clean via `git status`
   before moving to the next step, same pattern P13 established for its own
   Run B.
