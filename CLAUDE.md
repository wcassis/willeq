# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**This file is RULES plus a map. Narrative history lives in `refactor/completed/`
and `docs/decisions.md`; if you want to know why a rule exists, follow the
citation rather than expecting the story here.** It has a hard size ceiling of
60,000 characters, enforced by the `FileSizeCeiling` ctest test, because the
kanban workers that read it as a context file silently truncate past ~62,900.

Additional context is in subdirectory CLAUDE.md files:
- `src/client/graphics/CLAUDE.md` - Rendering, shaders, GLES2, zone loading, performance
- `include/client/graphics/ui/CLAUDE.md` - Static UI panels, key bindings, slash commands
- `include/client/state/CLAUDE.md` - Game state modules, EventBus, 108 event types
- `include/client/bridge/CLAUDE.md` - Event/intent bridge, thread-safe game↔renderer communication
- `src/client/input/CLAUDE.md` - Input handlers, hotkey manager, command processor, action dispatch
- `src/client/audio/CLAUDE.md` - Audio system, sound categories, audio tests
- `docker/CLAUDE.md` - Cross-compilation, DRM/KMS device, GLES2 driver
- `docs/CLAUDE.md` - Coordinate systems, heading conventions, docs layout

## Orientation (read before large tasks)

- `docs/decisions.md` — standing architecture decisions, including every ruling
  from Will. Settled; do not re-litigate.
- `docs/module_map.md` — functional map of every split translation unit
  (EverQuest, Application, CommandProcessor, InventoryManager, IrrlichtRenderer,
  EntityRenderer, WLDLoader, SimulationWorker, ZoneShader, tests/cmake). Consult
  it to find the right TU instead of scanning.
- `refactor/BOARD.md` — **the open-work queue of record. Start here.** One entry
  per open item, each confirmed against the tree at the tip named in its header,
  classed A (live defect) / B (unreached or hygiene) / C (needs Will's decision)
  / P (process). Nothing else is a work queue.
- `refactor/K_component_extraction.md` — the open structural programme
  (component extraction from `class EverQuest`, then `class IrrlichtRenderer`).
  The active unit plans are `refactor/K<nn>_*.md`.
- `refactor/completed/` and most of `docs/reference/` are historical snapshots
  (bannered); do not trust their findings as current, nor the three
  `*_archive_20260903.md` trackers `BOARD.md` replaced (K00, 2026-09-03).
- Workspace: canonical checkout at `/mnt/projects/fable/main` (branch `fable`;
  symlink from `/mnt/projects/willeq-fable`), executor worktree slots under
  `/mnt/projects/fable/wt/` (live list: `git worktree list`). Task branches:
  `task/<UNIT>-<name>`, e.g. `task/K01-guild-component`. The `renderer` branch is
  reference-only — never merge it.

**Reading order for a new agent:** `refactor/BOARD.md` → `docs/module_map.md` →
`docs/decisions.md` → the unit plan you were dispatched with.

## Process requirements

- Every unit of work is PLANNED by the orchestrator (a `refactor/<UNIT>_*.md`
  plan with scope, file territory, STOP conditions and a verification bar) and
  EXECUTED by executor agents in worktree slots under
  `refactor/EXECUTOR_PROTOCOL.md`. Independent legs are dispatched in parallel
  slots whenever their file territories do not overlap; the orchestrator
  serializes merges onto `fable` (`docs/decisions.md`, 2026-07-22 workspace
  layout). Do not execute unplanned work inline in `main`.
- **Before merging a branch, run the order/coverage golden tests for every TU
  the branch touched** (T337; the G batch pinned `zone_dispatch`,
  `bridge_component_intents`, `command_processor_registration`, `application`,
  `renderer_frame_render`, `renderer_player_physics` with goldens). A branch cut
  before a golden's unit merged is green on its own and fails on the tip.
- **A number in a doc is a liability.** Where a count drifts with ordinary work,
  name the command that measures it instead. `DocCounts`, `DocClaims`,
  `DocCitations` and `CommentClaims` (ctest label `hygiene`) enforce the
  survivable half of this; they only check what they were taught, so a unit that
  states a new number adds a row to `scripts/refactor/check_doc_counts.py`.
- **Strike every mention when you close an item.** A fix verified only by a
  hand-run tool leaves no regression net and no doc trigger.
- **A Pi perf or memory finding names its mechanism before it asks for a
  ruling** (B1006, #327): the driver fact it rests on
  (`docs/reference/lima_cost_sheet.md`, or a cited Mesa function for a new
  one) and the prediction that fact makes, checked against the measurements
  in hand. A finding without one is a measurement, not a ruling request;
  reviewers re-derive the mechanism, not only the citations.
- **Local-model kanban cards follow `docs/local_model_cards.md`** (B1020):
  prove-then-fix and tool-plus-live-leg are two cards each, workers never edit
  a CLAUDE.md or cut cards (`CLAUDE.md EDITS` / `FOLLOW-UP` summary sections
  instead), and a chain merges only on PASS verdicts naming the current head
  sha. After every wave a retrospective classifies each block/fail/retry by
  cause and fixes the cause there.
- **CI runs the build and the suite on the GitLab runner, not in your
  worker.** `.gitlab-ci.yml` (T502) builds Release and runs the full ctest suite
  on pushes to `task/*`, `plans/*` and `fable`, and on merge requests, in three
  stages: `hygiene` (no C++ build, so a doc/size/citation break fails in under a
  minute; the WHOLE pipeline when every changed path is `*.md`, B1042), `build` (`build-test`
  builds and runs the suite in one job, B1042; `RDPServerFrameTest.UpdateFrameMultipleTimes`
  is re-run with `--repeat until-pass:3`) and `gate` (`visual_gate`, 3 in-script retries,
  never `allow_failure`, all five manifest zones, `docs/ci.md`). The x86 jobs run
  on **gitlab-runner-1 and gitlab-runner-2** (tag `x86-build`, B1008) and refuse
  other hosts (a kanban worker's 4 GiB cap OOM-kills a link; builds stay off the Inspur box and off
  hermes-ext). `client-ledger-live` rebuilds in-job a tree not stamped by its host+sha (B1026).
  Push a new branch with `-o ci.skip`, then open the MR: the MR pipeline is the one that counts
  (B1042). Runner trees are **warm**: force a clean pipeline before trusting a build-system change.

### File sizes

**Batches J, K, L and G are complete: every `*.cpp`/`*.h`/`*.hpp` under `src/`,
`include/`, `tests/`, `tools/` and `benchmarks/` is under 1,500 lines, and
`scripts/refactor/file_size_allowlist.txt` is EMPTY.** A file over 1,500 is a
**FAIL** in the `FileSizeCeiling` ctest test, not a warning (M00,
`docs/decisions.md` 2026-09-04; T385/B167 added `tools/`, `benchmarks/`). 3,000 lines is a second, universal hard
cap that applies to allowlisted files too.

Measure, do not quote: `bash scripts/refactor/check_file_sizes.sh -C <repo>
--all` prints the file count and every warn/fail. Put substantial new code in
new TUs; a file that must exceed the ceiling needs an allowlist entry with a
reason and a no-grow max. Close-outs: `refactor/J_file_size_reclamation.md`,
`refactor/K_component_extraction.md`,
`refactor/L_renderer_component_extraction.md`, `refactor/G_oversize_methods.md`.

**`CLAUDE.md` itself has a 60,000-character ceiling** (same test, reason at the top). Cut narrative before you cut a rule.

### Memory accounting (Will's standing decisions)

- **100 % accounting of memory use is the PRIMARY goal** (`docs/decisions.md`
  2026-09-14, T491). Untracked anonymous memory is a defect to drive to zero,
  not a number to report. `include/client/core/memory_ledger.h` replaces the
  allocator entry points and charges every block's whole glibc chunk to the
  calling thread's `MemoryScope` category; `scripts/ci/visual_gate.py` gates the
  unattributed and slack shares and every category against its budget.
- **32–64 MB is the SECONDARY goal** (`docs/decisions.md` 2026-09-17), and
  **it is judged on willeq's own memory: `RssAnon` minus the ledger's
  `gpu_driver`** — "64 MB excludes driver memory" (Will, 2026-09-29). Driver
  .so file pages are outside it too, but every driver byte is still reported
  as its own row. The 256 MB envelope on `RssAnon` stays the FAILING bound in
  CI; 64 MB is printed, never asserted, with the gap broken down by bucket
  (`eqt::memory::gapToTarget`), taken on **owned = RssAnon − gpu_driver +
  gpu_buffers** (label `owned=RssAnon-gpu_driver+gpu_buffers`; the
  `gpu_buffers` row is added only when the `[memgpu]` row is OUTSIDE
  RssAnon; `over_target` on the `[memledger]` line,
  `over_target_rss_anon` is the whole-RssAnon figure; `gpu_driver` is listed
  as EXCLUDED, never a shrink bucket; the driver .so COW pages stay on the
  owned side; B978 2026-10-01 superseded B933's figure). **Memory first,
  frame rate second** (Will,
  2026-09-29).
- **GPU buffers are outside RssAnon on the Pi** (Lima GEM/shmem, B955): the
  `gpu_buffers` row (`[memgpu]`, `include/client/core/gpu_buffer_census.h`)
  sees them only when the client runs in its own cgroup (the Pi rigs do).
  B978 (2026-10-01) counts that row toward the 64 MB target when it is
  outside RssAnon; the `[memgpu]` line ends `owned_rss_anon=<RssAnon-gpu_driver>
  owned=<target figure>`; the old `owned_plus_gpu_buffers` field was removed
  by B978 and its absence is pinned by the tests.
- **Development may be done on x86, but the final numbers are required on BOTH
  platforms**, and a shrink unit is planned off the Orange Pi's own ledger
  output, not the x86 one — the two queues disagree by more than an order of
  magnitude in both directions (`docs/decisions.md` 2026-09-17 addendum, T496).
- **A block shared by several owners is reported ONCE, with all its claimants
  named** — never counted into each claimant, which would inflate the total and
  make 100 % accounting unreachable (relayed at T501; the shared-block half
  has no `docs/decisions.md` entry yet).

## Project Overview

WillEQ is a standalone EverQuest client targeting the Titanium client version
with optional 3D graphics rendering. It was migrated from the akk-stack headless
client to remove all eqemu dependencies. All opcodes are hard-coded for Titanium
specifically.

## Build Commands

**Pass a hardcoded `-j` (`-j$(nproc)` does not expand in a Bash tool command;
hermes-ext workers `-j3`, B1046) and `-DCMAKE_BUILD_TYPE=Release`; build NAMED
targets, never a whole tests-on tree at `-j12`** (2026-10-19 OOM,
`docs/decisions.md`).

**Desktop-GL Irrlicht support has been removed (P30,
`refactor/desktop_gl_elimination_plan.md`).** `EQT_GRAPHICS=ON` (the default)
requires a locally-built patched GLES2 Irrlicht — there is no
system-`libirrlicht-dev`/X11-GLX fallback. Configuring without one fails early
at `cmake` time with a message naming the bootstrap command below.

**One-time bootstrap** (only needed once per checkout, or after the patched
Irrlicht is deleted — `third_party/irrlicht-gles2-x86/` is gitignored):

```bash
bash scripts/build-gles2-x86.sh --irrlicht-only
```

```bash
# Configure (from project root) — the GLES2-config tree is the primary dev/test
# tree (tests on, full ctest suite). Since T392 (2026-09-05, board B215) the
# suite has NO expected failures: a red test is a real red.
mkdir -p build && cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DEQT_GRAPHICS=ON \
    -DEQT_GLES2=ON \
    -DEQT_DRM=OFF \
    -DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86 \
    -DEQT_BUILD_TESTS=ON

cmake --build build -j24
cd build && ctest --output-on-failure

# Live/integration tests (need a running eqemu server + display + EQ client
# files) are NOT in the default suite. Register them, then select by label:
#   cmake -S . -B build -DEQT_LIVE_TESTS=ON
#   ctest --test-dir build -L live
# The binaries build either way; only ctest registration is gated (T164).

# The hygiene tests are pure source/doc scans and need no C++ artefact:
#   ctest --test-dir build -L hygiene

# EQT_GRAPHICS=OFF is built AND tested by CI's build-headless job (B1038): a
# test case reaching a GRAPHICS_SOURCES TU is #ifdef EQT_HAS_GRAPHICS, or the
# test is registered only under EQT_GRAPHICS.

# One test, or the binary directly
cd build && ctest -R TestName --output-on-failure
./build/bin/test_packet

# Build output
./build/bin/willeq
```

This is the same patched Irrlicht / EGL+GLESv2 backend (surfaceless llvmpipe on
this dev VM, native GLES2 on the Orange Pi) that `scripts/build-gles2-x86.sh`
and `scripts/build-arm-noble.sh` use — see "Orange Pi (ARM) Cross-Compilation".
`scripts/build-gles2-x86.sh` with no args additionally produces a second,
smaller tree (`build-gles2/`, tests off) holding just `willeq` +
`gles2_zone_benchmark` — **that is the tree `scripts/ci/visual_gate.sh` runs the
benchmark out of**, and the gate needs it rebuilt from the same tip or it fails
with an identical `characterModelsBytes` deficit in every zone (a stale tree,
never a defect).

## Running

```bash
# With graphics (default: surfaceless EGL, no display required — the same
# llvmpipe backend the GLES2 tree's tests and visual gate use)
./build/bin/willeq -c willeq.json

# With graphics, X11-windowed (P25; opt-in only, never DISPLAY-autodetected)
./build/bin/willeq -c willeq.json --egl-x11

# Headless (no graphics)
./build/bin/willeq -c willeq.json --no-graphics

# VNC (headless servers). VNC_PORT=5950
./scripts/start-with-vnc.sh -c willeq.json          # vnc://localhost:5950

# VNC + audio streaming: VNC_PORT=5910, AUDIO_PORT=8085
./scripts/start-with-vnc-audio.sh -c willeq.json    # + vlc http://localhost:8085

# Native RDP (alternative to VNC), default port 3389
./build/bin/willeq -c willeq.json --rdp [--rdp-port 13389]
# mstsc.exe /v:host:3389   |   xfreerdp /v:host:3389 /cert:ignore
```

| Method | Use Case | Audio | Client |
|--------|----------|-------|--------|
| Surfaceless EGL (default) | No display needed at all — llvmpipe GPU emulation | Local | Native |
| X11-windowed EGL (`--egl-x11`, P25) | Local display, GLES2 in a window | Local | Native |
| Xvfb + VNC | Remote access, cross-platform | None | VNC client |
| Xvfb + VNC + Audio | Remote with sound | HTTP stream | VNC + VLC |
| Native RDP | Windows users, better compression | RDP audio | mstsc.exe, xfreerdp |

### Where the client's shipped data comes from

**The client does not have to be started from the repo root** (T399, board
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
4. the directory holding the running executable, then up to five parents.

`willeq.json` is resolved the same way. **A new
shipped-data load must call `ResolveDataPath()`** —
`tests/test_data_path_routing.cpp` re-takes the census on every ctest run and
fails on a bare `"config/…"` / `"data/…"` literal that does not.

**Generated caches follow the opposite rule: they are written OUTSIDE the
checkout** (T402, board B248), through `eqt::ResolveCachePathForRead()` /
`ResolveCachePathForWrite()` (same header), which pick the first writable of
`$WILLEQ_CACHE_DIR`, `$WILLEQ_DATA_ROOT/cache`, `<exe dir>/cache` and `cache/`.
The GLES2 driver's shader-binary cache
(`<hash>.bin`, `<cache dir>/shader_cache/`) is one too (B959): the host calls
`eqt::ConfigureGles2ShaderCache()` before creating its device and the driver, which
sits below the client, only ever receives the directory; none set means cache off,
never `config/shader_cache` under the CWD. **A new generated cache uses these,
never `ResolveDataPath()`.**

### The shipped data has build recipes — use them, do not hand-edit

Three shipped assets are **derived**, not hand-carried, and each has a ctest
test that fails if the checked-in bytes and their inputs drift apart:

| asset | recipe | pinned by | ctest |
|---|---|---|---|
| `data/textures/*.bmp` (72 files) | `scripts/build_textures.sh` → `tools/generate_textures` (64, procedural), `tools/spell_sprite_atlas_builder` (1), `tools/item_icon_atlas_builder` (2), `tools/spell_icon_atlas_builder` (4), `tools/spellbook_backdrop_builder` (1) | `data/textures.sha256` | `TexturesReproducible` |
| `/mnt/projects/EverQuest_ETC1/willeq.eqe1` (142 entries: 72 textures, 69 ETC1 `_alpha` companions, 1 raw — `build/bin/verify_eqe1` prints the census) | `scripts/build_willeq_pack.sh` | `data/willeq.eqe1.sha256` | `PackReproducible` |
| `data/willeq.db` (28,791 rows in ten tables; `metadata.version` = 8) | `scripts/build_willeq_db.sh` → `tools/willeq_db_builder`, over the EQ client's seven `*.txt` files plus `data/item_models.json` and `config/race_models.json` | `data/willeq.db.sha256` | `DbReproducible` (T450) |

The pack lives outside the repo and is read live by every worktree; only
`build_willeq_pack.sh` may install it (temp file in the destination directory,
`mv`d into place).

**The EQE1 pack's format version matters, and a stale one fails loudly**
(B930, #248). A v1/v2 pack caps entry names at 31 characters, so a zone
whose shipped textures need a longer name dies in the client's
all-or-nothing texture load with a FATAL that names the STALE PACK AND
ITS VERSION. A structurally-clean v1/v2 pack is still legitimate when
all its names fit 31 chars (willeq.eqe1 is v2 and stays v2 by the
builder's design). Audit a host's pack directory:
`for f in <dir>/*.eqe1; do ./build/bin/verify_eqe1 --require-v3 "$f"; done`
(STALE on any v1/v2, exit 1). Pinned by test_eqe1_stale_pack and
test_eqe1_verify_stale_pack.

**The pack's inputs are the 72 BMPs plus `tools/ui_atlas_builder`, so a texture
change cascades**: regenerate the BMPs, rewrite `data/textures.sha256`
(`--write-pins`), rebuild the pack, update `data/willeq.eqe1.sha256` — all in
**one commit**, or both tests go red.

**Rebuild the database with `scripts/build_willeq_db.sh`** and commit
`data/willeq.db` together with its new `data/willeq.db.sha256` line, exactly
like the pack. The pin is possible because `metadata.input_digest` (a digest of
each input's ROLE plus its contents, never its path, so every worktree agrees)
replaced a `__DATE__ " " __TIME__` build stamp that changed the bytes on every
recompile. **Do not bump `kWilleqDbExpectedVersion` to ship a data change**: a
version bump invalidates every build tree in the workspace (T304/T355), and a
row edit is not a schema change.

**Every table the database ships is one the client queries, in both directions,
with no exemptions** (T452/T454/T456, boards B468/B474/B476).
`WilleqDatabaseShippedContent.EveryTableTheClientQueriesExists` and
`…EveryTableTheDatabaseShipsIsOneTheClientQueries` are the two nets; the
equipment paths resolve live through `EquipmentModelLoader::buildEquipmentIndex()`. **B468's lesson: "nothing reads
it" and "nothing CAN read it" look identical from a grep**, so each drop gets
its own deliberate check rather than being swept along.
`data/equipment_models.json` and `tools/equipment_index_builder` still exist and
now feed nothing — boarded, not deleted as a side effect.

**`data/textures/` has FIVE recipes (B300/B376/B1077/B1076).** Sixty-four of the
BMPs are procedural (`generate_textures`); the other eight are the official
client's own art. `spell_sprite_atlas.bmp` comes from `SpellEffects/` (B1127: the
spellsnew.edd textures, PNG/TGA/BMP) via `tools/spell_sprite_atlas_builder`, and `item_icons0.bmp` + `item_icons1.bmp`
from `uifiles/default/dragitem1..34.tga` via `tools/item_icon_atlas_builder`,
the four `spell_icon*.bmp` via `tools/spell_icon_atlas_builder`, and
`spellbook_backdrop.bmp` (book tiles + Done, composed from `uifiles/default`)
via `tools/spellbook_backdrop_builder`.
`build_textures.sh` runs all five into the same directory and the pin file
covers the union.

#### The item icon sheets

The layout lives in ONE place, `include/client/graphics/ui/item_icon_atlas.h`,
which the builder and the client's `ItemIconSheets` loader both include. Two
facts there are QUOTED from the oracle rather than assumed:

- **34 sheets of 6x6 40x40 cells, ids 500..1723** — `EQUI_Animations.xml`'s
  single `<Ui2DAnimation item="A_DragItem">` lists exactly `dragitem1.tga` ..
  `dragitem34.tga` with `<CellWidth>40</CellWidth>` on a `<Size>256x256</Size>`
  texture.
- **COLUMN-major within a sheet**, because that animation carries
  `<Vertical>true</Vertical>`. The usual `(id-500)%36 / 6 = row` folklore is the
  wrong way round, and `dragitem34` — the only partly-filled sheet — proves it:
  its thirteen icons fill two whole COLUMNS plus one.
  `tests/test_item_icon_atlas.cpp` asserts both the positive and the negative,
  so a "cleanup" back to row-major is a red test rather than 1224 wrong pictures.

1200 of the 1224 cells carry art; an empty cell is still addressable and draws
nothing. The sheets load **EAGERLY**, beside `ui_atlas` in `UIAtlas::load()`,
and that is deliberate: `ConstrainedTextureCache::getOrLoad()` logs FATAL and
`std::exit(1)`s on a pack miss, so first-use loading would turn a startup
failure into a mid-SESSION exit the moment a player opens an inventory (written
out in `include/client/graphics/ui/item_icon_sheets.h`). A `lookup()` miss makes
`UIRenderer::drawItemIcon()` return false, which is why every caller keeps a
text placeholder as the fallback.

#### The spell sprite atlas

Its layout lives in ONE place,
`include/client/graphics/environment/spell_sprite_atlas.h`, which the builder
and `SpellEffectsConfig` both include, so the sheet and the client's addressing
of it cannot drift. A particle's `textureIndex` below `SpellSpriteAtlas::kBase`
(64) is a procedural atlas tile; at or above it, a **sprite ordinal, not a
cell** — because `EmitterConfig::textureRegions[]` is a `uint8_t[4]` and 528
cells do not fit in one. **GRID sheets animate** (4x4, or 2x2 for 128x128 flares;
1024x1024, 32x32 cells, 16 reserved per sprite): `UnifiedParticleRenderer::render()` takes
the CELL from `SpellSpriteAtlas::cellForAge()`, so a grid plays exactly once
per particle life.
**Every 64x64 file is ONE picture, not a grid**
(B1124): `kSheetFrames` says 1, the builder packs them whole (and exits 1 if
its seam census disagrees) and the particle holds that cell all life.

A grid's playback rate is therefore `16 / lifetime`, set by
`config/spell_effects.json`. **A spellsnew.eff slot is timed by its EDD words**
(B1145, `spell_fx_timing.h`): +0x8C delay, run (fixed |+0x78| or the stage's
duration; Source: + the caster's CastingAnim track, B1148), +0x7C life, burst/stream; B1121's schedule
serves spells with no record; OP_DeleteSpawn takes a corpse's emitters and model (faded from 1.3 s,
`corpse_fade.h`) 3.5 s later.
Ends: `review/B1121/scripts/b1120_measure.sh <leg> <out>` (T422).
`tests/test_spell_burst_oracle_match.cpp` pins both.

**A spell particle is its sprite x its EDD emitter's colour words** (B1146).
**The oracle draws `spellsnew.eff`, not `spells.eff`** (B1127,
`review/B1127/README.md`): field 145 (`spell_anim`) names the record, its stages
name `spellsnew.edd` emitters, each names a texture file, and the particle is its
texels, on the row's bands (`tests/spell_fx_*_oracle.h`, B1153). Field 123's GEN names
serve spells with no field 145. Field 85 RESISTTYPE is not painted with: `SpellPresets::applyStageSpriteNames()`
drops the resist tint to white (T441), then `applyStageEmitterColours()` gives each
texture region its emitter's start->end RGB and (B1149) the client's fade x
peak alpha and +0x68 blend (`docs/spellsnew_format.md`). `resistColors` and `colorForVocabulary()` serve only
spells with no usable record. All three stages route their
names: Source → cast glow and spellComplete, Middle → 0x4033 object only, Target →
impact. The landing is a VOLUME, not a disc (B1120). Source and Target are drawn
per spellsnew.eff SLOT (B1144): minLevel <= caster level (NPC 1), on its anchor
at its attach point; no Target slot, no landing; no OP_BeginCast, no Source;
each emits its EDD burst/ticks and sprite size (B1149); the
B1131/B1132 measured rows are test bands now (B1153); `tests/test_spell_slot_rules.cpp`. `spellOverrides` holds
the five measured values (38/41/54/458/656; 253 retired by B1161 #480; B1120/B1122 shapes; sizes gated on the caster-ROI
p90, `review/B1119/scripts/fx_sprite_roi.py`), each kept after the B1153 probe (B1153); **an unmeasured spell resolves
to the plain style: an invented override is the guess Will forbids.**
**The particle pool is ONE preset key**, `spellParticlePool` in
`config/constrained_presets.json` (desktop 8192, sbc 4096; B1130's survey demand
fits both); a spell with no field-145 record derives its landing from it by
one rule (`SpellEffectsConfig::applyPool`, B1123).
**Fable FX gates run in `tools/model_viewer --spell-test --plan`** (B1138, the
client's cast sequence surfaceless); lane 5 is for oracle legs.
`tests/test_spell_colour_and_overrides.cpp` pins both halves and the negatives.

#### Texture alpha

The census re-measured at T444 is **68 of the 72 shipped 32-bit with their alpha
in-band**. The four 24-bit ones each carry a written reason in
`tests/test_texture_alpha_census.cpp`, whose `EveryShippedBmpIsAccountedFor`
makes a new generator declare which side it is on: `spell_particle_fallback.bmp`
(drawn additively, alpha provably unsampled — its cosine falloff lives in RGB,
which is what an additive blend samples), `detail_atlas.bmp` (**B410**: 24-bit on
scope, not a measured reason; both consumers alpha-BLEND it, so converting it
is a look change), and `rain_streaks.bmp` +
`snow_flakes.bmp` (no consumer). Since T427 **every one of
the 72 shipped BMPs is now byte-reproducible from its generator** and
`NOT_REPRODUCIBLE` is empty. `generate_textures --report` prints the census.

**A 32-bit entry is not free**: an ETC1 256x256 entry with mips is 43,704
bytes and its RGBA8888 form is 349,524. T411
(board B287) took that back by fixing the shaders instead of the payload: the
three alpha-BLEND shaders (`SOLID3D_FS`, `UI2D_FS`, the particle point sprite)
sample the ETC1 `_alpha` companion on texture unit 1, bound in one place,
`COpenGLES2Driver::bindAlphaCompanionUnit()`, with
`ConstrainedTextureCache` recording the pairing on the texture itself. The
pack's `--rgba8888-texture` list is **down from 60 entries to one**. That one is
`ui_atlas`, and not for a single-sampler reason: a 1px UI `drawRect` minifies the
atlas 4:1 and selects companion mip level 2, where ETC1 decodes the isolated
`white` texel as 217/255 and dims the rect by 15%. **A new `--rgba8888-texture`
override is a ctest FAIL** (`TextureAlphaCensus.NoPackEntryNeedsTheRgba8888Payload`);
a consumer that genuinely cannot take a second sampler needs an entry in that
test's `kInBandAlpha` list naming the consumer and the reason.

A generator must be **bit-identical across optimisation levels** — a shipped
asset whose bytes depend on `CMAKE_BUILD_TYPE` makes `TexturesReproducible` a
coin toss (board B268: signed-`int` overflow UB in two noise hashes). Keep new
generator arithmetic defined.

## The eqemu lane

**eqemu runs on dedicated hosts, not here: 5 lanes** (B1008,
2026-10-08; `docs/decisions.md`). `scripts/eqemu/env.sh` is the one place that
knows: `EQEMU_LANE=1..5` picks the eqemu host and its paired official-client
oracle host (lane 1 = `172.16.200.99`, the default; lane 5 = ThinkPad, B1055), every docker/compose/`.env`
access goes through `eq_remote` (ssh to `EQEMU_SSH`), and `AKK_DIR` is the
akk-stack path ON that host. `EQEMU_HOST=local` drives a stack on this machine.
A card names its LANE, never an address. Each lane's exclusive lock lives on its
own VM — take it with `eqemu_lock_take`, never by touching a path by hand. All of `scripts/eqemu/*.sh` is sourced from
this file; see `refactor/EXECUTOR_PROTOCOL.md` for the lock discipline.

### The scenario lane — a live verification that exits with a code

**T410 (board B138/B139): the exit code is the result** of a live verification.

```bash
# One scenario, headless, against a running eqemu:
./build/bin/willeq -c config.json --scenario zone_in --timeout 120

# The whole rig -- lock, up.sh, snapshot, run, restore with the world DOWN,
# down.sh, release -- with the client's exit code passed through:
bash scripts/scenario.sh zone_in --timeout 120 --unit T4xx

# The restore's own regression net (T493) -- run it after ANY change to the
# restore path; it takes the lock itself and leaves the stack down:
bash scripts/scenario.sh --selftest --unit T4xx

# The readiness net (T497): the first expects 0, the second 70
bash scripts/eqemu/ready_selftest.sh --unit T4xx
bash scripts/eqemu/ready_selftest.sh --unit T4xx --legacy
```

**"Restore with the world DOWN" means STOP THE CONTAINER** (T493, board B784),
because **the zone writes its in-memory copy of a character back when the zone
SHUTS DOWN, not when the client disconnects.**

**A PORT PROBE IS NOT LIVENESS** (T497, board B797). A host-side `connect()` on
5998/9000 answers with the server fleet at ZERO processes: those are PUBLISHED
container ports, so `docker-proxy` binds them on the host for as long as the
CONTAINER runs, whatever is (not) listening inside it. The probe was also testing
a transport no client uses — TCP 5998 is the loginserver's **servertalk**
listener and TCP 9000 is world's zone link, while the Titanium client logs in
over **UDP** 5998/9000.

There is now ONE readiness definition, **`eqemu_stack_ready`** in
`scripts/eqemu/env.sh` (T497): for loginserver and world, the process is running
AND its port answers from INSIDE the container's netns (`eqemu_service_ready` =
`server_process_running` + `eqemu_container_port_open`, a `/dev/tcp` connect via
`docker exec`, falling back to `/proc/net/tcp`). zone is deliberately not in it
— its port is UDP and assigned by world, so `up.sh` polls the zone's own bootup
log line. **Nothing may gate on `eqemu_host_port_reachable`**; it proves
reachability only, and `status.sh` prints exactly that distinction.
`scripts/eqemu/ready_selftest.sh` is the net: it brings the stack up, kills the
fleet AND the container's supervisor, checks the container is still Up with both
host ports still answering, and requires `eqemu_stack_ready` to be FALSE.

**Never write a container-side `pkill` by hand** (T497, board B798). Use
`eq_container_pkill <signal> <ere>` (`env.sh`), which refuses a pattern that
matches its own `bash -c` wrapper — the exact defect B784 turned out to be (the
exec killed itself with 143 and the rest of the chain never ran). The safe
shapes are `'/bin/(loginserver|world|zone)( |$)'` and the bracket trick
`'[s]pire-launcher'`. The one deliberate exception is `restore_selftest.sh`'s
`--legacy` branch, which reproduces the defect on purpose.

**The restore has ONE implementation, `scripts/eqemu/restore_lib.sh`** (T493),
sourced by `scripts/scenario.sh`, `scripts/llmbench/teardown.sh` and
`scripts/llmbench/record.sh` — `eqemu_world_stop` (`docker stop` the
eqemu-server container; mariadb is a separate container and stays up) then ONE
statement batch, not one `UPDATE` per row. **Do not inline another copy**, and do not go back to `pkill`.
`scripts/eqemu/restore_selftest.sh` is the net: it logs a character in, makes the
snapshot and the world disagree on purpose, restores, runs `down.sh`'s SIGTERM
step and only THEN diffs, so the write-back is caught rather than waited for. Its
`--legacy` flag runs the pre-T493 restore and fails, which is how the net is
known to test something.

**Exit-code taxonomy** (`include/client/scenario/scenario_runner.h`; nothing else
may be returned from a scenario run):

| code | meaning |
|---|---|
| 0 | pass — the scenario asserted and the assertion held |
| 1 | fail — the scenario ran and its assertion failed |
| 2 | timeout — zoned in, but no verdict inside `--timeout` (**also what an unfinished run reports**, so an external `/quit`, a SIGTERM or a crash can never masquerade as success) |
| 3 | could-not-start — never reached a zone (server down, bad login, unknown scenario name) |

**Outside `--scenario`** a run returns 0 for a clean `/q`, headless `/camp`,
`/camp desktop` (B1005), window close or SIGTERM, and **4** (`kClientFatalExit`) when it ended on a FATAL (loader
thread or `LoadZoneSources()`, `fatal_exit.h`; B918 — 1 stays bad arguments /
failed initialize); a `--scenario` run
never leaves 0..3 (a FATAL before a verdict reads as 2).
The one decision is `eqt::scenario::ProcessExitCode()`.

`scripts/scenario.sh` adds operator codes on top: 64 lock held by another session
(or a bad option), 65 the server would not come up, 66 the snapshot did not
restore. `--selftest` adds 67 (the client never reached the world), 68 (the
restore was slower than its budget) and 69 (a world/zone process was still alive
when the restore finished).

**How it hangs off the client.** `--scenario` forces Automated mode, and the
runner is registered on `AutomatedMode::setAutomationCallback` — the per-tick
hook that had zero registrants in the whole tree until this (board B139). The
callback runs on the GAME THREAD (`Application::processInput()`), one
`ScenarioRunner::Tick(dt)` per ~60 Hz tick; returning `false` is
`AutomatedMode`'s own contract for "stop", so the client unwinds through
`isQuitRequested()` exactly like `/camp` and never calls `exit()` from inside a
tick.

**The runner owns the pre-flight, not the scenarios.** Until `IsFullyZonedIn()`
is true it does not call `Step()` at all; if that never happens inside the start
deadline the verdict is 3. So "the server was down" can never be reported as a
scenario failure.

**Scenarios poll; they do not subscribe.** `GameState::events()` (which
`AutomatedMode` forwards) and the renderer-facing `GameStateBridge` are two
different streams, and the interesting landings — buffs via T348's `OP_Action`
path — go to the bridge, which a headless client does not drain. Polling reads
the same authoritative state either stream was published from.

The registered scenarios are in `src/client/scenario/scenario_registry.cpp`
(`kScenarios`) — read that table, not this paragraph, for the live list. Today:
`zone_in` (login → zone → `IsFullyZonedIn()` with a non-empty zone name),
`two_client_survive` (a second player appears AND both hold for
`--scenario-hold` seconds, default 300), `cast_lands` (prep, `/cast 1`, then the
buff actually reaching `BuffManager`), and T494's `equip_observed` /
`equip_actor` / `equip_actor_return` pair-plus-return. The partner for a
two-client scenario is a **peer process**, launched by `scripts/scenario.sh`
running the partner scenario on the second account — the client never forks a
second client.

UIGAP-live(2) UI legs: `scenario_ui_*.cpp`; scenario.sh sources `<name>.sidecar.sh`/`.post.sh` (DB half).

The state machines are pure logic over `IScenarioWorld`
(`include/client/scenario/scenario.h`), so
`tests/test_scenario_state_machines.cpp` runs every case with no client, no
server and no network. A scenario needing something new adds a method there, and
`ApplicationScenarioWorld` is the only implementation that touches `EverQuest`.
Trace goes to the `MOD_SCENARIO` log module, pinned to INFO by the lane itself so
a `--log-level` cannot silence the verdict.

### Video capture (temporal evidence — attack swings, casting, zoning, particles)

A temporal sequence needs video, not burst-captured screenshots.

```bash
# Dump numbered PNG frames while connected/zoned (render-thread hook, off by
# default — a normal run with no --dump-* flags touches none of this):
./build/bin/willeq -c willeq.json \
    --dump-frames-dir /tmp/capture --dump-every 1 --dump-max-frames 600

# Encode the frame directory into a reviewable mp4 with MEASURED (not
# assumed) fps:
bash scripts/ci/encode_capture.sh /tmp/capture <UNIT_ID> [output_dir]
```

- `--dump-frames-dir <dir> --dump-every <N>` (T64) writes a PNG via the existing
  `IrrlichtRenderer::saveScreenshot()` path every Nth rendered frame, from
  `Application::render()` right after a successful `processFrame()`. An empty
  dir or `--dump-every 0` is a complete no-op.
- `--dump-max-frames <N>` (default 600) bounds total frames written — the Orange
  Pi's `/tmp` tmpfs is only ~242 MB, and unbounded dumping is a footgun there.
- `scripts/ci/encode_capture.sh` computes the **effective fps from the frames'
  own file mtimes** — never assumes 30/60 — so the video plays back at true
  elapsed wall-clock time. It also writes `<UNIT_ID>_capture.txt` beside the
  video with the measured fps/frame-count/real-time span; keep that file with
  the video.
- **Capture fps is an artifact of the capture mechanism, not a performance
  measurement** (P12). **Never draw a performance conclusion from a
  capture video.**
- Verify frames are real images before encoding — `saveScreenshot()` has
  returned garbage under the surfaceless path before (P10, P20; `review/T64/`).

### Linux Capabilities

```bash
# Required for reliable zone loading
sudo setcap cap_net_raw,cap_net_admin+ep ./build/bin/willeq
```

- `CAP_NET_RAW`: raw packet capture (diagnostic feature).
- `CAP_NET_ADMIN`: `SO_RCVBUFFORCE`, to set the UDP receive buffer above the
  kernel limit.

Without `CAP_NET_ADMIN` the zone-in burst overflows the socket buffer
(`net.core.rmem_max`) and the load hangs at 40%.

On the Pi: the CI-published binary (NFS) carries no capability, and `setcap`
does not stick on NFS or on the Pi's `/tmp` (tmpfs, `nosuid`). Copy it to the
Pi's ext4 home and `setcap` it there (`scripts/pi_rendered_zone_sweep.sh`,
`refactor/B854/RENDERED_SWEEP.md`).

### Orange Pi (ARM) Cross-Compilation

The client cross-compiles for Orange Pi One (Allwinner H3, ARMv7-A Cortex-A7,
Mali 400 GPU). **Target platform**: Ubuntu Noble (24.04), mainline kernel, Lima
open-source driver.

- **GPU rendering**: native OpenGL ES 2.0 via custom `COpenGLES2Driver`
  (extends Irrlicht's `CNullDriver`); 6 built-in GLSL ES 1.0 shader programs, no
  fixed-function pipeline. No desktop GL → GLES translation, native ETC1
  textures (real 6:1 savings via `GL_ETC1_RGB8_OES`), and the shared path for
  the planned Android port. See `src/client/graphics/CLAUDE.md`.
- **Display**: DRM/KMS direct rendering without X11 (`--drm`).
- **Single-tenant SBC lanes** (`docs/ci.md` "The aarch64 SBC lanes"): `PI_LANE=1` Orange Pi, `2` Rock64, `3` ROCK 4D, `4` Radxa A7S, `5` Orange Pi Zero; table in `scripts/pi/env.sh`; name the lane, never an address.
  `pi_lock_take <owner>` before any run, `pi_lock_release <owner>` after (on the device; `lock_selftest.sh`).
- **GL driver: the shipped LLVM-less Mesa prefix** (Will 2026-09-30, B935; `docs/ci.md` "The shipped Mesa
  prefix"): Pi rigs run `ci-arm/<sha>/bin/willeq-nollvm` (absolute DT_RPATH; `$ORIGIN` is dropped under setcap)
  by default, `WILLEQ_MESA=system` opts out, and every run proves the loaded libgallium from its maps.
- **Build**: `docker/Dockerfile.arm-noble` + `scripts/build-arm-noble.sh` (sets
  `-DEQT_GLES2=ON`) → `build-arm-noble/bin/willeq` (ELF 32-bit ARM, ~10 MB
  stripped).

```bash
bash scripts/build-arm-noble.sh

# On the Pi (DRM/KMS, GLES2 — the only GLES2 windowing path on ARM)
./willeq -c config.json --drm --renderer gles2 --constrained orangepi -r 800 600

# The GLES2 path on the x86 dev VM (llvmpipe via EGL surfaceless; H01)
bash scripts/build-gles2-x86.sh
./build-gles2/bin/willeq -c config.json --renderer gles2
```

There is **NO X11+GLES2 path** (H01, 2026-07-23) — the ARM Irrlicht patch
compiles only the DRM/FB device. On x86 the forked device
(`docker/irrlicht-drm-x86/`) falls back to Mesa's surfaceless EGL platform
(llvmpipe), which is how CI runs the real GLES2 driver headlessly.

**Legacy platforms**: desktop GL 2.1 on Noble is retired (P32, `docs/decisions.md`
2026-07-28); `--no-gles2` is gone.

**DRM input handling**: the DRM device
(`docker/irrlicht-drm/CIrrDeviceFB.cpp`) reads keyboard/mouse via Linux evdev
(`/dev/input/event*`). VT keyboard processing is disabled via `KDSKBMODE(K_OFF)`
to prevent the console layer from intercepting Ctrl+key combinations.

## Dependencies

Required: OpenSSL, zlib, fmt, GLM (header-only), cereal (header-only:
`libcereal-dev` / brew `cereal`; configure fails without it, B1038), jsoncpp
Optional:
- Boost (waypoint pathfinding)
- librecast-dev (navmesh pathfinding via Recast/Detour)
- A locally-built patched GLES2 Irrlicht (3D graphics rendering) — see "Build
  Commands" above. Not an apt package: `libirrlicht-dev`/`libxxf86vm-dev`
  (desktop-GL Irrlicht) are no longer used (P30).
- freerdp3-dev, libwinpr3-dev (native RDP streaming)
- Audio uses header-only libraries: miniaudio.h (output), tsf.h + tml.h (MIDI
  synthesis), dr_wav.h (WAV loading)

## Architecture

### Threading Model (Batch D)

The application runs two threads, decoupled via a thread-safe event/intent
bridge:

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

- **GameStateBridge** (`include/client/bridge/game_state_bridge.h`) —
  thread-safe queue pair using a swap-vector pattern. Game thread pushes events
  (state changes), renderer pushes intents (user actions). D24 coalesces
  EntityMoved events per entity and PlayerPositionChanged intents.
- **IrrlichtBridge** (`include/client/bridge/irrlicht_bridge.h`) — translates
  game events into IrrlichtRenderer calls
  (switch over the 108 `GameEventType` enumerators; T264: every event has a
  publisher and a consumer, pinned by `test_reachability_sweep`)
- **ConsoleBridge** (`include/client/bridge/console_bridge.h`) — logs events to
  console in headless mode
- **Event types** (`include/client/state/event_types.h`; the `GameEvent` variant
  and its payload headers are `event_bus.h` + `event_payloads_*.h`)
  — 108 event types covering all game state changes
- **Intent types** (`include/client/events/renderer_intents.h`) — 49 intent
  structs in the `RendererIntent` variant, pinned by `test_reachability_sweep`

**Key separation**: EverQuest communicates with the renderer only through the
bridge — no `m_renderer` member, no `GetRenderer()` accessor. Application owns
the renderer and wires it to EverQuest via bridge pointers. Player movement
flows renderer→game exclusively via the `PlayerPositionChanged` intent;
inventory UI state flows game→renderer exclusively via enriched
`InventorySlotChanged` events; bridge payloads are typed (no `void*`). The
graphics-facing game code is concentrated in
`src/client/bridge/bridge_component.cpp` (the boundary TU);
`src/client/core/everquest.cpp` itself has only two graphics includes. K16 closed
the last structural seam: `InitGraphics` takes
`EQT::Graphics::IZoneLoadSurface*` (`include/client/graphics/zone_load_surface.h`),
so game logic names no renderer class. The asset loaders still live in the
`EQT::Graphics` namespace — that is naming only and was left deliberately (the
closed B006). **Do not "fix" coupling ad hoc.**

### Connection Flow

Three stages, each with its own connection manager:
1. **Login Server** (`m_login_connection_manager`) — authenticates, gets the
   server list
2. **World Server** (`m_world_connection_manager`) — character selection, zone
   info
3. **Zone Server** (`m_zone_connection_manager`) — actual gameplay

### Core Components

**Application** (`include/client/application.h`, `src/client/application.cpp` +
the `application_*.cpp` TUs, J06 and B888) — top-level orchestrator: owns renderer, bridge,
game thread and loading thread; spawns the game thread running the EverQuest
tick loop; the main thread runs the render loop (or sleeps headless) and manages
the zone loading thread (GL context transfer, snapshot handoff).

**EverQuest** (`include/client/eq.h`, split across `src/client/core/everquest.cpp`
+ domain TUs under `src/client/core/`, `src/client/action/` and the extracted
`src/client/<family>/` components, Batch K) — game state and network logic.
`everquest.cpp` itself holds only the core: lifecycle, connection setup, the zone
packet-receive callback. Everything else (zone-stream dispatcher, zone entry,
movement, entities, chat, merchant, bridge, …) lives in domain TUs — **consult
`docs/module_map.md` to find the right file**. All Titanium opcodes are enums:
`TitaniumLoginOpcodes`, `TitaniumWorldOpcodes`, `TitaniumZoneOpcodes`. Zero
graphics dependencies.

**IrrlichtRenderer** (`include/client/graphics/irrlicht_renderer.h`,
`src/client/graphics/renderer/` + eleven component directories, Batch L)
- Its own five TUs (`irrlicht_renderer.cpp`, `renderer_init.cpp`,
  `renderer_entities.cpp`, `renderer_event_receiver.cpp`, B998's
  `renderer_pre_zone.cpp`) live in
  `src/client/graphics/renderer/`, beside the free-function
  `software_cursor_texture.cpp`; everything else was extracted into eleven
  components, each `class EQT::Graphics::<Name>Component` with its own directory
  under `src/client/graphics/` and `include/client/graphics/`: **RdpComponent**
  (`rdp/`), **Eqe1ArchiveComponent** (`eqe1/`), **OverlaysComponent**
  (`overlays/`), **DiagnosticsComponent** (`diagnostics/`),
  **PlayerCameraComponent** (`player/`), **SlashCommandsComponent** (`slash/`),
  **SimBridgeComponent** (`sim/`), **ZoneStreamComponent** (`zone_stream/`),
  **ZoneLifecycleComponent** (`zone_lifecycle/`), **ZoneLoadComponent**
  (`zone_load/`) and **FrameComponent** (`frame/`)
- `EQT::Graphics::RendererContext`
  (`include/client/graphics/renderer_context.h`) is the renderer's
  `ClientContext`: shared members bound by reference plus a pointer to each
  sibling component (`ctx_.Frame()`, `ctx_.ZoneLoad()`, …). A component reaches
  shared state as `ctx_.member`, a member OWNED by another component as
  `ctx_.<Owner>().<accessor>()` (L13's pairwise rule: a member two components
  share moves to the one that writes it), and anything still on the class as
  `owner_.member_`. The count of remaining `owner_.` handles drifts with normal
  work — **re-measure, do not quote a number**. See the closed **B050**.
- Lifecycle entry points (`initLoadingScreen`, `initDeferredGraphics`,
  `processFrame`, `shutdown`, `run`) stay on `IrrlichtRenderer`: `Application`
  and `IrrlichtBridge` must not learn a component exists.
- Consult `docs/module_map.md` for which TU holds which body.

**Network Stack** (`include/common/net/`, `src/common/net/`) —
`DaybreakConnection` (UDP reliable protocol: sequencing, fragmentation, CRC,
compression), `Packet` with `StaticPacket`/`DynamicPacket`,
`DaybreakConnectionManager` over UdpTransport.

**Pathfinding** (`include/client/pathfinder_*.h`) — `IPathfinder` (interface),
`PathfinderNull` (fallback), `PathfinderNavmesh` (Detour, needs
`EQT_HAS_NAVMESH`), `PathfinderWaypoint` (Boost A*, needs `EQT_HAS_WAYPOINT`).

**Maps** (`include/client/hc_map.h`, `include/client/raycast_mesh.h`) — `HCMap`
(zone maps, V1/V2) and `RaycastMesh` (AABB tree for collision/LOS).

**Combat** (`include/client/combat.h`) — `CombatManager`: auto-attack,
targeting, looting, abilities.

**Trading** (`src/client/trade_manager.cpp`) — `TradeManager`: player trading,
item/money exchange, trade state machine.

**Spells** (`src/client/spell/`) — `SpellManager` (casting, gem state,
cooldowns, memorization). The only path a player reaches: gem click / `/cast` →
`beginCastFromGem()` → `validateCast()`. Its bodies live in six TUs; see
`docs/module_map.md`, and the header banners name the defining TU.
`SpellTypeProcessor` (T324) and `SpellEffects` (T327, Will's ruling) were
DELETED (zero callers, stub handlers; the tombstone says why). **Do not rebuild
either**; the server is authoritative and reports every landing per target. Both
deletions share one tombstone in `src/client/core/everquest.cpp`.

**Skills** (`include/client/skill/`) — `SkillManager`, `SkillData`,
`skill_constants.h` (ids, names, categories, animation mappings).

### Packet Structures

Titanium-specific packet structures are in `include/common/packet_structs.h`,
binary-compatible with the Titanium client protocol.

### Logging

**All new logging code MUST follow `docs/standards/debug_logging_standards.md`.**
Default (level 0/NONE) is quiet — errors still reported. Seven levels: NONE,
FATAL, ERROR, WARN, INFO, DEBUG, TRACE. Module-based filtering (NET, ENTITY,
GRAPHICS, …), runtime configurable via CLI, config file or signals. Output
format: `[TIMESTAMP] [LEVEL] [MODULE] message`. Legacy logging in
`include/common/logging.h` does not follow these standards and should be
migrated when touching existing code.

## Test Organization

Tests are in `tests/`, one executable per suite, each linking only the minimal
sources it needs — e.g. `test_packet.cpp` (packet serialization),
`test_titanium_opcodes.cpp`, `test_packet_structs.cpp` (binary layout),
`test_integration_network.cpp`, `test_formatted_message.cpp` (NPC dialogue).
Audio and graphics integration tests are documented in their own CLAUDE.md files.

**Session replay oracle** (Batch R, `docs/plans/session_replay_oracle.md`):
record a real play session with `--record-session data/captures/session.sleq`,
then `test_session_replay` replays it into a headless EverQuest and asserts game
state against a golden snapshot (self-created on first run). A synthetic-session
mode keeps the test green without a recording. Protocol-level pcap tests
(`test_daybreak_*`, `test_pcap_*`) skip unless a capture exists at
`data/captures/session.pcap` (or `WILLEQ_PCAP`).

## Key Patterns

- **Bridge events/intents**: game state changes are published via
  `m_bridge->pushEvent()`; renderer user actions arrive via
  `m_bridge->drainIntents()`. **Never call renderer methods directly from
  EverQuest.**
- **Zone loading**: Application creates a `ZoneLoadSnapshot` (immutable copy of
  game state), starts a loading thread that owns the GL context, builds GPU
  resources, then signals completion. The game thread keeps processing network
  packets during loading.
- Zone connection requires specific packet sequencing — see
  `CheckZoneRequestPhaseComplete()` and the `m_*_sent` / `m_*_received` flags.
- Movement updates require `SendPositionUpdate()` to sync with the server.
  Entities are tracked in `m_entities`, keyed by spawn_id.
- Doors are tracked by `DoorStateManager`
  (`include/client/door_state_manager.h`, reached as `GameState::doors()`) —
  `doors_`, a `std::map<uint8_t, DoorState>` keyed by door id. There is no
  `m_doors` member on `EverQuest`. <!-- RETIRED-OK: naming the member that is gone is the point of this sentence -->
  Since T376 (B141) the same manager also owns the pending-click set
  (`pendingClicks_`, `addPendingClick()` / `isClickPending()`); the duplicate
  `EntitiesComponent::m_pending_door_clicks` and `ClearPendingDoorClicks()` <!-- RETIRED-OK: tombstone for the two names T376 deleted -->
  were deleted. `ZoneProcessMoveDoor` must ask `isClickPending()` BEFORE
  `setDoorState()` consumes the entry.
- **Friend class pattern**: `CombatManager` and `TradeManager` are friends of
  `EverQuest` for internal access, and **they are the only two** (T366, board
  B038). No extracted component is a friend and none holds an `EverQuest&`
  transition handle. Every `owner_` outside `graphics/` is gone; every `owner_`
  inside `graphics/` is a different pattern — Batch L's `IrrlichtRenderer&
  owner_` in the eleven renderer components (the RendererContext pattern) and a
  `ZoneShaderManager*` back-pointer in `zone_shader_callbacks.h`. **If something
  new needs `EverQuest`, it goes on `ClientContext` or a context service, not on
  a handle.**
- **Inventory slot numbering — TWO numberings, one convention** (T494, board
  B796, `docs/decisions.md` 2026-09-17). fable's inventory model, every
  `MoveItemIntent`, `SendMoveItem` and the whole wire are **Titanium**-numbered
  (ammo 21, general 22-29, cursor 30) because that is what the server serialises
  to a Titanium client. `inventory.slot_id` in the database, every eqemu log line
  and everything Spire shows are **server/RoF2**-numbered (waist 20, **power
  source 21 — not sent to this client at all**, ammo 22, general 23-30, cursor
  33, general bags `4010 + 200*bag + idx`). **Everything a user types or reads is
  `inventory.slot_id`**, and the conversions live in
  `include/client/inventory/inventory_constants.h`: the possessions pair
  `parseUserSlot()` in / `describeSlot()` out (both ported from the server's own
  `titanium.cpp`) and the corpse pair `parseCorpseSlot()` in /
  `corpseSlotToServer()` out (B810, #199). **Do not add a third ad-hoc
  conversion at a call site**, and do not "fix" a model-side number to match a
  message.
- **`EntityAppearance::equipment[]` is a MATERIAL / IT model number, never a
  database item id** (T498, board B755/B823). Slots 0-6 carry the armour
  material, slots 7 and 8 the weapon's `IT<n>` number, and the rule is the
  server's own: `Mob::GetEquipmentMaterial()` is `atoi(&item->IDFile[2])`. It
  reaches the client three ways and all three agree — the spawn parse,
  `OP_WearChange`, and for the LOCAL player
  `UpdatePlayerAppearanceFromInventory()`, which reads
  `ItemInstance::idfileModel` off Titanium static field 3. Until T498 the local
  player's own weapon slots carried the DB item id and every render site resolved
  it through `data/item_models.json`, a hand-carried table that disagrees with
  the server on **2,128 of its 3,661 rows**. **Do not put an item id in
  `equipment[]`, and do not add an item-id lookup to a render site**:
  `EquipmentModelLoader::getModelIdForItem()` survives only for the two offline
  tools that genuinely hold item ids (`tools/model_viewer`,
  `tools/hwcaps/ui_inventory_capture`). The drift is enumerated in
  `data/item_models_drift_baseline.txt` and ratcheted both ways by the
  `ItemModelsMatchServer` ctest test, which compares it to the committed export
  `data/item_idfile.tsv` (`scripts/eqemu/export_item_idfile.sh`, 117,944 peq
  rows) and so needs no live mariadb. Regenerating the file is open — board
  **B827**, Will's call, because it changes what the tools draw.
- **BSP tree** (`include/client/zone_bsp.h`): zone spatial data shared between
  game state (water detection, zone lines) and renderer (PVS culling). No
  graphics dependencies.

## Configuration

**Main config** (`willeq.json`): `{"eq_client_path": "/path/to/EverQuest"}`,
optionally with `"rdp": {"enabled": true, "port": 3389}`.

**Spell effects** (`config/spell_effects.json`, optional): JSON particle presets
with hot-reload; 3 tiers (global → per-style defaults → per-spell overrides)
covering resist colours, spawn rates, lifetimes, motion types and blend modes.

**Per-character UI state IS persisted, and it is the only thing that is** (T432,
board B275). The client writes `<cache dir>/ui/UI_<Char>_<server>.ini` — the
official client's own filename, section names and `Show=0/1` keys — and re-reads
it at zone-in. It is a generated file, so it goes through
`ResolveCachePathForWrite()` (T402's rule) and never dirties the checkout.

What it holds is what Will ruled user-changeable (`docs/decisions.md`
2026-09-07): the seven toggleable windows (`[InventoryWindow]`, `[SpellBookWnd]`,
`[SkillsWindow]`, `[GroupWindow]`, `[PetInfoWindow]`, `[BuffWindow]`,
`[CombatAbilityWnd]`); the camera — `[Camera] View=` (one of the official
client's six cameras) plus the two user cameras' azimuth/pitch/distance/height,
with T432's old `[Camera] Mode=` still read and mapped explicitly; and
`[OptionsWindow] Show=` plus an `[Options]` section holding `Music`,
`SoundVolume` and `ClipPlane` (T434, on the oracle's 0..20 slider scale:
`eqclient.ini` `Music=18`/`SoundVolume=10` displays 90 %/50 %) and, since
B430/B433, every RENDERER-owned Options setting under its `eqclient.ini` key
(table: `include/client/graphics/ui/options_settings.h`; per-control
decisions: `options_classification.cpp`, an unclassified row is a ctest
FAIL). SERVER-owned ones (anon, roleplay, autosplit, auto-consent) are never
written locally: `GameOptionIntent` out, `GameOptionChanged` back.

Window POSITIONS are static by design (`UILayout` computes every rect from the
resolution) and key bindings are global (`config/hotkeys.json`; the Options
Keys page saves a user copy to `<cache dir>/hotkeys.json`, loaded in its
place — B433), so neither is
per character. An ABSENT `Show=` key keeps the default, which is `defaults.ini`'s
own rule (T368), so a fresh character writes no file until it changes something.
Since T467 (board B560) an identity change FROM a character resets every
persisted field to its default before the new file is read, so nothing carries
over; the FIRST identity of a run is not reset, so a volume the audio manager
published before zone-in is kept. Chat colours persist under `[TextColors]`
(B993). **Chat timestamps and scrollback are NOT persisted** (scope).

The feature is four files: `include/client/graphics/ui/ui_preferences.h`,
`src/client/graphics/ui/ui_preferences.cpp` (the format, device-free),
`src/client/graphics/frame/renderer_frame_preferences.cpp` (load at zone-in,
save on change and at `/camp`) and
`src/client/graphics/frame/renderer_frame_options.cpp` (every field with a
SETTING behind it: intents to the game thread, or a renderer call, and
`FrameComponent::applyOptionsSetting()` is the one body the click path, the
load path and `/option`/`/filter` all go through). **A new Options setting is
one row in `options_settings.cpp` + its arm in `applyOptionsSetting()` + its
row in `options_classification.cpp`.** There is deliberately **no
per-toggle save hook**: `FrameComponent::syncUiPreferences()` compares the whole
collected value against the last one it wrote, so a newly persisted field cannot
be added and then forgotten at one call site. **A new persisted field goes in
`UiPreferences` + the `kWindows` table (or the `kOptions` table, the same
member-pointer device for the non-boolean `[Options]` keys) + the binding,
nowhere else.**

**Volumes round-trip** (T439, board B432). The two volume intents carry an
ABSOLUTE step 0..20 (`include/client/audio/volume_steps.h`, the oracle's own
unit) and `BridgeComponent::PublishVolumesChanged()` publishes what the audio
manager actually holds back on `GameEventType::VolumesChanged`, from all four
sites that move a volume: the config apply, the two intents, and `/musicvolume`
/ `/effectsvolume`.

`config/chat_settings.json` and `config/ui_settings.json` — the DELETED
`WindowManager`'s saved state — described draggable windows the client no longer
has and were read by nothing, so T402 deleted them (board B245). Every shipped
`config/*.json` that remains is named by code, and
`DataPathRouting.EveryShippedConfigFileIsNamedByCode` keeps it that way.

Audio configuration is documented in `src/client/audio/CLAUDE.md`.

## Graphics Integration

Graphics is enabled by default (`EQT_GRAPHICS=ON`). Requires EQ Titanium client
files for zone geometry and character models. See `src/client/graphics/CLAUDE.md`
for rendering details, and `include/client/graphics/ui/CLAUDE.md` for UI and key
bindings.
