# B1096 (#416): pre-zone EQE1 resolution — the player model is built before the packs are open

Class A, live defect. Every ETC1 run of the fable tip dies at zone-in to
qeynos2 with `Texture 'daffa0001.bmp' not found in any EQE1 flat pack` (Will's
XU4 play test 2026-10-19, `xu4:~/willeq/logs/play-20261019T01*.log`, the last at
`--log-level=DEBUG`).

## The call order, from "Loading characters" to the FATAL

The player's race model (DAF, face variant 1) is built in the PRE-ZONE phase —
the render thread draws the character before the zone-load thread has run its
EQE1 step. Two independent pre-zone builders reach the same FATAL:

1. **The paperdoll** (`CharacterModelView`) is rebuilt by the render thread the
   frame after the player entity exists, on the loading screen:
   - `OverlaysComponent::updatePaperdollForFrame()` — `characterModelView_->setCharacter(...)`
   - `CharacterModelView::setCharacter` — `raceModelLoader_->createAnimatedNodeWithEquipment(...)`
   - `RaceModelLoader::createAnimatedNodeWithEquipment` → `getAnimatedMeshWithAppearance(...)`
   - `buildMeshFromGeometry` → `ZoneMeshBuilder::loadTextureFromBMP(name, texInfo->data, ...)`
   - `ConstrainedTextureCache::getOrLoad(name, data, ...)`

2. **The in-zone player model** is built on the loading thread, but that thread
   runs the zone steps in order — and the FATAL is reached from the *render*
   thread (path 1) first, before Step0 runs:
   - `Application::loadZoneGraphicsOnThread` → `m_renderer->loadZoneSequential(...)`
   - `ZoneLoadComponent::loadZoneSequential`
   - `loadZoneStep0_EQE1(ctx)` ← opens the zone packs **and** registers the archive resolver
   - `loadZoneStep8_Entities(ctx)` ← where the in-zone player model is prepared

**Step0 runs on the loading thread; the paperdoll (path 1) runs on the render
thread and is already building the model before Step0 returns.** The DEBUG log
confirms it: exactly one `EQE1Loader: mmap'd` (willeq.eqe1, from startup) and no
`SEQ Step0` line before the FATAL.

## The FATAL branch

`ConstrainedTextureCache::getOrLoad` (the "EQE1 is configured but texture not
found" branch, `constrained_texture_cache.cpp`):

```
getOrLoad(name, data, ...)
  findCompressedEntry(name, stem)          (constrained_texture_cache_compressed.cpp)
    scan the OPEN loaders ... miss
    if (!eqe1ArchiveResolver_) return nullptr       <-- unset pre-zone
  if (!snapshotEQE1Loaders().empty()) {              <-- willeq.eqe1 IS open
    LOG_FATAL("Texture '{}' not found in any EQE1 flat pack")
    std::exit(1);
  }
```

The resolver (`eqe1ArchiveResolver_`) is registered ONLY in
`loadZoneStep0_EQE1` (`renderer_zone_load.cpp`). Until then, `willeq.eqe1`
open makes `snapshotEQE1Loaders()` non-empty, so any name whose owner is another
pack (daffa0001 → global_chr, globaldaf_chr per the index) hits the
all-or-nothing rule and exits.

### The key correction to the issue's mechanism

The FATAL gate is **`!snapshotEQE1Loaders().empty()`** — *any* loader
open — **not** "the caller's data was empty". B1051's three sites
(`renderer_zone_load_entities.cpp`, `zone_geometry.cpp`,
`variant_loading_preload.cpp`) made the loaders *proceed with empty data* when
`hasEQE1Loaders()` — but that is a red herring for the FATAL itself: keeping the
S3D bytes would not stop the exit, because the branch never reads `data`. What
matters is that the name is **unresolvable** (no open pack holds it and no
resolver is set). This is why the fix is to make the name *resolvable* before
the pre-zone build, not to keep the bytes (the card's "preferred" Option B is
inert for this FATAL — see "Why this option, not keeping the bytes").

## The fix

Install the EQE1 name→archive resolution at startup, in `initLoadingScreen`,
right after willeq.eqe1 opens (U02) and before the pre-zone UI (U03):

- `renderer_init.cpp` — `initializeEQE1EarlyResolution(*this, constrainedTextureCache_)`
  (inside `initLoadingScreen`, after the U02 willeq.eqe1 block, before U03)
- new TU `src/client/graphics/eqe1/eqe1_early_resolution.cpp` — loads
  `data/eqe1_texture_index.json` (once; Step0's `isLoaded()` guard makes its
  re-load a no-op) and registers
  `cache->setEQE1ArchiveResolver(name -> Eqe1().openArchiveContaining(name))`.

With the resolver in place before the pre-zone build, `findCompressedEntry`
(`_compressed.cpp:391`) resolves a miss to the ONE archive that holds the name
and opens it (daffa0001 → global_chr). A genuine asset gap (a name in no
archive) still resolves to nothing and stays fatal under decision #2 — no new
FATAL sites. `loadZoneStep0_EQE1` still re-registers; that stays a no-op.

### Why this option, not "keep the S3D bytes"

The card's preferred Option B (the B1051 condition becomes "this name's archive
is open or resolvable", keeping the bytes otherwise) does not prevent the FATAL:
`getOrLoad`'s all-or-nothing branch at `:622` is reached before `data` is ever
decoded, and it fires on *any* open loader + unresolvable name. Keeping the bytes
only changes which *later* line would warn. Registering the resolver (and letting
the index open the owning pack) is what actually removes the miss. B1051's own
fix is left intact — the per-NPC variant series still resolves from EQE1 once the
zone's packs are open, and now resolves pre-zone too.

## Red / green

- **Red** (`test_pre_zone_eqe1_resolution_order`, source scan): `initLoadingScreen`
  must name `initializeEQE1EarlyResolution()` between the U02 willeq.eqe1 open
  (`getOrOpenEQE1("willeq")`) and the U03 UI renderer
  (`frame_->uiRenderer_ = std::make_unique<UIRenderer>()`). Compiles on the
  parent (no new symbol) and FAILS there — the anchor is absent. Green at the
  head. The scan reads the production source at run time (WILLEQ_SOURCE_DIR
  resolves to the checkout via willeq_paths.txt), so no ctest properties are
  needed — under the suite's PRE_TEST discovery (T163) a configure-time
  `SET_TESTS_PROPERTIES` on a discovered test would fail the configure.
- **Green / behavior** (`test_pre_zone_eqe1_resolution`, two tests):
  - `PreZoneMissWithNoResolverExits` — death test, the exact branch: willeq.eqe1
    open, no resolver, daffa0001 → `getOrLoad` exits(1). (The parent's *behavioral*
    red; it names the new seam so it builds at the head, where it pins the FATAL
    branch is still the behavior a *no-resolver* configuration has.)
  - `EarlyResolutionResolvesANameNoOpenPackHolds` — the SAME single-open-pack
    configuration, but the early resolution installed: the index resolves
    daffa0001 → global_chr, the pack is opened on demand, the name is served
    by it. No exit. It requests the name with and without bytes to show the FATAL
    gate is the open-loader set, never the data.

Both behavioral tests skip when the ETC1 tree is not mounted (asset dependency,
same idiom as `test_eqe1_archive_component.cpp`); on the build host (ThinkPad,
via rbuild) both are present.

## B1051 tests still green

`test_entity_model_sharing` and `test_eqe1_source_archive` are untouched and run
in the full suite; the fix adds a resolver before them, never removes one.

## Live leg on the XU4 (the exact regression scenario, on the published armhf build)

`ci-arm/cc61d067a2f637818ef8f5542878f4ac30697c83` (this branch's head, built by
the MR's `build-arm` job), run through a copy of `~/willeq/play.sh`
(`~/willeq/play-b1096.sh`; the binary staged as `~/willeq/willeq-b1096` with
`setcap`, so Will's `play.sh`/`willeq`/`update.sh`/`willeq.json` are untouched),
logging in to the local akk-stack (10.0.30.23). `willeq.json` names the
character (Testchar), and the FATAL fires at zone-in — no extra flag needed.
Full log: `~/willeq/logs/play-b1096-20261019T064517Z.log` (kept at
`/home/user/kb-evidence/kb-b1096-prezone-eqe1/xu4_play_b1096.log`, 78,308 lines).

Result: **no FATAL** (`grep -c FATAL` = 0), the client zoned in and held the
zone for the full 220 s hold. The exact pre-zone sequence that used to exit:

```
[02:45:19.200] [INFO ] [GRAPHICS] EQE1Loader: mmap'd ...                  <- 1 pack open (willeq, startup)
[02:45:19.522] [DEBUG] [GRAPHICS] EQE1: early name->archive resolution installed (pre-zone); the index answers a miss with at most one open
[02:45:23.466] [INFO ] [MAIN] [LOADING][Phase 4/15] (15%) Loading characters...
...
[02:45:34.509] [DEBUG] [GRAPHICS]   [CONSTRAINED] ETC1 texture 'daffa0001.bmp' uploaded (32x64, 7 mips, 2768 bytes, +alpha)
[02:45:34.959] [INFO ] [GRAPHICS] BoidsManager: Entering zone 'qeynos2' biome=7
```

`daffa0001.bmp` — the name that FATALed at `play-20261019T01*.log` — uploads
from the pack the early resolver opened on demand, in the pre-zone player-model
window (`Sequential: preload race=6 gender=1` at line 7595 = DAF male, then
`RaceModelLoader: promoted preloaded model key=1537 to main cache`, then the
upload at line 7603, all before `Entering zone 'qeynos2'` at line 8117).
`EQE1Loader: mmap'd` count: 1 before the model build (the startup willeq.eqe1,
at 02:45:19), 8 total by zone-entry — the extra opens are the index-driven
per-name archive opens the resolver performs, not a bulk pre-open of every
pack. (The driver's `EQE1Loader: mmap'd` line prints its format string
unexpanded — `'%s' (v%u, ...)` — a pre-existing `common/logging.h` issue,
follow-up below.)

## No new FATAL sites, no budget/golden/preset/third_party change

`git grep 'LOG_FATAL' -- <new files>` is empty for the new TU; the only behavioral
change is registering a resolver earlier. `tests/goldens/*`, any `manifest.json`,
baselines, ratchets, `config/constrained_presets.json` and `third_party/` are
untouched (empty grep).

## Follow-up (outside this unit's scope)

`EQE1Loader: mmap'd '%s' (v%u, %d textures, %d alpha, %d raw, %zu bytes)`
(`eqe1_loader.cpp:269`) prints its format string UNEXPANDED in the client's
logs — the `LOG_INFO(MOD_GRAPHICS, ...)` path is not consuming the va-args — so
the pack name/version is never visible. Cosmetic (the numbers are useless, the
line still proves an open happened), pre-existing, and affects every `EQE1_LOG_*`
site in that file; a unit against `common/logging.h` should pin a
format-args round-trip test.
