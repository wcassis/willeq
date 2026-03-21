# Refactoring Progress

Master tracker for the willeq architecture refactoring effort.

## Process

Each unit follows: **Plan -> Implement -> Review -> Commit**

1. Write plan with numbered steps and acceptance criteria to unit file
2. Implement, re-reading the plan file during work
3. Review: re-read plan, check each step, note deviations in review section
4. Commit only after review passes
5. Update this table

## Completed Batches

All completed batch plans and unit files are in `refactor/completed/`.

| Batch | Description | Status |
|-------|-------------|--------|
| A | Architecture (BackgroundWorkQueue, unified priority, entity pipeline) | done |
| B | Budget/Safety/Cleanup (dead code, budget guards, equipment textures) | done |
| C | Post-audit remaining issues (thread pool, GL safety, mutex, dead code) | done |
| L | Loading Thread Separation (sequential zone loading, GL context transfer) | done |
| S | Startup Validation & Pre-allocation (config validation, fail-fast) | done |
| D | Game State Threading (event bus, bridge, intents, thread separation) | done |
| U | UI and Text Rendering Overhaul (bitmap font, batched UI, static layout) | done |
| T | Texture Loading Consolidation (single decoder, getOrLoad/uploadTexture) | done |

---

## Batch E — ETC1 Flat Pack Textures (GLES2)

Pre-compress all EQ textures to ETC1 offline, package in flat pack container files (`.eqe1`).
Replaces BMP/DDS decode pipeline with direct `glCompressedTexImage2D` upload on GLES2.
Full design: `docs/plans/full_etc1_textures.md`.

### Phase E1 — Offline Tooling

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| E01 | EQE1 binary format definition + header structures | done | |
| E02 | EQE1 builder tool — S3D → ETC1 flat pack converter | done | |
| E03 | Batch conversion script + validation | done | |

### Phase E2 — Runtime Loader

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| E04 | EQE1Loader class — mmap reader with name lookup | done | |
| E05 | Compressed upload path in ConstrainedTextureCache | done | |

### Phase E3 — Integration

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| E06 | Configuration — `--eqe1-path` CLI flag + config field | done | |
| E07 | Zone load wiring — open EQE1 files, pass to cache | done | |
| E08 | Entity texture wiring — chr/obj EQE1 loading during entity prep | done | |

### Phase E4 — Validation and Cleanup

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| E09 | End-to-end validation on Orange Pi (memory, visual, performance) | done | |
| E10 | Remove zone atlas UV workaround path | done | |

### Phase E5 — Composite Entity Textures

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| E11 | entity_composite_builder tool | done | |
| E12 | Composite mesh building in RaceModelLoader | done | |
| E13 | Runtime composite entity loading and rendering | done | |

---

| D09 | IrrlichtBridge handles entity events | done | `f97ff45` |
| D10 | IrrlichtBridge handles chat, combat, player stat events | done | `69589ce` |
| D11a | IrrlichtBridge handles inventory + currency events | done | `69589ce` |
| D11b | IrrlichtBridge handles loot + vendor events | done | `69589ce` |
| D11c | IrrlichtBridge handles bank + trade events | done | `69589ce` |
| D12 | IrrlichtBridge handles door, group, pet, spell, skill events | done | |
| D13 | IrrlichtBridge handles world/environment + zone lifecycle events | done | |

### Phase 4 — Intent Handling

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D14 | Movement intent: renderer posts PlayerPositionChanged, game thread consumes | done | |
| D15 | Interaction intents: target, door, loot, vendor, banker, trainer, world object, zoning, chat, read item | done | |
| D16 | UI intents: spell, buff, skill, loot actions, vendor buy/sell, bank, trade, pet, group, camp/quit, hotbar | done | |

### Phase 5 — Remove Direct Coupling

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D17a | Add missing entity event types + fix weapon skill state ownership | done | |
| D17b | Remove direct renderer calls from entity packet handlers | done | |
| D18a | Remove direct renderer calls from inventory/loot/vendor packet handlers | done | |
| D18b | Remove direct renderer calls from bank/trade/trainer packet handlers | done | |
| D19a | Remove direct renderer calls from small handlers (chat, combat, door, world, weather, etc.) | done | |
| D19b | Remove direct renderer calls from zone lifecycle functions | done | |
| D19c | Remove remaining packet handler and chat routing renderer calls | done | `76a86c1` |
| D20a | Remove callback lambdas + activate intent processing | done | `76a86c1` |
| D20b1 | Remaining Setup*Callbacks → intents (tradeskill, trainer, inventory) | done | `29e058e` |
| D20b2 | InitGraphics callbacks → intents + events (hotbar, group accept, HUD, buff fade, skill, chat link) | done | `a8bc154` |
| D20b3 | Chat window + collision map decoupling | done | `a962792` |
| D20b4 | Window pointer decoupling — EverQuest* + manager pointers removed from all UI windows | done | `556efbb` |
| D20b5 | Renderer lifecycle cleanup (bridge/loading/shutdown → Application ownership) | done | `846a6a3` |
| D20c1 | Move UpdateGraphics to Application (renderer->processFrame called directly) | done | `9323d27` |
| D20c2 | Convert per-frame polls to events (target HP, time of day, audio→player pos, volume→intents) | done | `c47767f` |
| D20c3 | Zone loading reads from snapshot, not live game state (bridge event queue handles delta) | done | `861f5f4` |
| D20c4 | Remove requestQuit renderer calls; hotbar config deferred to D20e | done | `ba9a849` |
| D20d | Move 30 renderer slash commands to renderer via RendererCommand bridge event | done | `bba5848` |
| D20e | Remove m_renderer from EverQuest | done | `0ce550a` |
| D20f | Move zone data loading to game state layer | done | `229e521` |
| D20g | Decouple RDP server from renderer | done | `86aedc4` |

### Phase 6 — Thread Separation

*D23 (Remove networkTickCallback_) deleted — already completed by Batch L (`58f0791`).*

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D21a | Split PreRenderTick — game-side tick vs render-side drain | done | `35c96f8` |
| D21b | Move game tick to dedicated thread with fixed-rate loop | done | `a26ba6a` |
| D21c | Thread-safe zone load handoff (snapshot + loading thread) | done | `a26ba6a` |
| D21d | Thread-safe quit/connect signaling between threads | done | `a26ba6a` |
| D22 | Synchronize bridge queues with proper threading | done | `a26ba6a` |

### Phase 7 — Cleanup and Optimization

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D24 | Optimize high-frequency events (entity position batching) | done | `60a2cf9` |
| D25 | Add console bridge implementation | done | `b94c825` |
| D26 | Update documentation and CLAUDE.md | done | `d774034` |

---

## Batch U — UI and Text Rendering Overhaul

Replace draggable window system with static-layout batched UI.
Full plan: `refactor/U_ui_overhaul.md`. Design analysis: `docs/future/ui_fixes.md`.

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| U00 | Publish initial state snapshot events (prerequisite for all panels) | done | |
| U01 | Bitmap font atlas + batched text renderer | done | |
| U02 | UI atlas texture | done | |
| U03a | UILayout — fixed screen region definitions | done | |
| U03b | UIRenderer — batched quad drawing API (atlas + fallback) | done | |
| U03c | /newui toggle + render hook in IrrlichtRenderer | done | |
| U03d | Player status + target info panels (proof of concept) | done | |
| U04 | Chat panel | done | |
| U05 | Inventory and item slots | done | |
| U06a | Hotbar panel (10 slots + cooldowns + key labels) | done | |
| U06b | Spell gem panel (8 gem slots) | done | |
| U06c | Buff bar (icon row + duration text) | done | |
| U06d | Casting bar (progress bar + spell name) | done | |
| U06e | Group panel (5 member HP bars) | done | |
| U06f | Pet panel (HP bar + command buttons) | done | |
| U06g | Spellbook popup | done | |
| U06h | Skills popup | done | |
| U06i | XP bar (full width, bottom) | done | |
| U06j | New UI input handling (slot clicks, popups, chat focus) | done | |
| U07a | Relocate game-state data models out of graphics/ui/ | done | |
| U07b | Standalone hotbar data model (JSON read/write + events) | done | |
| U07c1 | Disable old UI render + input (new UI takes over) | done | |
| U07c2 | Delete WindowManager + all window subclass files | done | |
| U07c3 | Delete WindowBase, UISettings, old UI infrastructure | done | |

---

## Batch T — Texture Loading Consolidation

Consolidate all texture decoding into `ConstrainedTextureCache::getOrLoad`. Remove all
duplicate decoders. Separate decode (any thread) from GPU upload (renderer thread).

### Phase T1 — Cache Refactor

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| T01 | Split `getOrLoad` into decode-only (no GL) + new `uploadTexture` (GL). Add `DecodedEntry` struct for decoded-but-not-uploaded textures. Add `uploadAllPending()` for sequential loader. | done | |
| T02 | Add 8bpp paletted BMP support to `decodeBMP` (colors-used field, palette lookup) | done | |
| T03 | Make `getOrLoad` synchronous (remove background pool submission, remove deferred nullptr-retry) | done | (completed in T01) |
| T04 | Remove `queueDecodedARGB` and `processUploadQueue` | done | |

### Phase T2 — Remove Duplicate Decoders

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| T05 | `RaceModelLoader::preloadModelData` — remove inline DDS/BMP decoder (lines 698-820), replace with `getOrLoad` calls. Add `ConstrainedTextureCache*` member to `RaceModelLoader`. Remove `RaceModelData::decodedTextures`. | done | |
| T06 | Step 8c `decodeAndAdd` lambda — remove inline DDS/BMP decoder (140+ lines), replace with `getOrLoad` calls for each variant texture. Remove `variantTextures` vector. | done | |
| T07 | Step 8d `prepOneEquip` lambda — remove inline DDS decoder, replace with `getOrLoad` calls for equipment textures. Remove `DecodedTexture` vectors from `EquipPrepData` and `EntityVisual::equipmentTextures`. | done | |
| T08 | Sky renderer `uploadPreDecodedTexture` — replace `decodeBMPtoARGB` + direct upload with `getOrLoad` + `uploadTexture`. | done | |

### Phase T3 — Entity Build Consolidation

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| T09 | Step 8e — remove all `queueDecodedARGB` calls. Replace with `uploadAllPending()` after steps 8a-8d populate the cache. | done | |
| T10 | `EntityRenderer::pollAndDistributePrepResults` — remove `queueDecodedARGB` calls and texture moves. Textures already in cache from prep worker `getOrLoad` calls. GPU upload deferred to `processOneEntityBuildStep` (T11). | done | |
| T11 | `EntityRenderer::processOneEntityBuildStep` — texture upload phases now skip directly (textures in cache from getOrLoad). `uploadAllPending()` at SceneNodeCreation. | done | |
| T12 | Add `processAllEntityBuildSteps` — not needed. Sequential loader already loops via `buildEntityMesh` per entity in Step 8f. `uploadAllPending()` in T09 handles batch upload. | skipped | |

### Phase T4 — Cleanup

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| T13 | Remove `EntityVisual::variantTextures`, `equipmentTextures`, `texturesSubmittedToGpu`, `nextTextureUpload`, `uploadedTextures`, `uploadedTextureAlpha`, `nextVariantUpload`, `nextEquipTextureUpload`. Remove inline DDS decoder in Placeholder phase. Keep `equipmentStaging`. `DecodedTexture` struct kept (still referenced by entity_prep_worker). | done | |
| T14 | Remove `decodeBMPtoARGB` standalone function (sky). Remove `bilinearUpscaleARGB`. Remove dead commented-out unconstrained texture path from `loadTextureFromBMP`. | done | |
| T15 | Verify: grep for `addTexture`, `queueDecodedARGB`, `loadTextureFromBMP`, `decodeBMP`. Confirmed single `decodeBMP` in constrained cache, single `addTexture` in `uploadTexture`. Removed dead `uploadPreDecodedTexture` from sky renderer. Other `addTexture` calls are procedural/runtime (effects, cursor) — not texture loading. | done | |

---

## Batch Z — Offline Zone Rendering Benchmark

Standalone benchmark tool (`gles2_zone_benchmark`) that loads real EQ zone geometry via the
production S3D/WLD pipeline, replays a navmesh-generated camera path, and measures per-frame
rendering metrics with A/B toggles for each optimization technique.

### Phase Z1 — Basic Framework

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| Z1.1 | `gles2_zone_benchmark.cpp` — CLI parsing, Irrlicht device creation, benchmark loop | done | |
| Z1.2 | `zone_bench_loader.h/.cpp` — Zone loading: S3DLoader, texture cache, atlas, ZoneShaderManager, per-region mesh build, VBO upload, BSP/portal/bbox | done | |
| Z1.3 | Render loop — set matrices, collect draw entries with visibility, setMaterial+drawMeshBuffer, glFinish, frame stats | done | |
| Z1.4 | CMakeLists.txt — add executable with zone pipeline + GLES2/DRM + navmesh libs | done | |

### Phase Z2 — Navmesh Camera Path

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| Z2.1 | `zone_bench_camera.h/.cpp` — CameraPath class, generateFromNavmesh (sample waypoints, pathfind, concatenate) | done | |
| Z2.2 | Fallback orbit path (generateOrbit) | done | |
| Z2.3 | Path interpolation (getPositionAt/getDirectionAt, fixed dt, --speed flag) | done | |
| Z2.4 | Integrated into render loop (camera advance, view matrix, FrustumCuller, BSP region lookup, PVS filter) | done | |

### Phase Z3 — Per-Frame Metrics + CSV

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| Z3.1 | `zone_bench_metrics.h/.cpp` — FrameMetrics struct, MetricsCollector with begin/end timing | done | |
| Z3.2 | Draw loop instrumented — draw call count, state change count, polygon count | done | |
| Z3.3 | CPU vs GPU split — clock around draw loop + glFinish separately | done | |
| Z3.4 | CSV output (--csv FILE) + summary stats (min/avg/max/p95) | done | |

### Phase Z4 — A/B Optimization Toggles

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| Z4.1 | `--no-material-sort` — disable material-key draw sorting | done | |
| Z4.2 | `--no-portal-cull` — disable PVS bitvector check | done | |
| Z4.3 | `--no-frustum-cull` — disable frustum test | done | |
| Z4.4 | `--no-front-to-back` — disable distance sorting within material groups | done | |
| Z4.5 | `--no-atlas` — use buildTexturedMesh instead of buildAtlasedMesh (load-time) | done | |
| Z4.6 | `--no-depth-test` — glDisable(GL_DEPTH_TEST) | done | |
| Z4.7 | `--max-draws N` / `--max-polys N` — cap per frame | done | |
| Z4.8 | `--lighting N` — point light count 0-8 from zone data | done | |

### Phase Z5 — Synthetic Draw Call Isolation

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| Z5.1 | `--synthetic-draws` — sweep N=1..1000 identical draws, measure per-call overhead | done | |
| Z5.2 | State change variant — setMaterial between each draw | done | |
| Z5.3 | Empty draw measurement — (TODO: 1-triangle buffer isolation) | | |
