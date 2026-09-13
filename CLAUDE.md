# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Additional context is in subdirectory CLAUDE.md files:
- `src/client/graphics/CLAUDE.md` - Rendering, shaders, GLES2, zone loading, performance
- `include/client/graphics/ui/CLAUDE.md` - Static UI panels, key bindings, slash commands
- `include/client/state/CLAUDE.md` - Game state modules, EventBus, 101 event types
- `include/client/bridge/CLAUDE.md` - Event/intent bridge, thread-safe game↔renderer communication
- `src/client/input/CLAUDE.md` - Input handlers, hotkey manager, command processor, action dispatch
- `src/client/audio/CLAUDE.md` - Audio system, sound categories, audio tests
- `docker/CLAUDE.md` - Cross-compilation, DRM/KMS device, GLES2 driver
- `docs/CLAUDE.md` - Coordinate systems, heading conventions, docs layout

## Orientation (read before large tasks)

- `docs/decisions.md` — standing architecture decisions. Settled; do not re-litigate.
- `docs/module_map.md` — functional map of every split translation unit (EverQuest, Application, CommandProcessor, InventoryManager, IrrlichtRenderer, EntityRenderer, WLDLoader, SimulationWorker, ZoneShader, tests/cmake). Consult it to find the right TU instead of scanning.
- `refactor/BOARD.md` — **the open-work queue of record. Start here.** One entry per open item, each confirmed against the tree at the tip named in its header, classed A (live defect) / B (unreached or hygiene) / C (needs Will's decision) / P (process). Nothing else is a work queue.
- `refactor/K_component_extraction.md` — the open structural programme (component extraction from `class EverQuest`, then `class IrrlichtRenderer`). The active unit plans are `refactor/K<nn>_*.md`.
- `refactor/completed/` and most of `docs/reference/` are historical snapshots (bannered); do not trust their findings as current. That includes `deferred_archive_20260903.md`, `progress_archive_20260903.md` and `testing_progress_archive_20260903.md` — the three trackers `BOARD.md` replaced (K00, 2026-09-03). They are research records; many entries were closed by later units without being struck.
- Workspace: canonical checkout at `/mnt/projects/fable/main` (branch `fable`; symlink from `/mnt/projects/willeq-fable`), executor worktree slots at `/mnt/projects/fable/wt/slot1..3` plus `t1`, `t2` (testing-session slots). Task branches: `task/<UNIT>-<name>`, e.g. `task/K01-guild-component`. The `renderer` branch is reference-only — never merge it.

**Reading order for a new agent:** `refactor/BOARD.md` → `docs/module_map.md` →
`docs/decisions.md` → the unit plan you were dispatched with.

## Process requirement

- Every unit of work is PLANNED by the orchestrator (a `refactor/<UNIT>_*.md`
  plan with scope, file territory, STOP conditions and a verification bar)
  and EXECUTED by executor agents in worktree slots under
  `refactor/EXECUTOR_PROTOCOL.md`. Independent legs are dispatched in
  parallel slots whenever their file territories do not overlap; the
  orchestrator serializes merges onto `fable` (`docs/decisions.md`,
  2026-07-22 workspace layout). Do not execute unplanned work inline in
  `main`.
- **Merge step (T337, 2026-09-04): before merging a branch, run the
  order/coverage golden tests for every TU the branch touched** (the G
  batch pinned `zone_dispatch`, `bridge_component_intents`,
  `command_processor_registration`, `application`, `renderer_frame_render`,
  `renderer_player_physics` with goldens). A branch cut before a golden's
  unit merged is green on its own and fails on the tip.
- **Batches J, K, L and G are complete; every file under `src/`, `include/`
  and `tests/` is under 1,500 lines and ctest enforces it.** The last two
  exceptions there are gone —
  `include/client/eq.h` fell to 899 lines and
  `include/client/graphics/irrlicht_renderer.h` to 1,343, and T448 took the
  renderer facade the rest of the way to **1,191** (ten zero-caller
  declarations deleted, every multi-line inline body moved to the TU that owns
  the state, and the Batch-L narrative moved into the component headers) — so
  `scripts/refactor/file_size_allowlist.txt` is EMPTY and a file over 1,500
  under `src/`, `include/`, `tests/`, `tools/` or `benchmarks/` is a **FAIL**
  in the `FileSizeCeiling` ctest test, not a warning (M00,
  `docs/decisions.md` 2026-09-04; T385 added the last two directories, board
  B167 — `--all` had never scanned `tools/`, and six files there were over the
  ceiling while the checker reported a clean tree). **The tree is clean and
  `FileSizeCeiling` is GREEN** (T453 re-measured: `check_file_sizes.sh --all`
  reports 1,196 files, 0 warn, 0 fail; 1,181 at T451, 1,162 at T447, 1,129 at
  T444). T447 split the last
  three tools riding the ceiling — `willeq_db_builder`, `wld_dump` and
  `gles2_shader_perpixel_benchmark` — see `docs/module_map.md`'s tools table.
  T451 split the next four riding it: `s3d_loader.cpp` (1,336) into five TUs,
  `animated_mesh_scene_node.cpp` (1,287) into four plus a seam header,
  `tools/zone_line_editor.cpp` (1,303) into four plus a seam header, and
  `include/client/state/event_bus.h` (1,269) into the `GameEventType` enum
  (`include/client/state/event_types.h`, which is what `DocCounts` and
  `ReachabilitySweep` now read), five `event_payloads_*.h` family headers and
  an umbrella `event_bus.h` that includes them all — so no include site
  changed. All thirteen resulting TUs are under 550 lines.
  **T453 emptied the band above 1,200 entirely**: the five TUs that were over
  it — `src/client/graphics/door_manager.cpp` (1,365, into four),
  `tools/hwcaps/gles2_fog_volume_benchmark.cpp` (1,220, into four plus a seam
  header), `src/client/graphics/detail/detail_manager.cpp` (1,219, into
  three), the action-dispatcher test TU (1,217, into three sources on one
  target plus `tests/action_dispatcher_test_fixture.h`, all 63 case names
  preserved) and
  `tools/test_gl_points.cpp` (1,206, into three plus a seam header) — are now
  20 TUs, the largest 772 lines, and the largest file anywhere under `src/`,
  `include/`, `tests/`, `tools/` or `benchmarks/` is 1,193. T387 finished what T385 started and
  board **B185** is struck: `tools/hwcaps/zone_bench_loader.cpp` is 977 lines
  and `tools/hwcaps/gles2_zone_benchmark.cpp` 992, not the 3,280 and 3,060
  earlier revisions of this paragraph reported. Put
  substantial new code in new TUs; a file that genuinely must exceed the
  ceiling needs an allowlist entry with a reason and a no-grow max. Close-outs:
  `refactor/J_file_size_reclamation.md`, `refactor/K_component_extraction.md`,
  `refactor/L_renderer_component_extraction.md`,
  `refactor/G_oversize_methods.md`.

## Project Overview

WillEQ is a standalone EverQuest client targeting the Titanium client version with optional 3D graphics rendering. It was migrated from the akk-stack headless client to remove all eqemu dependencies. All opcodes are hard-coded for Titanium specifically.

## Build Commands

**IMPORTANT: `-j$(nproc)` causes problems when executed directly in a Bash tool command (the shell expansion doesn't work reliably in Claude Code interactive sessions). Use a hardcoded value instead, e.g. `cmake --build build -j24` for this 24-core host. If building from a script (e.g. `scripts/build-arm-noble.sh`), `$(nproc)` works fine.**

**Desktop-GL Irrlicht support has been removed (P30,
`refactor/desktop_gl_elimination_plan.md`).** `EQT_GRAPHICS=ON` (the default)
now requires a locally-built patched GLES2 Irrlicht — there is no more
system-`libirrlicht-dev`/X11-GLX fallback. Configuring without one fails
early at `cmake` time with a message naming the bootstrap command below (not
a build/link-time failure).

**One-time bootstrap** (only needed once per checkout, or after the patched
Irrlicht is deleted — `third_party/irrlicht-gles2-x86/` is gitignored):

```bash
bash scripts/build-gles2-x86.sh --irrlicht-only
```

```bash
# Configure (from project root) — the GLES2-config tree is now the primary
# dev/test tree (tests on, full ctest suite). Run it; since T392 (2026-09-05,
# board B215) the suite has NO expected failures — the long-standing
# "ALSA quirk" waiver on AudioManagerTest.PreloadCommonSounds was a
# misdiagnosis and the test is green. A red test is a real red.
mkdir -p build && cmake -S . -B build \
    -DEQT_GRAPHICS=ON \
    -DEQT_GLES2=ON \
    -DEQT_DRM=OFF \
    -DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86 \
    -DEQT_BUILD_TESTS=ON

# Build (no -j flag!)
cmake --build build

# Run tests
cd build && ctest --output-on-failure

# Live/integration tests (need a running eqemu server + display + EQ client
# files) are NOT in the default suite. Register them, then select by label:
#   cmake -S . -B build -DEQT_LIVE_TESTS=ON
#   ctest --test-dir build -L live
# The binaries build either way; only ctest registration is gated (T164).

# Run a single test
cd build && ctest -R TestName --output-on-failure
# Or directly:
./build/bin/test_packet

# Build output
./build/bin/willeq
```

This is the same patched Irrlicht / EGL+GLESv2 (surfaceless llvmpipe on this
dev VM, native GLES2 on the Orange Pi) backend as `scripts/build-gles2-x86.sh`
and `scripts/build-arm-noble.sh` use — see "Orange Pi (ARM)
Cross-Compilation" below. `scripts/build-gles2-x86.sh` (no args) additionally
produces a second, smaller tree (`build-gles2/`, tests off) with just
`willeq` + `gles2_zone_benchmark` — that is what `scripts/ci/visual_gate.sh`
targets; keep using it for the visual gate, it is unaffected by the above.

## Running

```bash
# With graphics (default: surfaceless EGL, no display required — same
# llvmpipe backend the GLES2 tree's tests and visual gate use)
./build/bin/willeq -c willeq.json

# With graphics, X11-windowed (P25; opt-in only, never DISPLAY-autodetected —
# see "Orange Pi (ARM) Cross-Compilation" below)
./build/bin/willeq -c willeq.json --egl-x11

# Headless (no graphics)
./build/bin/willeq -c willeq.json --no-graphics

# With VNC (for headless servers)
./scripts/start-with-vnc.sh -c willeq.json
# Connect via: vnc://localhost:5950 (VNC_PORT in the script; its header comment
# still says 5999 — the variable is authoritative, T215)

# With VNC + Audio streaming (for headless servers with sound)
./scripts/start-with-vnc-audio.sh -c willeq.json
# Connect VNC: vnc://localhost:5910 (VNC_PORT=5910 in start-with-vnc-audio.sh)
# Connect Audio: vlc http://localhost:8085 (AUDIO_PORT=8085)

# With native RDP (alternative to VNC)
./build/bin/willeq -c willeq.json --rdp
# Connect via: mstsc.exe /v:hostname:3389 (Windows)
# Or: xfreerdp /v:hostname:3389 /cert:ignore (Linux)

# RDP with custom port
./build/bin/willeq -c willeq.json --rdp --rdp-port 13389
```

### Where the client's shipped data comes from

**The client no longer has to be started from the repo root** (T399, board
B235). Every shipped data file it reads — `config/*.json`, `data/willeq.db`,
`data/textures/*.bmp`, `data/zone_lines/*.json`, `data/item_models.json`,
`data/eqe1_texture_index.json` and the rest — goes through one resolution,
`eqt::ResolveDataPath()` (`include/client/data_paths.h`). Search order, first
existing file wins:

1. the path itself, if it is absolute (never rewritten);
2. **the path as given, i.e. relative to the current working directory** —
   deliberately first, so a run from the repo root reads exactly the bytes it
   read before this existed (the visual gate included);
3. `$WILLEQ_DATA_ROOT/<relative>`, for a staged install whose data tree is not
   beside the binary;
4. the directory holding the running executable, then up to three parents —
   which finds `<repo>/config/...` from `<repo>/build/bin/willeq`,
   `<repo>/build-gles2/bin/willeq` and `<repo>/build-arm-noble/bin/willeq`
   alike.

If nothing exists the input comes back unchanged, so the caller's own "could
not open X" names the X it asked for. `willeq.json` itself is resolved the
same way. **A new shipped-data load must call `ResolveDataPath()`** —
`tests/test_data_path_routing.cpp` re-takes the census on every ctest run and
fails on a bare `"config/…"` / `"data/…"` literal that does not.

**Generated caches follow the opposite rule: they are written OUTSIDE the
checkout** (T402, board B248). `snd_index_cache.json` (the sound-archive
index) and `gfx_index_cache.json` (the character-archive index) used to be
git-tracked files under `config/` that the client rewrote on every run, so a
plain `willeq` run left the working tree dirty and a careless `git add -A`
committed one machine's absolute EQ path. They now resolve through
`eqt::ResolveCachePathForRead()` / `ResolveCachePathForWrite()`
(`include/client/data_paths.h`), which pick the first writable of
`$WILLEQ_CACHE_DIR`, `$WILLEQ_DATA_ROOT/cache`, `<exe dir>/cache` (i.e.
`build/bin/cache`) and `cache/`. A miss is not an error — it costs a rescan,
measured at ~0.4 s for the 17 sound archives and ~30 ms for the 378
character archives. A new generated cache uses these, never `ResolveDataPath()`.

### The shipped data has build recipes — use them, do not hand-edit

Three shipped assets are **derived**, not hand-carried, and each has a ctest
test that fails if the checked-in bytes and their inputs drift apart:

| asset | recipe | pinned by | ctest |
|---|---|---|---|
| `data/textures/*.bmp` (67 files) | `scripts/build_textures.sh` → `tools/generate_textures` (64, procedural), **`tools/spell_sprite_atlas_builder` (1, T417) and `tools/item_icon_atlas_builder` (2, T430) — both lifted from the client's own art** | `data/textures.sha256` | `TexturesReproducible` |
| `/mnt/projects/EverQuest_ETC1/willeq.eqe1` (132 entries since T430: 67 textures, 64 ETC1 `_alpha` companions, 1 raw — `build/bin/verify_eqe1` prints the census; re-measured T444) | `scripts/build_willeq_pack.sh` | `data/willeq.eqe1.sha256` | `PackReproducible` |
| `data/willeq.db` (29,791 rows in eleven tables; `metadata.version` = 7) | `scripts/build_willeq_db.sh` → `tools/willeq_db_builder`, over the EQ client's seven `*.txt` files plus `data/item_models.json`, `data/equipment_models.json` and `config/race_models.json` | `data/willeq.db.sha256` | `DbReproducible` (T450) |

**Every table the builder can create now ships** (T452, board B468 CLOSED).
There used to be a twelfth, `s3d_assets` — a name→archive index over every
S3D, built only when `willeq_db_builder` was given `--s3d-index`, which no
script, CMake rule or test has ever passed. So it was never in the shipped
file, and the two client queries against it
(`WilleqDatabase::getAssetLocation()` / `getAssetsInArchive()`) failed at
`prepare()` and returned "not found" from a MISSING TABLE rather than from a
miss — for six months, on a reachable path. The loader, the flag,
`tools/s3d_index_builder` and both queries are gone.
`WilleqDatabaseShippedContent.EveryTableTheClientQueriesExists` (ctest) now
takes the census from `src/client/willeq_database.cpp` itself and fails on a
query naming a table the shipped database does not have, so this class of
drift cannot recur silently.

**T454 (board B474) closed the other half of that finding, and added the
CONVERSE net.** `equipment_textures` — 2,623 rows naming which of the 17
`*equip*.s3d` archives holds each held-item texture — shipped for six months
with no reader anywhere in the client. Unlike `s3d_assets` this was not a
broken route but a REDUNDANT one: the equipment texture path resolves live,
through `EquipmentModelLoader::buildEquipmentIndex()`, which indexes those same
17 archives into `textureIndex_` at zone load, and `getTextureRef()` is what
the mesh build asks. The writer and the table are gone and the database was
rebuilt and re-pinned under T450's recipe (32,414 rows → **29,791**, twelve
tables → eleven; `metadata.input_digest` did not move, because the inputs did
not). `WilleqDatabaseShippedContent.EveryTableTheDatabaseShipsIsOneTheClientQueries`
now fails on a shipped table that no client SQL names. It carries exactly one
exemption, `equipment_models` (1,000 rows), which is dead by the same
measurement and boarded as **B476** rather than dropped as a side effect —
B468's lesson is that "nothing reads it" and "nothing CAN read it" look
identical from a grep, so each drop gets its own deliberate check.

**The database's pin is T450's (board B467), and it could not have existed
before.** `willeq_db_builder` stamped `metadata.build_date = __DATE__ " "
__TIME__`, so **recompiling the tool changed `data/willeq.db` with
byte-identical inputs** — which is why T447's split proof had to normalise one
row instead of comparing sha256s, and why the database was the one derived
asset nothing checked. That row is now `metadata.input_digest`: a digest of
the inputs themselves, mixed from each input's ROLE (never its path, so every
worktree agrees) plus its contents. Nothing has ever read the stamp — the
client reads only `metadata.version` — so no consumer moved. Rebuild with
`scripts/build_willeq_db.sh` and commit `data/willeq.db` together with its new
`data/willeq.db.sha256` line, exactly like the pack. **Do not bump
`kWilleqDbExpectedVersion` to ship a data change**: a version bump invalidates
every build tree in the workspace (T304/T355), and a row edit is not a schema
change.

The pack's inputs are the 67 BMPs plus `tools/ui_atlas_builder`, so a texture
change cascades: regenerate the BMPs, rewrite `data/textures.sha256`
(`--write-pins`), rebuild the pack, update `data/willeq.eqe1.sha256` — all in
**one commit**, or both tests go red.

**`data/textures/` has THREE recipes since T430 (boards B300, B376).**
Sixty-four of the BMPs are procedural (`generate_textures`); the other three
are the official client's own art. `spell_sprite_atlas.bmp` comes from
`SpellEffects/spel*.tga` via `tools/spell_sprite_atlas_builder`, and
`item_icons0.bmp` + `item_icons1.bmp` come from
`uifiles/default/dragitem1..34.tga` via `tools/item_icon_atlas_builder`.
`build_textures.sh` runs all three tools into the same directory, so a
`data/textures/` change may need any of them, and the pin file covers the
union.

**The item icon sheets (T430, board B376 CLOSED).** Before T430 fable had no
item art at all: every panel that showed an item carried the server's icon id
(S2 put it on `InventorySlotChangedData`) and drew a placeholder — the
inventory popup and the tradeskill grid painted the first three characters of
the item's NAME, and T421's item display window painted the icon NUMBER inside
`IDW_IconButton`'s frame. The layout lives in ONE place,
`include/client/graphics/ui/item_icon_atlas.h`, which the builder and the
client's `ItemIconSheets` loader both include. Two facts there are QUOTED from
the oracle rather than assumed, and both matter:
- **34 sheets of 6x6 40x40 cells, ids 500..1723** — `EQUI_Animations.xml`'s
  single `<Ui2DAnimation item="A_DragItem">` lists exactly `dragitem1.tga` ..
  `dragitem34.tga` with `<CellWidth>40</CellWidth>` on a `<Size>256x256</Size>`
  texture.
- **COLUMN-major within a sheet**, because that animation carries
  `<Vertical>true</Vertical>`. The usual `(id-500)%36 / 6 = row` folklore is
  the wrong way round, and `dragitem34` — the only partly-filled sheet —
  proves it: its thirteen icons fill two whole COLUMNS plus one, not two rows.
  `tests/test_item_icon_atlas.cpp` asserts both the positive and the negative
  (the row-major cell holds different art), so a "cleanup" back to row-major
  is a red test rather than 1224 wrong pictures.

1200 of the 1224 cells carry art: `dragitem34` stops after thirteen, and
`dragitem21` has a transparent cell in the MIDDLE of its grid (icon id
**1230**). An empty cell is still addressable and simply draws nothing. The
sheets load EAGERLY, beside `ui_atlas` in `UIAtlas::load()` — T430 wrote them
lazily first and MEASURED the consequence: `ConstrainedTextureCache::getOrLoad()`
logs FATAL and `std::exit(1)`s on a pack miss, so first-use loading turns a
startup failure into a mid-SESSION exit the moment a player opens an inventory
(the reason is written out in `include/client/graphics/ui/item_icon_sheets.h`;
an earlier revision of this paragraph said LAZILY and was wrong — T444). A
`lookup()` miss makes `UIRenderer::drawItemIcon()` return false,
which is why every caller kept its pre-T430 text placeholder as the fallback.
The spell sprite atlas's layout —
which sprite's which animation frame occupies which of the 32x32 cells — lives
in ONE place,
`include/client/graphics/environment/spell_sprite_atlas.h`, which the builder
and `SpellEffectsConfig` both include, so the sheet and the client's
addressing of it cannot drift. A particle's `textureIndex` below
`SpellSpriteAtlas::kBase` (64) is a procedural atlas tile; at or above it, a
**sprite ordinal, not a cell** — because `EmitterConfig::textureRegions[]` is a
`uint8_t[4]` and 528 cells do not fit in one. **Those sheets animate**, and
since T420 (board B355 closed) all sixteen frames of all 33 sprites ship
(1024x1024, 32x32 cells, 528 used): the CELL is derived per particle per frame
in `UnifiedParticleRenderer::render()` from
`SpellSpriteAtlas::frameForNormalizedAge(p.getNormalizedAge())`, so the sheet
plays through exactly once over a particle's lifetime — frame 0 at birth, the
last frame as it dies. These sheets are fade-in/peak/fade-out cycles, so that
mapping is the animation; a fixed cells-per-second clock would strand a
short-lived particle in the faint fade-in frames. No shader change was needed
— the atlas offset was already a CPU-computed vertex attribute. The playback
RATE is therefore `16 / lifetime`, set by `config/spell_effects.json`. **T422
settled that lifetime against the official client (boards B226/B356/B370, all
closed): `impact` and `spellComplete` now run 1.6-2.0 s, i.e. 8.0-10.0
cells/s, where they used to run 0.4-0.8 s and 20-40 cells/s.** The lifetime
is measured -- the oracle's particle population rises for 1.46-1.96 s with no
deaths at all -- but the cells/s figure is DERIVED from it, because at the
official client's measured 4.4-5.8 renders/s a direct animation-rate
measurement is below Nyquist. Do not quote 8-10 cells/s as a measurement, and
do not re-derive T420's "below ~3 cells/s" bound: it came from a statistic
over a mixed-age particle population and does not survive
(`review/T422/README.md`). Any future edit to those lifetimes moves the
animation rate with them; `tests/test_spell_burst_oracle_match.cpp` fails if it
leaves the measured band.

**A SPELL'S COLOUR IS ITS SPRITE, and there is no colour field anywhere in the
chain** (T441, boards B398 and B397, both closed). `spells_us.txt` field 123
SPELLAFFECTINDEX indexes `spells.eff`; that record's three stage blocks name
sprites; `GEN<L><d>0_SPRITE` resolves 0-based to `SpellEffects/spel<l><d+1>.tga`;
and the particle is that file's texels. Field 85 RESISTTYPE is a mechanic the
official client does not paint with, and T73 had already refuted the only
colour-shaped candidate inside `spells.eff`'s unknown region. So
`SpellPresets::applyStageSpriteNames()` drops the particle colour modulation to
WHITE whenever it installs a real stage's sprites — `POINT_SPRITE_FS` computes
`texColor * vColor`, and any non-white modulation is fable tinting the oracle's
own art. `config/spell_effects.json`'s `resistColors` and
`SpellVisualFX::colorForVocabulary()` survive only for the ~946 spells with no
usable `spells.eff` record, the one case where there is no art to take a colour
from. All three stages route their names now: Source → cast glow and
spellComplete, Middle → projectile trail, Target → impact. `spellOverrides` is
no longer empty either: 93, 41 and 253 carry the populations and (for 253) the
sizes and duration T422 measured live, and a spell whose oracle numbers nobody
has taken still resolves to the plain style — an invented override is the guess
Will's ruling forbids. `tests/test_spell_colour_and_overrides.cpp` pins both
halves, including the negatives (a reintroduced tint, an invented override).

The pack lives outside the repo and is
read live by every worktree; only `build_willeq_pack.sh` may install it (it
writes a temp file in the destination directory and `mv`s it into place).

All but one of those BMPs is produced by
`src/client/graphics/texture_generators.cpp` (plus
`DetailTextureAtlas::createAtlasImage()` for `detail_atlas.bmp`, and T417's
`spell_sprite_atlas.bmp`, which is client art rather than generated pixels). That TU was
in **no CMake target at all** until T400/T403 — which is how the particle
atlas shipped alpha-less for a year (board B225). T406 closed the second
half of that (board B266). T417 and T430 have added three sheets since, so
the census re-measured at T444 is **63 of the 67 shipped 32-bit with their
alpha in-band**, and the four 24-bit ones are the four named below.
 T411 then took the pack side back: the pack's
`--rgba8888-texture` list is **down from 60 entries to one**, because the
three alpha-BLEND shaders
(`SOLID3D_FS`, `UI2D_FS`, the particle point sprite) now sample the ETC1
`_alpha` companion on texture unit 1 — the driver binds it from the unit-0
texture in one place, `COpenGLES2Driver::bindAlphaCompanionUnit()`, and
`ConstrainedTextureCache` records the pairing on the texture itself (boards
B261 and B287, both closed). The
four that stay 24-bit each carry a written reason in
`tests/test_texture_alpha_census.cpp`, whose `EveryShippedBmpIsAccountedFor`
makes a new generator declare which side it is on:
`spell_particle_fallback.bmp` (drawn additively, alpha provably unsampled —
boards **B286**/**B322**; T424 closed B322 by moving its cosine falloff out of
the discarded alpha channel and into RGB, which is what an additive blend
samples — before that it shipped as a solid white square), `detail_atlas.bmp`
(board **B410**: T427 closed **B265** by regenerating it, so **every one of
the 67 shipped BMPs is now byte-reproducible from its generator and
`NOT_REPRODUCIBLE` is empty** — 65 when T427 wrote that, 67 since T430's two
icon sheets, re-measured T444; it stays 24-bit on scope, not on a measured
reason, because both its consumers alpha-BLEND it and converting it is a look
change), and `rain_streaks.bmp` + `snow_flakes.bmp` (no consumer
at all). T417's `spell_sprite_atlas.bmp` is 32-bit and takes the ordinary ETC1
companion route — it needed no `--rgba8888-texture` override. `generate_textures --report` prints the census.

**A 32-bit entry is not free**: with all 60 in-band the pack went 2.83 MB to
19.99 MB and every zone's `eqe1MappedBytes` rose by 17.2 MB, because an ETC1
256x256 entry with mips is 43,704 bytes and its RGBA8888 form is 349,524.
T411 (board B287) took that back by fixing the shaders instead of the payload:
the pack is **9,726,596 bytes** (5,532,254 after T411; T417 added the spell
sprite sheet and its companion, +349,664, and T430 the two item-icon sheets
and their companions, +2,796,480 — the extra over the pre-T406
2.83 MB is the ETC1 companions, which are new, as the old 24-bit BMPs had no
alpha to carry), and `eqe1MappedBytes` fell by 14,462,656 per zone at T411
before rising 349,664 at T417 and 2,796,480 at T430. The size is pinned by
`data/willeq.eqe1.sha256`, which carries the same arithmetic; re-measured
T444. The one entry that keeps
an in-band payload is `ui_atlas`, and not for a single-sampler reason: a 1px
UI `drawRect` minifies the atlas 4:1 and selects companion mip level 2, where
ETC1 decodes the isolated `white` texel as 217/255 and dims the rect by 15%. **A new
`--rgba8888-texture` override is now a ctest FAIL**
(`TextureAlphaCensus.NoPackEntryNeedsTheRgba8888Payload`); if a consumer
genuinely cannot take a second sampler, it needs an entry in that test's
`kInBandAlpha` list naming the consumer and the reason.

A generator must be **bit-identical across optimisation levels** — a shipped
asset whose bytes depend on `CMAKE_BUILD_TYPE` makes `TexturesReproducible` a
coin toss. Two noise hashes overflowed signed `int` (UB) and GCC folded them
differently at -O0 and -O2, moving 157 texels of the particle atlas (board
B268, fixed in T403 by moving them to `uint32_t`). Keep new generator
arithmetic defined.

### Display Options Comparison

| Method | Use Case | Audio | Client |
|--------|----------|-------|--------|
| Surfaceless EGL (default) | No display needed at all — llvmpipe GPU emulation | Local | Native |
| X11-windowed EGL (`--egl-x11`, P25) | Local display, GLES2 in a window | Local | Native |
| Xvfb + VNC | Remote access, cross-platform | None | VNC client |
| Xvfb + VNC + Audio | Remote with sound | HTTP stream | VNC + VLC |
| Native RDP | Windows users, better compression | RDP audio | mstsc.exe, xfreerdp |

RDP and VNC can run simultaneously if both X11 and RDP are enabled.

### The scenario lane — a live verification that exits with a code

**T410 (board B138/B139).** Before this, every live verification on the board
ended with a human reading a session transcript: `main.cpp` returned `0`
unconditionally, so a client that crashed, never logged in, or logged in and
did nothing at all were indistinguishable to a script. The scenario lane makes
the process exit code the result.

```bash
# One scenario, headless, against a running eqemu:
./build/bin/willeq -c config.json --scenario zone_in --timeout 120

# The whole rig -- lock, up.sh, snapshot, run, restore with the world DOWN,
# down.sh, release -- with the client's exit code passed through:
bash scripts/scenario.sh zone_in --timeout 120 --unit T4xx
```

**Exit-code taxonomy** (`include/client/scenario/scenario_runner.h`; nothing
else may be returned from a scenario run):

| code | meaning |
|---|---|
| 0 | pass — the scenario asserted and the assertion held |
| 1 | fail — the scenario ran and its assertion failed |
| 2 | timeout — zoned in, but no verdict inside `--timeout` (**also what an unfinished run reports**, so an external `/quit`, a SIGTERM or a crash can never masquerade as success) |
| 3 | could-not-start — never reached a zone (server down, bad login, unknown scenario name) |

`scripts/scenario.sh` adds its own operator codes on top: 64 lock held by
another session, 65 `up.sh` failed, 66 the server snapshot did not restore.

**How it hangs off the client.** `--scenario` forces Automated mode, and the
runner is registered on `AutomatedMode::setAutomationCallback` — the per-tick
hook that had zero registrants in the whole tree until this (board B139). The
callback runs on the GAME THREAD (`Application::processInput()`), one
`ScenarioRunner::Tick(dt)` per ~60 Hz tick; returning `false` is
`AutomatedMode`'s own contract for "stop", so the client unwinds through
`isQuitRequested()` exactly like `/camp` and never calls `exit()` from inside a
tick.

**The runner owns the pre-flight, not the scenarios.** Until
`IsFullyZonedIn()` is true it does not call `Step()` at all; if that never
happens inside the start deadline the verdict is 3. So "the server was down"
can never be reported as a scenario failure, and every scenario's first
`Step()` may assume a live, zoned-in client.

**Scenarios poll; they do not subscribe.** `GameState::events()` (which
`AutomatedMode` forwards) and the renderer-facing `GameStateBridge` are two
different streams, and the interesting landings — buffs via T348's `OP_Action`
path — go to the bridge, which a headless client does not drain. Polling reads
the same authoritative state either stream was published from.

| scenario | asserts | replaces |
|---|---|---|
| `zone_in` | login → zone → `IsFullyZonedIn()` with a non-empty zone name | every `grep "Zone connection complete"` rig |
| `two_client_survive` | a second player appears AND both hold for `--scenario-hold` seconds (default 300) | `review/T391/scripts/t391_run.sh`'s `kill -0` poll, which printed SURVIVED when the partner never logged in |
| `cast_lands` | T401's prep recipe, then `/cast 1`, then the buff actually reaching `BuffManager` | the hand-read chat panel in T386/T401 |

The partner for `two_client_survive` is a **peer process**, launched by
`scripts/scenario.sh` running `zone_in` on the second account — the client
never forks a second client.

The state machines are pure logic over `IScenarioWorld`
(`include/client/scenario/scenario.h`), so
`tests/test_scenario_state_machines.cpp` runs all twenty cases with no client,
no server and no network. A scenario needing something new adds a method
there, and `ApplicationScenarioWorld` is the only implementation that touches
`EverQuest`. Trace goes to the `MOD_SCENARIO` log module, pinned to INFO by
the lane itself so a `--log-level` cannot silence the verdict.

### Video capture (temporal evidence — attack swings, casting, zoning, particles)

Stills can't show a temporal sequence. When the thing under review is
temporal, record video instead of burst-capturing screenshots and hoping a
frame lands.

```bash
# Dump numbered PNG frames while connected/zoned (render-thread hook, off by
# default — a normal run with no --dump-* flags touches none of this):
./build/bin/willeq -c willeq.json \
    --dump-frames-dir /tmp/capture --dump-every 1 --dump-max-frames 600

# Encode the frame directory into a reviewable mp4 with MEASURED (not
# assumed) fps:
bash scripts/ci/encode_capture.sh /tmp/capture <UNIT_ID> [output_dir]
```

- `--dump-frames-dir <dir> --dump-every <N>` (T64, extracted from the P12
  anim-video-check session): writes a PNG via the existing
  `IrrlichtRenderer::saveScreenshot()` path every Nth rendered frame, from
  `Application::render()` right after a successful `processFrame()`. Empty
  dir or `--dump-every 0` is a complete no-op — zero cost beyond one
  string-empty + int check.
- `--dump-max-frames <N>` (default 600) bounds total frames written — the
  Orange Pi's `/tmp` tmpfs is only ~242MB, and unbounded dumping is a
  footgun there (T61 already had to deploy stripped binaries to make room).
  Dumping stops with one log line once the cap is hit.
- `scripts/ci/encode_capture.sh` computes the **effective fps from the
  frames' own file mtimes** — never assumes 30/60 — so the video plays back
  at true elapsed wall-clock time. It also writes `<UNIT_ID>_capture.txt`
  next to the video with the measured fps/frame-count/real-time span; keep
  that file with the video, don't discard it.
- **Capture fps is an artifact of the capture mechanism, not a performance
  measurement.** Per-frame `glReadPixels` + PNG encode is expensive — P12
  measured ~4-7.6 effective fps on this x86 dev VM under llvmpipe, far below
  real-time. A viewer without that context will wrongly conclude the game
  runs at ~5fps. Never draw a performance conclusion from a capture video.
- Verify frames are real images before encoding — `saveScreenshot()` has a
  documented history of returning garbage under the surfaceless path (P10,
  P20); a video of garbage is worse than no video. See
  `refactor/T64_video_capture_extraction.md` and `review/T64/README.md` for
  the worked example (proof frames, verification method, deviations).

### Linux Capabilities

The client requires specific Linux capabilities for optimal network performance:

```bash
# Set capabilities on the binary (required for reliable zone loading)
sudo setcap cap_net_raw,cap_net_admin+ep ./build/bin/willeq
```

**Why this is needed:**
- `CAP_NET_RAW`: Required for raw packet capture (diagnostic feature)
- `CAP_NET_ADMIN`: Required for `SO_RCVBUFFORCE` to set UDP socket receive buffer above kernel limit

**Background:** During zone loading, the EQ server sends a burst of 40+ packets at nearly the same instant. The default Linux socket buffer (~425KB) can overflow, causing packet loss and zone loading failures (typically hanging at 40%). The client uses `SO_RCVBUFFORCE` to request a 1MB buffer, which requires `CAP_NET_ADMIN`.

Without `CAP_NET_ADMIN`, the client falls back to the kernel maximum (`net.core.rmem_max`, typically 212KB), which may cause intermittent zone loading failures on busy servers.

### Orange Pi (ARM) Cross-Compilation

The client cross-compiles for Orange Pi One (Allwinner H3, ARMv7-A Cortex-A7, Mali 400 GPU).

**Current target platform**: Ubuntu Noble (24.04) with mainline kernel and Lima open-source driver.
- **GPU rendering**: Native OpenGL ES 2.0 via custom `COpenGLES2Driver` (extends Irrlicht's `CNullDriver`)
- **Display**: DRM/KMS direct rendering without X11 (`--drm` flag)
- **Build**: `docker/Dockerfile.arm-noble` + `scripts/build-arm-noble.sh` (sets `-DEQT_GLES2=ON`)
- **Output**: `build-arm-noble/bin/willeq` (ELF 32-bit ARM, ~10MB stripped)

```bash
# Cross-compile
bash scripts/build-arm-noble.sh

# Run on Orange Pi (DRM/KMS, GLES2 — the only GLES2 windowing path on ARM)
./willeq -c config.json --drm --renderer gles2 --constrained orangepi -r 800 600

# Run the GLES2 path on the x86 dev VM (llvmpipe via EGL surfaceless; H01)
bash scripts/build-gles2-x86.sh
./build-gles2/bin/willeq -c config.json --renderer gles2
```

Note (H01 finding, 2026-07-23): there is NO X11+GLES2 path — the ARM Irrlicht
patch compiles only the DRM/FB device. On x86 the forked device
(`docker/irrlicht-drm-x86/`) falls back to Mesa's surfaceless EGL platform
(llvmpipe), which is how CI runs the real GLES2 driver headlessly.

**GLES2 rendering backend**: The Orange Pi uses a custom native GLES2 driver (`COpenGLES2Driver`) rather than Irrlicht's desktop OpenGL driver. This eliminates all desktop GL → GLES translation complexity, enables native ETC1 compressed textures (real 6:1 memory savings via `GL_ETC1_RGB8_OES`), and provides a shared rendering path for the planned Android 4.4 port. The driver uses 6 built-in GLSL ES 1.0 shader programs (no fixed-function pipeline). See `src/client/graphics/CLAUDE.md` for details.

**Legacy platforms**: (1) Noble with desktop GL 2.1 (Lima/Mesa) — retired (P32, `docs/decisions.md` 2026-07-28 addendum): `scripts/build-arm-noble.sh --no-gles2` no longer exists, GLES2 is the only ARM renderer. (2) Debian Jessie (kernel 3.4) with proprietary Mali blob and gl4es translation — fully superseded.

**DRM input handling**: The DRM device (`docker/irrlicht-drm/CIrrDeviceFB.cpp`) reads keyboard/mouse via Linux evdev (`/dev/input/event*`). VT keyboard processing is disabled via `KDSKBMODE(K_OFF)` to prevent the console layer from intercepting Ctrl+key combinations.

## Dependencies

Required: OpenSSL, zlib, fmt, GLM (header-only), jsoncpp
Optional:
- Boost (waypoint pathfinding)
- librecast-dev (navmesh pathfinding via Recast/Detour)
- A locally-built patched GLES2 Irrlicht (3D graphics rendering) — see "Build
  Commands" above. Not an apt package: `libirrlicht-dev`/`libxxf86vm-dev`
  (desktop-GL Irrlicht) are no longer used (P30).
- freerdp3-dev, libwinpr3-dev (native RDP streaming)
- Audio uses header-only libraries: miniaudio.h (output), tsf.h + tml.h (MIDI synthesis), dr_wav.h (WAV loading)

## Architecture

### Threading Model (Batch D)

The application runs two threads, decoupled via a thread-safe event/intent bridge:

```
Game Thread                         Render Thread (main)
───────────                         ────────────────────
Network events (TickNetwork)        forwardStagedEvents() → IrrlichtBridge
Game state updates (GameTick)       processFrame() (Irrlicht render)
stagePendingEvents() (T220/T227:    Loading thread management
  ONE GameState, EverQuest-owned,
  written+read on this thread)
Intent processing (bridge intents)
Audio updates                       Zone load completion detection
```

- **GameStateBridge** (`include/client/bridge/game_state_bridge.h`) — thread-safe queue pair using swap-vector pattern. Game thread pushes events (state changes), renderer pushes intents (user actions). D24 coalesces EntityMoved events per entity and PlayerPositionChanged intents.
- **IrrlichtBridge** (`include/client/bridge/irrlicht_bridge.h`) — translates game events into IrrlichtRenderer calls (switch over the 101 `GameEventType` enumerators (T264: every event now has a publisher and a consumer, pinned by test_reachability_sweep))
- **ConsoleBridge** (`include/client/bridge/console_bridge.h`) — logs events to console in headless mode
- **Event types** (`include/client/state/event_bus.h`) — 101 event types covering all game state changes
- **Intent types** (`include/client/events/renderer_intents.h`) — 42 intent structs in the `RendererIntent` variant (T264 pruned; T268 deleted DestroyAllLootIntent -- no Titanium destroy-loot packet; T328 added PlaySoundIntent, T419 RecipeDetailsRequestIntent, T421 RecipeSearchIntent/RecipeFavoriteIntent/ItemDisplayRequestIntent T425 SpellDisplayRequestIntent and T436 CloseBankIntent -- the bank window's Done button, since BankerInteractIntent's handler returns early on an already-open window and nothing else could end a bank session from the renderer; pinned by test_reachability_sweep) for user actions

**Key separation**: EverQuest communicates with the renderer only through the bridge — no `m_renderer` member, no `GetRenderer()` accessor. Application owns the renderer and wires it to EverQuest via bridge pointers. Player movement flows renderer→game exclusively via the `PlayerPositionChanged` intent (S2 removed the legacy synchronous callback); inventory UI state flows game→renderer exclusively via enriched `InventorySlotChanged` events; bridge payloads are typed (no `void*`). The graphics-facing game code is concentrated in `src/client/bridge/bridge_component.cpp` (the boundary TU); `src/client/core/everquest.cpp` itself has only two graphics includes (loading status + command registry). K16 closed the last structural seam: `InitGraphics` takes `EQT::Graphics::IZoneLoadSurface*` (`include/client/graphics/zone_load_surface.h`), the two-method zone-load surface `IrrlichtRenderer` implements, so game logic names no renderer class. The asset loaders still live in the `EQT::Graphics` namespace — that is naming only and was left deliberately (see the closed B006 in `refactor/BOARD.md`). Do not "fix" coupling ad hoc.

### Connection Flow

The client connects through three stages, each with its own connection manager:
1. **Login Server** (`m_login_connection_manager`) - Authenticates and gets server list
2. **World Server** (`m_world_connection_manager`) - Character selection and zone info
3. **Zone Server** (`m_zone_connection_manager`) - Actual gameplay

### Core Components

**Application class** (`include/client/application.h`, `src/client/application.cpp` + 4 `application_*.cpp` TUs, J06)
- Top-level orchestrator — owns renderer, bridge, game thread, loading thread
- Spawns game thread running EverQuest tick loop
- Main thread runs render loop (or sleeps in headless mode)
- Manages zone loading thread (GL context transfer, snapshot handoff)

**EverQuest class** (`include/client/eq.h`, implementation split across `src/client/core/everquest.cpp` + domain TUs under `src/client/core/`, `src/client/action/` and the extracted `src/client/<family>/` components, Batch K)
- Game state and network logic. `src/client/core/everquest.cpp` itself (K14: moved from the old flat `eq.cpp`) holds only the core: lifecycle, connection setup, the zone packet-receive callback. Everything else (the zone-stream dispatcher, zone entry, movement, entities, chat, merchant, bridge, …) lives in domain TUs — consult `docs/module_map.md` to find the right file
- Manages all three connection stages
- Handles entity tracking, movement, chat, combat, and doors
- All Titanium opcodes defined as enums: `TitaniumLoginOpcodes`, `TitaniumWorldOpcodes`, `TitaniumZoneOpcodes`
- Zero graphics dependencies — communicates with renderer only via GameStateBridge

**IrrlichtRenderer class** (`include/client/graphics/irrlicht_renderer.h`, `src/client/graphics/renderer/` + eleven component directories, Batch L)
- The renderer. Its own four TUs (`irrlicht_renderer.cpp`, `renderer_init.cpp`,
  `renderer_entities.cpp`, `renderer_event_receiver.cpp`) live in
  `src/client/graphics/renderer/`, beside T460's free-function
  `software_cursor_texture.cpp`; everything else was extracted into eleven
  components, each `class EQT::Graphics::<Name>Component` with its own
  directory under `src/client/graphics/` and `include/client/graphics/`:
  **RdpComponent** (`rdp/`), **Eqe1ArchiveComponent** (`eqe1/`),
  **OverlaysComponent** (`overlays/`), **DiagnosticsComponent**
  (`diagnostics/`), **PlayerCameraComponent** (`player/`),
  **SlashCommandsComponent** (`slash/`), **SimBridgeComponent** (`sim/`),
  **ZoneStreamComponent** (`zone_stream/`), **ZoneLifecycleComponent**
  (`zone_lifecycle/`), **ZoneLoadComponent** (`zone_load/`) and
  **FrameComponent** (`frame/`)
- `EQT::Graphics::RendererContext` (`include/client/graphics/renderer_context.h`)
  is the renderer's `ClientContext`: the shared members bound by reference plus
  a pointer to each sibling component (`ctx_.Frame()`, `ctx_.ZoneLoad()`, …).
  A component reaches shared state as `ctx_.member`, a member OWNED by another
  component as `ctx_.<Owner>().<accessor>()` (L13's pairwise rule: a member two
  components share moves to the one that writes it), and anything still on the
  class as `owner_.member_`. L13 took that last handle from 1,065 uses to
  151 — 81 of them public methods and 70 in thirteen listed stays, none of
  which may move because each crosses a thread boundary or backs a lifetime
  contract. That number drifts with normal work: T366's census read 163, T371's
  read 165 and T388's reads **174** — re-measure, do not quote it. See the closed **B050**
- Lifecycle entry points (`initLoadingScreen`, `initDeferredGraphics`,
  `processFrame`, `shutdown`, `run`) stay on `IrrlichtRenderer`: `Application`
  and `IrrlichtBridge` must not learn a component exists
- Consult `docs/module_map.md` for which TU holds which body

**Network Stack** (`include/common/net/`, `src/common/net/`)
- `DaybreakConnection` - UDP reliable protocol with sequencing, fragmentation, CRC, and compression
- `Packet` - Base class with `StaticPacket` (fixed buffer) and `DynamicPacket` (resizable)
- `DaybreakConnectionManager` - Handles connection lifecycle via UdpTransport
- Socket receive buffer set to 1MB to handle server packet bursts during zone loading

**Pathfinding** (`include/client/pathfinder_*.h`)
- `IPathfinder` - Abstract interface
- `PathfinderNull` - Fallback (no pathfinding)
- `PathfinderNavMesh` - Detour navmesh (optional, requires EQT_HAS_NAVMESH)
- `PathfinderWaypoint` - Boost A* waypoint graph (optional, requires EQT_HAS_WAYPOINT)

**Maps** (`include/client/hc_map.h`, `include/client/raycast_mesh.h`)
- `HCMap` - Zone map loading (V1/V2 formats)
- `RaycastMesh` - AABB tree for collision/LOS checks

**Combat** (`include/client/combat.h`)
- `CombatManager` - Auto-attack, targeting, looting, combat abilities

**Trading** (`src/client/trade_manager.cpp`)
- `TradeManager` - Player-to-player trading, item/money exchange, trade state machine

**Spells** (`src/client/spell/`)
- `SpellManager` - Spell casting, gem state, cooldowns, memorization. The only
  one of these a player reaches: gem click / `/cast` -> `beginCastFromGem()` ->
  `validateCast()`. Its bodies live in six TUs (T407 split the 1,451-line
  `spell_manager.cpp` before it breached the ceiling) - see
  `docs/module_map.md` for which TU holds which method; the header's banners
  name the defining TU too.
- `SpellTypeProcessor` - **DELETED (T324, 2026-09-02)**. Client-side target
  resolution with zero callers; the server resolves spell targets and reports
  each landing.
- `SpellEffects` - **DELETED (T327, 2026-09-02)**, on Will's ruling. Effect
  application with zero callers once T324 removed its sole consumer; all 49
  per-effect handlers were logging stubs, and its value-scaling table was wrong
  against the server (recorded in `refactor/completed/deferred_archive_20260903.md`, item 45).
- Both deletions share one tombstone in `src/client/core/everquest.cpp`. Do not rebuild
  either; the server is authoritative and reports every landing per target.

**Skills** (`include/client/skill/`)
- `SkillManager` - Tracks player skills, handles activation, cooldowns
- `SkillData` - Skill properties (value, category, cooldown, requirements)
- `skill_constants.h` - Skill IDs, names, categories, animation mappings

### Packet Structures

Titanium-specific packet structures are defined in `include/common/packet_structs.h`. These are binary-compatible with the Titanium client protocol.

### Logging

**IMPORTANT**: All new logging code MUST follow the standards in `docs/standards/debug_logging_standards.md`.

Key points:
- Default (level 0/NONE): Quiet mode - errors still reported, no verbose output
- Seven levels: NONE, FATAL, ERROR, WARN, INFO, DEBUG, TRACE
- Module-based filtering (NET, ENTITY, GRAPHICS, etc.)
- Runtime configurable via CLI, config file, or signals
- Output format: `[TIMESTAMP] [LEVEL] [MODULE] message`

Legacy logging in `include/common/logging.h` does not follow these standards and should be migrated when touching existing code.

## Test Organization

Tests are in `tests/` with one executable per test suite. Each test links only the minimal sources it needs:
- `test_packet.cpp` - Packet serialization
- `test_titanium_opcodes.cpp` - Opcode value verification
- `test_packet_structs.cpp` - Binary struct layout validation
- `test_integration_network.cpp` - Network protocol integration
- `test_formatted_message.cpp` - FormattedMessage parsing for NPC dialogue

Audio tests and graphics integration tests are documented in their respective CLAUDE.md files.

**Session replay oracle** (Batch R, `docs/plans/session_replay_oracle.md`): record a real play session with `--record-session data/captures/session.sleq`, then `test_session_replay` replays it into a headless EverQuest and asserts game state against a golden snapshot (self-created on first run). A synthetic-session mode keeps the test green without a recording. Protocol-level pcap tests (`test_daybreak_*`, `test_pcap_*`) skip unless a capture exists at `data/captures/session.pcap` (or `WILLEQ_PCAP`).

## Key Patterns

- **Bridge events/intents**: Game state changes are published as events via `m_bridge->pushEvent()`. Renderer user actions arrive as intents via `m_bridge->drainIntents()`. Never call renderer methods directly from EverQuest.
- **Zone loading**: Application creates a `ZoneLoadSnapshot` (immutable copy of game state), starts a loading thread that owns the GL context, builds GPU resources, then signals completion. Game thread continues processing network packets during loading.
- Zone connection requires specific packet sequencing - see `CheckZoneRequestPhaseComplete()` and the various `m_*_sent` / `m_*_received` flags
- Movement updates require `SendPositionUpdate()` to sync with server
- Entities tracked in `m_entities` map keyed by spawn_id
- Doors are tracked by `DoorStateManager`
  (`include/client/door_state_manager.h`, reached as
  `GameState::doors()`) — `doors_`, a `std::map<uint8_t, DoorState>` keyed by
  door id. There is no `m_doors` member on `EverQuest`. <!-- RETIRED-OK: naming the member that is gone is the point of this sentence --> Since T376 (B141) the
  same manager also owns the pending-click set (`pendingClicks_`,
  `addPendingClick()` / `isClickPending()`); the duplicate
  `EntitiesComponent::m_pending_door_clicks` and `ClearPendingDoorClicks()` <!-- RETIRED-OK: tombstone for the two names T376 deleted -->
  were deleted. `ZoneProcessMoveDoor` must ask `isClickPending()` BEFORE
  `setDoorState()` consumes the entry
- Friend class pattern: `CombatManager` and `TradeManager` are friends of
  `EverQuest` for internal access. **They are the only two** (T366, 2026-09-05,
  board B038 CLOSED): no extracted component is a friend and none holds an
  `EverQuest&` transition handle any more. A comment-filtered `owner_` grep
  under `src/client` / `include/client` returns **zero outside `graphics/`**;
  every `owner_` inside `graphics/` is a different pattern — Batch L's
  `IrrlichtRenderer& owner_` in the eleven renderer components (the
  RendererContext pattern) and a `ZoneShaderManager*` back-pointer in
  `zone_shader_callbacks.h`. A component reaches shared state as `ctx_.member`
  and a sibling family as `ctx_.<Family>().Foo()`; if something new needs
  `EverQuest`, it goes on `ClientContext` or a context service, not on a handle.
- **BSP tree** (`include/client/zone_bsp.h`): Zone spatial data shared between game state (water detection, zone lines) and renderer (PVS culling). No graphics dependencies.

## Configuration

**Main config** (willeq.json):
```json
{
    "eq_client_path": "/path/to/EverQuest"
}
```

**RDP configuration** (optional):
```json
{
    "eq_client_path": "/path/to/EverQuest",
    "rdp": {
        "enabled": true,
        "port": 3389
    }
}
```

**Spell effects configuration** (optional):
- `config/spell_effects.json` - JSON-configurable spell particle presets with hot-reload
- 3-tier hierarchy: global settings → per-style defaults → per-spell overrides
- Configures resist colors, spawn rates, lifetimes, motion types, blend modes

**Per-character UI state IS persisted, and it is the only thing that is**
(T432, board **B275** closed; T402 found the gap). The client writes
`<cache dir>/ui/UI_<Char>_<server>.ini` — the official client's own filename,
section names and `Show=0/1` keys — and re-reads it at zone-in. What it holds
is what Will ruled user-changeable (`docs/decisions.md` 2026-09-07): the seven
toggleable windows (`[InventoryWindow]`, `[SpellBookWnd]`, `[SkillsWindow]`,
`[GroupWindow]`, `[PetInfoWindow]`, `[BuffWindow]`, `[CombatAbilityWnd]`) and
`[Camera] Mode=` — **plus, since T434, `[OptionsWindow] Show=` and an
`[Options]` section holding `Music`, `SoundVolume` and `ClipPlane`**, the
three Options-window controls fable can wire, under the oracle's own key
names and on the oracle's own 0..20 slider scale (measured: `eqclient.ini`
has `Music=18`/`SoundVolume=10` and that client displays 90 %/50 %). Window POSITIONS are static by design — `UILayout` computes
every rect from the resolution — and key bindings are global
(`config/hotkeys.json`), so neither is per character. An ABSENT `Show=` key
keeps the default, which is `defaults.ini`'s own rule (T368), so a fresh
character writes no file until it changes something. **Chat timestamps, chat
scrollback and UI colours are still NOT persisted**; that was scope, not
oversight.

The whole feature is four files —
`include/client/graphics/ui/ui_preferences.h`,
`src/client/graphics/ui/ui_preferences.cpp` (the format, device-free) and
`src/client/graphics/frame/renderer_frame_preferences.cpp` (the load at
zone-in, the save on change and at `/camp`). There is deliberately **no
per-toggle save hook**: `FrameComponent::syncUiPreferences()` compares the
whole collected value against the last one it wrote, so a newly persisted
field cannot be added and then forgotten at one call site. A new persisted
field goes in `UiPreferences` + the `kWindows` table (or T434's `kOptions`
table, the same member-pointer device for the non-boolean `[Options]` keys) +
the binding, nowhere else. T434 added a fourth file to the feature,
`src/client/graphics/frame/renderer_frame_options.cpp`, because its three
persisted fields are the first that have a SETTING behind them rather than
just a struct member: two of them go to the game thread as intents and the
clip plane is a renderer call, so `FrameComponent::applyOptionsSetting()` is
the one body both the click path and the load path go through. **T439 (board
B432) made the volume round trip.** The two volume intents carry an ABSOLUTE
step 0..20 (`include/client/audio/volume_steps.h`, the oracle's own unit) and
`BridgeComponent::PublishVolumesChanged()` publishes what the audio manager
actually holds back on `GameEventType::VolumesChanged` — from all four sites
that move a volume: the config apply, the two intents, and `/musicvolume` /
`/effectsvolume`. Before that the intents carried a delta, the renderer
shadowed what it had last sent, and a slash command behind its back left the
slider stale and made its next drag apply a difference against a value that
was no longer true. It is a generated file, so it goes through `ResolveCachePathForWrite()`
(T402's rule) and never dirties the checkout.

The two files that once looked like the persistence,
`config/chat_settings.json` and `config/ui_settings.json`, were
the DELETED `WindowManager`'s saved state (removed with it in `0f25cb57`,
"U07c2: Delete WindowManager + all old UI window files"); they described
draggable windows the client no longer has and were read by nothing, so T402
deleted them (board B245). Every shipped `config/*.json` that remains is
named by code, and `DataPathRouting.EveryShippedConfigFileIsNamedByCode`
keeps it that way.

Audio configuration is documented in `src/client/audio/CLAUDE.md`.

## Graphics Integration

Graphics is enabled by default (`EQT_GRAPHICS=ON` in CMake). Requires EQ Titanium client files for zone geometry and character models. See `src/client/graphics/CLAUDE.md` for rendering details, and `include/client/graphics/ui/CLAUDE.md` for UI and key bindings.
