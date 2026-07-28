# P28 — eq.h graphics-guard misclassification fix

**Slot: slot2. x86 only — no Pi, no eqemu.** Sibling agents: P27
(slot1, headless-segfault test investigation — different files), P29
(slot3, capture recording — no source changes). Territories disjoint.

Executes TESTING_HANDBACK.md item 1 (read it FIRST — it carries the
full diagnosis; trust it, verify as you go). The five
`#ifdef EQT_HAS_GRAPHICS` blocks in `include/client/eq.h` enclose
large amounts of NON-graphics code (GameTick, m_quit_requested,
InventoryManager, CommandRegistry, hotbar model, EQ client path, the
~163-line member block at ~1689-1853). Consequence: `EQT_GRAPHICS=OFF`
fails with 351 errors across 12 TUs, and ~33k lines of base-layer
source can't be compiled into any test binary. The fix direction is
settled (handback + deferred.md): **move non-graphics members OUT of
the guards**, leaving only genuinely renderer-facing API inside
(InitGraphics/ShutdownGraphics, IrrlichtRenderer,
ConstrainedRendererConfig, EQT::Graphics::* types). Do NOT add guards
to the .cpp files — the correct end state is that they need none.

## Work

1. Confirm the macro indirection (CMake option `EQT_GRAPHICS` →
   compile macro `EQT_HAS_GRAPHICS`) and note where it's defined.
2. eq.h: reclassify the five guard blocks. The 163-line member block
   mixes real graphics state (m_graphics_initialized,
   m_constrained_config) with plain game state — split it with
   judgment; when a member is genuinely ambiguous, decide by "does the
   headless client need it to function?" (it began life headless).
   NOTE: this legitimately reverses the hotbar-accessor guards added
   by 55b101e — the types (HotbarButtonType/PendingHotbarButton/
   CommandRegistry/InventoryManager fwd decls, eq.h ~73-110) move OUT
   of the guard; expect knock-on un-guarding in eq_chat.cpp from that
   same commit.
3. Fix the two smaller error categories the handback identified:
   uses of `eqt::ui`/`eqt::state::EntityPoseState`/`EQT::Graphics`
   types in non-graphics code paths (move the decls out or relocate
   the code's types — judgment per case, favor the smallest change
   that keeps eq.cpp's two-graphics-includes architecture), and
   application.cpp's incomplete-type GameStateBridge at the
   headless-bridge attach site (~line 540 — likely a missing include).
4. GOAL STATE: `cmake -S . -B build-nogfx -DEQT_GRAPHICS=OFF
   -DEQT_BUILD_TESTS=ON` configures AND builds clean, and its ctest
   runs (expected: the previously NOT_BUILT test_titanium_opcodes /
   test_eq_client / test_session_replay now build; session-replay
   tests may SKIP for missing fixtures — P29 is recording them
   concurrently, skipping is fine).

## Rules

- Behavior-preserving refactor: the graphics-ON builds must be
  functionally identical. No logic changes, no member renames, no
  "while I'm here" cleanups. Preprocessor-block moves and includes
  only, plus the minimal type-relocations step 3 requires.
- Territory: include/client/eq.h, the 12 failing TUs' guard/include
  lines, application.cpp attach site, include/client headers that
  need decls relocated. NOT: graphics source, driver, tests' logic
  (test CMake fixes only if a link set needs the same backend idiom
  P26 established).
- STOP: a member's classification can't be settled without a design
  decision (e.g. something that LOOKS non-graphics but is entangled
  with renderer lifetime) — list it with analysis rather than
  guessing; partial progress with a clean build still failing on a
  named remainder is an acceptable outcome IF the remainder list is
  short and precise.

## Verification

- Full 6-step bar on the desktop tree (known exceptions per
  protocol) — graphics-ON must be at baseline.
- GLES2 tree full ctest at P26 baseline (1233/1235 + the documented
  P27-in-flight segfault, whatever its state on your branch).
- The NEW bar: build-nogfx builds clean + its ctest result reported
  (expected ~1235 registered now; failures = documented exceptions +
  fixture skips only).
- Visual gate PASS, goldens untouched.

## Review

### Macro indirection

Confirmed intentional: `CMakeLists.txt:12` `OPTION(EQT_GRAPHICS ...)` →
`ADD_DEFINITIONS(-DEQT_HAS_GRAPHICS)` at `CMakeLists.txt:79,88` when
`EQT_GRAPHICS` (and Irrlicht) is available.

### Per-block reclassification (eq.h)

| Original block | Stayed guarded (`EQT_HAS_GRAPHICS`) | Moved out (unconditional) |
|---|---|---|
| 73–110 (fwd decls) | `IrrlichtRenderer`, `ConstrainedRenderingPreset`, `ZoneLoadStep`, `constrained_renderer_config.h` include, `LoadingStatus` | `BspTree`/`PfsArchive`/`WldLoader` fwd decls (zone_bsp.h: "no graphics dependencies"), `PlayerPositionChanged` (renderer_intents.h: "no renderer includes allowed"), `eqt::inventory::InventoryManager`, `eqt::ui::CommandRegistry`, `HotbarButtonType`, `PendingHotbarButton` |
| 765–769 (hotbar accessors) | — | `GetPendingHotbarButtons`/`ClearPendingHotbarButtons`/`GetPendingHotbarButtonCount` (their only graphics dependency was the now-unguarded `PendingHotbarButton` type) |
| 1032–1081 (InitGraphics cluster) | `InitGraphics`, `ShutdownGraphics`, `CreateZoneLoadSnapshot`, `Set/GetConstrainedPreset`, `Set/GetConstrainedConfig`, `SetPlayerGraphicsEntityPending`, `SetLoadingStatus`, `ConsumeZoneLoadRequest`, `SetZoneBspTree` | `GameTick`, `PreRenderTick`, `PostRenderTick`, `Set/GetEQClientPath`, `SetRegionMapsPath`, `Set/GetConfigPath`, `Save/LoadHotbarConfig`, `PublishFullStateSnapshot`, `GetHotbarModel`, `GetCurrentZoneName`, `GetTimeOfDay`, `OnGraphicsMovement`, `UpdateInventoryStats` |
| 1563–1565 (`m_pending_hotbar_buttons`) | — | moved out (vector of the now-unguarded `PendingHotbarButton`) |
| 1689–1853 (163-line member block) | `m_graphics_initialized`, `m_constrained_preset`, `m_constrained_config`, `m_loading_status_ptr`, `m_zone_load_requested` | everything else: `m_eq_client_path`/`m_region_maps_path`/`m_config_path`, `m_quit_requested`, `m_hotbar_model`, `m_inventory_manager`, `m_command_registry` + `RegisterCommands` family, loot/vendor/bank/trainer game-side state + packet handlers + public window methods, `OnSpawn*Graphics`, `OnPet*`, inventory/trade/book packet handlers |

### Ambiguous calls and rationale

- **`m_zone_bsp_tree`/`m_zone_archive`/`m_zone_wld` and their types
  (`BspTree`/`PfsArchive`/`WldLoader`)**: named "Graphics" but
  `zone_bsp.h` says outright "no graphics dependencies"; `eq_zone.cpp`'s
  `LoadZoneSources()` (unguarded, called from the base zone-connect flow)
  parses the WLD for the BSP tree used by water detection/zone lines.
  Reclassified as base game data; `pfs.cpp`/`wld_loader.cpp` moved from
  `GRAPHICS_SOURCES` to `CLIENT_CORE_SOURCES` in CMakeLists (confirmed via
  `audio_manager.cpp`'s unconditional `PfsArchive` use for sound-archive
  scanning, which was already relying on this classification being wrong).
- **`m_constrained_config`/`m_constrained_preset` read from
  `EverQuest::InitializeAudio()`** (`eq_bridge.cpp`): this WITH_AUDIO-gated
  function (not EQT_HAS_GRAPHICS-gated) read renderer-only startup state
  to size the audio cache — a pre-existing latent bug (not introduced by
  P28; reproduces at the `fable` tip once the surrounding guard is
  loosened). No headless equivalent of "constrained preset" exists, so I
  gated just those specific reads (`#ifdef EQT_HAS_GRAPHICS`) rather than
  invent one — audio simply skips the memory-constraint step when no
  renderer/preset exists. Flagging rather than silently deciding: if a
  headless memory-budget concept is wanted later, this is the seam.
- **`GameTick`/`PreRenderTick`/`PostRenderTick`'s internal
  `if (!m_graphics_initialized) return;`**: `m_graphics_initialized`
  stayed guarded (genuine renderer-lifetime flag), but these three tick
  functions are called unconditionally from
  `Application::updateGameState()`/`render()`. Gated just the early-return
  check; under `EQT_GRAPHICS=OFF` the ticks always run their body (no
  behavior change for graphics-ON — same flag, same check; for
  graphics-OFF there is no prior behavior to preserve, and
  `Application`'s own `m_graphicsInitialized` gate at the call site means
  these are effectively unreachable in real headless operation today
  regardless).
- **`eq_bridge.cpp`/`eq_merchant.cpp`'s single all-encompassing
  `#ifdef EQT_HAS_GRAPHICS`** (1592 and 1408 lines respectively): both
  files wrapped nearly their entire body in one guard. Audited every
  function for actual Irrlicht/renderer symbol usage (`grep` for
  `irr::`, `IrrlichtRenderer`, `getGUIEnvironment`, `m_renderer->`,
  weather/detail-system types) — found none outside `InitGraphics`/
  `ShutdownGraphics`/`CreateZoneLoadSnapshot` in `eq_bridge.cpp` (which
  keep their narrower guards) and none at all in `eq_merchant.cpp`. The
  rest is pure bridge-event/packet-handler logic; unguarded to match.
- **`hotkey_manager.cpp`/`irrlicht_bridge.cpp`**: unconditionally
  `#include <irrlicht.h>` yet were listed in `CLIENT_CORE_SOURCES`
  (always compiled) — a separate, pre-existing CMake misclassification
  independent of the eq.h guards (their headers are graphics-free; only
  the `.cpp` bodies need Irrlicht). Moved both to `GRAPHICS_SOURCES`;
  confirmed every call site (`HotkeyManager::instance()`,
  `IrrlichtBridge` construction) was already inside an
  `EQT_HAS_GRAPHICS` guard at the caller.
- **`inventory_manager.cpp`/`inventory_constants.cpp`/`hotbar_model.cpp`/
  `chat_message_buffer.cpp`/`command_registry.cpp`**: same
  misclassification in reverse — graphics-free implementations
  (confirmed via `#include` audit: no Irrlicht anywhere) that were only
  compiled under `GRAPHICS_SOURCES`, even though their headers are now
  (correctly) included unconditionally. Moved to `CLIENT_CORE_SOURCES`.

### STOP-list (none required)

No member's classification required a design decision beyond what's
covered above — the ambiguous cases were resolved by direct evidence
(explicit "no graphics dependencies" header comments, actual call-site
guard audits, or grep-confirmed absence of Irrlicht symbols) rather than
guesswork. No remainder list.

### Verification — all three trees + goldens

1. **Desktop (graphics ON), `build/`**: `cmake --build build -j24` — clean,
   0 errors. `ctest --test-dir build --output-on-failure`: **1233/1235
   passed**, 2 failed — both documented known host exceptions
   (`AudioManagerTest.PreloadCommonSounds`,
   `RDPServerFrameTest.UpdateFrameMultipleTimes` SEGFAULT/intermittent).
   `visual_gate`: **PASS**. Exactly at baseline.
2. **GLES2 + tests, `build-gles2-tests/`** (configured
   `EQT_GRAPHICS=ON EQT_GLES2=ON EQT_DRM=OFF
   EQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86
   EQT_BUILD_TESTS=ON WITH_RDP=ON` per P26's idiom): 8 targets
   (`entity_composite_builder`, `generate_surface_map`, `s3d_dump`,
   `zone_line_editor`, `model_viewer`, `test_graphics`,
   `test_asset_loading`, `test_door_loading`) fail to **link** with
   undefined X11 symbols (`XNextEvent`, `XCreateWindow`, etc.) in this
   dev environment. Verified by `git stash`-ing all P28 changes and
   rebuilding the same 8 targets against the unmodified `fable` tip in
   the same tree: **identical failures reproduce** — pre-existing/
   environment issue, not a P28 regression (unstashed and confirmed
   rebuild-clean afterward). `ctest --test-dir build-gles2-tests`:
   **1113/1118 passed** (3 `_NOT_BUILT` from the link failures above),
   2 failed — `AudioManagerTest.PreloadCommonSounds` (known host
   exception) and `RaceModelAnimationHeadlessTest.ModelAnimatesProperly_Headless`
   SEGFAULT (the documented P27-in-flight blocker, unrelated to this
   unit). `visual_gate`: **PASS**. Exactly at the P26 baseline.
3. **`bash scripts/build-gles2-x86.sh`**: clean, `Done. Binaries in
   .../build-gles2/bin/`.
4. **`build-nogfx/` (the new bar, `-DEQT_GRAPHICS=OFF
   -DEQT_BUILD_TESTS=ON`)**: configures and **builds clean** (351 → 0
   errors). `ctest --test-dir build-nogfx --output-on-failure`:
   **812/813 passed**, 1 failed — `AudioManagerTest.PreloadCommonSounds`
   (the same known host exception; audio is compiled in and works
   headless). `test_titanium_opcodes`/`test_eq_client`/
   `test_session_replay` now build and run (session-replay tests SKIP
   for missing fixtures, per plan — P29 records them concurrently).
   `visual_gate`: **PASS** (skips/no-ops cleanly without a renderer).
5. `bash scripts/build-arm-noble.sh`: clean, all binaries produced
   (including `model_viewer`).
6. `bash scripts/refactor/check_file_sizes.sh fable`: same one
   pre-existing FAIL (`eq.cpp`, already over threshold at the `fable`
   baseline: 3689→3697 lines, +8 from added comments) and same
   pre-existing WARNs (`eq.h`, `application.cpp`, `eq_bridge.cpp`,
   `eq_entities.cpp`, `eq_merchant.cpp`, `eq_movement.cpp` — all
   already over threshold at baseline, deltas of 0–24 lines from
   comments/guard punctuation only). No new FAILs.
7. Goldens: `git status` shows no changes under any golden/capture path.

### Deviations from the plan

- **CMake source-list fixes beyond eq.h/the 12 TUs/application.cpp**:
  the plan's territory names "include/client headers that need decls
  relocated," but reaching the goal state also required 7
  `CMakeLists.txt` source-list corrections (2 `.cpp` files moved
  `CLIENT_CORE_SOURCES → GRAPHICS_SOURCES`, 5 moved the other way) — see
  the STOP-list section above. These are mechanical consequences of the
  same misclassification (graphics-free files stuck in the graphics
  bucket or vice versa), not scope creep into driver/test logic, but
  flagging since CMakeLists.txt wasn't named explicitly.
- **`application.h` member-guarding**: added `#ifdef EQT_HAS_GRAPHICS`
  around `m_constrainedCache`/`m_renderer`/`m_irrlichtBridge`/
  `m_graphicsInputHandler`/`m_loadingThread`/`m_loadingScreenFont` (their
  `unique_ptr` destructors need complete Irrlicht-dependent types at
  `~Application()`) and split `isLoadingThreadActive()` into a
  guarded/`#else` pair. This is "include/client headers that need decls
  relocated" per the plan's territory, called out explicitly since it's
  a header outside eq.h.
- **No behavior change on any graphics-ON path**: every edit was either
  a pure guard/include relocation, or (for the two ambiguous runtime
  cases above) an added inner `#ifdef EQT_HAS_GRAPHICS` around code that
  has no headless equivalent — the graphics-ON code path executes
  identically before and after.
