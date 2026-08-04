# T43 — Sweep the `EQT_HAS_GRAPHICS` guards, and pin headless with a test that would have caught them

**Slot: t1. x86 only. No Pi. eqemu for the acceptance leg.**
No sibling unit.

**Territory: every `EQT_HAS_GRAPHICS` / `EQT_GRAPHICS` guard outside
`src/client/graphics/`, plus tests.** `src/client/graphics/` guards are
presumed legitimate — spot-check, don't sweep them.

Read `refactor/EXECUTOR_PROTOCOL.md`, then the deferred entry
**"PATTERN: `EQT_HAS_GRAPHICS` gates around non-graphics code — five
instances and counting"**, then P28's Review (it set the rule).

## Why this unit exists

Five separate units have each found one of these by accident, and the
severity escalated every time:

| unit | wrongly gated | headless effect |
|---|---|---|
| P28 | `eq.h` guard blocks | ~33k lines in no test binary |
| T29 | ~2,500 lines of command registration | no slash commands at all |
| T41 | `m_inventory_manager`'s **construction** | manager never built |
| T42 | the `HC_OP_ItemPacket` dispatch branch (+ a larger gate) | inventory packets dropped |
| T42 | `Application::initialize()` never calling `SetEQClientPath()` | **headless could not stay connected to a server** |

Nobody noticed the last one because nothing had tried. That is the shape
of the whole problem: these do not fail loudly, they make headless
quietly less functional than it appears — and headless is the mode the
automation work and the testing campaign run in.

**The rule is already settled** (P28): *only renderer-facing API stays
guarded.* What is missing is that nobody has applied it exhaustively.
This unit does that, and pins it so instance six cannot happen silently.

## Scope

### 1. Inventory and classify every guard

There are ~97 occurrences across 19 files. Concentration is in the game
and boundary layer — `application.cpp` (16), `eq.cpp` (13),
`eq_entities.cpp` (10), `eq_bridge.cpp` (10) — which is exactly where
misclassification bites.

Classify each as:
- **Legitimate** — genuinely renderer-facing API, or code that cannot
  compile/run without the renderer.
- **Misclassified** — non-graphics logic (game state, network, packet
  dispatch, command registration, construction of non-graphics members)
  that merely happens to sit near graphics code.

Produce the classification as a table in your Review. **A guard you
leave in place needs a one-line reason** — that is the artifact that
stops the next person re-auditing from scratch.

Note `eq_bridge.cpp` is the designated boundary TU (CLAUDE.md: "the
graphics-facing game code is concentrated in `eq_bridge.cpp`"), so guards
there are more likely legitimate. More likely, not automatically.

### 2. Fix the misclassified ones

Un-guard them, following P28's direction. Two oracles will catch
mistakes: the `EQT_GRAPHICS=OFF` build (compile errors surface anything
genuinely graphics-dependent) and item 3's test.

**The subtler risk the build will NOT catch**: code that compiles
headless but should not *run* there — most obviously anything pushing to
a bridge or renderer pointer that is null in headless. Null-check rather
than re-guard, and say which sites needed it.

Include the known residual: **all three `OnPetCreated()` call sites in
`eq_entities.cpp` are guarded**, so pet deltas never publish in an
`EQT_GRAPHICS=OFF` build (T41's snapshot fix covers only a pet present at
zone-in).

### 3. Pin it — the part that prevents instance six

Add a regression test that exercises a **headless zone-in end to end and
asserts game state actually populated** — inventory slots, pet, spell
gems, player stats. Three of the five instances above would have failed
such a test immediately.

It must run in the `EQT_GRAPHICS=OFF` tree, so it cannot depend on a
live server. The existing replay seam is the way in:
`EverQuest::ReplayZonePacket` already drives zone packets server-free and
is used by `test_session_replay` and `test_zone_connection_flow` (T41
added snapshot coverage there). Extend that approach rather than
inventing a new harness.

Assert **state populated**, not merely "did not crash" — the whole point
is that these failures are silent.

## STOP conditions

- The misclassified set turns out to be far larger than the ~49
  game/boundary-layer occurrences suggest, such that fixing them all in
  one unit would be unreviewable → STOP, deliver the classification
  table plus the test, and propose a phased split. **The audit alone has
  standalone value**; do not turn this into an unbounded refactor.
- Un-guarding something requires restructuring (not just null-checking) →
  leave it, classify it as "needs its own unit", and say why.
- A guard turns out to protect against something real that neither oracle
  catches → STOP and report; that is a finding worth more than the fix.
- Any behaviour change in the graphics build → STOP. This unit must be
  a no-op for graphics mode; that is the safety property.

## Verification bar

1. Both configures build clean — graphics and `EQT_GRAPHICS=OFF`.
2. `ctest` in **both** trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35),
   the golden replay does not skip (T36); a failure or `Skipped` on
   either is a real signal.
3. Golden replay unchanged and confirmed to have RUN, in both trees.
4. **Graphics mode must be provably unaffected.** Say how you established
   that — visual gate PASS with goldens untouched is the obvious
   instrument, and this is exactly what it is for.
5. **Live headless acceptance**: a real eqemu session in an
   `EQT_GRAPHICS=OFF` build that zones in, stays connected, and shows
   state populated — the scenario that was broken until T42. eqemu LOCK
   FIRST; `up.sh` only; restore Testchar to zone_id=36, x=-77.9232,
   y=21.9086, z=3, h=190; `down.sh` after. Evidence under
   `benchmarks/audits/T43/`.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

### Method

Grepped every `EQT_HAS_GRAPHICS`/`EQT_GRAPHICS` occurrence outside
`src/client/graphics/` and `include/client/graphics/` (19 files, matching
the plan's count). Read every active `#ifdef` block (not just the ~97
occurrence count -- many are `#endif`/comment lines) in the four
concentration files (`application.cpp`, `eq.cpp`, `eq_entities.cpp`,
`eq_bridge.cpp` = 49 of 97) plus `eq_commands*.cpp`, `eq_player_info.cpp`,
`eq_zone.cpp`, `eq.h`, `application.h`, `output_renderer.cpp`,
`graphical_mode.cpp`, `graphics_input_handler.{h,cpp}`, and spot-checked
the graphics-dependent test files. For each guard: read the guarded body,
checked whether it touches a renderer/Irrlicht symbol or a
graphics-lifetime-only member, and cross-referenced prior units'
declassification comments (P28/T29/T41/T42) already in the code.

### Classification table

**Misclassified — fixed this unit** (un-guarded; see per-file detail below):

| File:region | What was gated | Why misclassified |
|---|---|---|
| `application.cpp` ~729 | `LoadHotbarConfig()` + `GetHotbarModel()->publishAllSlots()` on zone-connect | Both un-guarded/declassified by P28 in `eq.h`; this call site never caught up — headless never loaded/published saved hotbar bindings |
| `eq.cpp` ~2271 | `m_inventory_manager->setPlayerInfo(race, class_, level)` | `m_inventory_manager` constructed unconditionally since T41; `setPlayerInfo` is pure inventory-manager state (item-restriction validation), no renderer dependency |
| `eq_entities.cpp` ~475, ~1234, ~1461 | `OnPetCreated(...)` (3 call sites) | The plan's named residual: `OnPetCreated` (`eq_bridge.cpp`) is a pure bridge-event publish; pet **deltas** never published in `EQT_GRAPHICS=OFF` (T41's snapshot fix only covered a pet present at zone-in) |
| `eq_entities.cpp` ~1824 | `UpdateInventoryStats()` on player HP change | `UpdateInventoryStats()` (`eq_bridge.cpp`) is unguarded/declassified since P28 — equipment-stat calc + bridge push, no renderer dependency |
| `eq_player_info.cpp` ~523 | `UpdateInventoryStats()` on player mana/endurance change | Same as above |
| `eq_bridge.cpp` ~1727 (`InitializeAudio`) | `eqPath = m_eq_client_path` | `m_eq_client_path` is plain game config, unconditional since T42's `SetEQClientPath()` fix; gating the *read* here meant `EQT_GRAPHICS=OFF + WITH_AUDIO=ON` could never find sound assets — audio silently stayed disabled even with a configured path |
| `eq_commands_spells.cpp` `/bank` handler | `IsBankWindowOpen()`/`OpenBankWindow()`/`CloseBankWindow()` | All three already unguarded at their own definitions (`eq.h`/`eq_merchant.cpp`, pure game state + bridge events) — only this command handler's own wrapping refused to call them headless |
| `eq_commands_spells.cpp` `/train` handler | `RequestTrainerWindow(target_id)` (+ its target/class validation) | `RequestTrainerWindow` (`eq_merchant.cpp`) just queues `HC_OP_GMTraining` — plain network logic, no renderer dependency |

**Legitimate — left guarded, one-line reason each** (representative, not
exhaustive — the full ~90 remaining occurrences fall into these same
categories):

| File(s) | Category | Reason |
|---|---|---|
| `application.cpp` (graphics-init cluster ~372-609, `render()`, `startLoadingThread`/`loadZoneGraphics*`, RDP shutdown, `SetPlayerGraphicsEntityPending`-driven zone-load-request/graphics-complete pump ~760-797) | Renderer lifecycle | Directly constructs/owns `IrrlichtRenderer`, GL context, loading thread; genuinely cannot compile or run without the renderer |
| `application.cpp` (`HandleSigHup`, CLI help text for `-r`/`--renderer`/`--drm`/etc., hotkey config load in `loadConfigFile`) | Renderer-only input/config | `HotkeyManager` requires `<irrlicht.h>` (moved to `GRAPHICS_SOURCES` by P28); the CLI flags/help text describe renderer-only options |
| `eq.h` (74, 1130, 1953 blocks) | Type/member declarations | `IrrlichtRenderer`/`ConstrainedRenderingPreset` fwd decls, `InitGraphics`/`ShutdownGraphics`/loading-thread signal members — P28 already reclassified this file exhaustively; re-verified, no further splits needed |
| `application.h` (constrainedCache/renderer/irrlichtBridge/graphicsInputHandler/loadingThread/loadingScreenFont members, `isLoadingThreadActive()`) | `unique_ptr` deleter needs complete Irrlicht-dependent type at `~Application()` | P28's own documented reason, re-verified |
| `eq.cpp`/`eq_bridge.cpp` `InitGraphics`/`ShutdownGraphics`/`CreateZoneLoadSnapshot`/`GameTick`-`PreRenderTick`-`PostRenderTick`'s `!m_graphics_initialized` early-return | Renderer lifecycle | P28's own documented split (member-level, not whole-function); re-verified |
| `eq_bridge.cpp` ~1753 (`InitializeAudio`'s memory-constraint block) | Renderer-startup-only state | `m_constrained_config`/`m_constrained_preset` have no headless equivalent (P28's own flagged judgment call) — audio just skips the memory-constraint step headless; distinct from the ~1727 `eqPath` fix directly above it, which reads unconditional state |
| `eq_entities.cpp` (`OnSpawnAddedGraphics`/`OnSpawnMovedGraphics` calls, `m_player_graphics_entity_pending` writes, `PlayerSpawnIdSet` push at the two *spawn-processing* sites ~396/~1209) | Renderer entity creation | These push/set state whose only stated purpose and only consumer (`irrlicht_bridge.cpp`) is marking/creating the renderer's player node; the *general* `EntitySpawned`/`PlayerSpawnIdSet` publish during `ClientUpdate` (~1474-1531, already unguarded since before this unit) is the unconditional path headless relies on — confirmed this is not a second silent gap, see "Investigated, not a bug" below |
| `eq_zone.cpp` ~212 (`CleanupZone`'s `ZoneUnloading` push) | Renderer loading-screen signal | Comment states "show loading screen via bridge"; sole consumer is `irrlicht_bridge.cpp`'s loading-screen handler — no headless equivalent named |
| `eq_commands_spells.cpp`/`eq_commands_movement.cpp` (`/hotkeys`, `/spellreload`, `/weatherquality`, `rendererCmd` helper used by `/timestamp`, `/reloadeffects`, `/reloadweather`, etc.) | Renderer-only singletons/commands | `HotkeyManager`, `SpellEffectsConfig`, `WeatherConfigLoader` all require Irrlicht; `rendererCmd` pushes a `RendererCommand` bridge event that only the render thread's command bridge drains (T29's own documented "headless honesty" design: says so instead of silently no-op'ing) |
| `input/graphics_input_handler.{h,cpp}` | Whole-file | `IEventReceiver`-wrapping Irrlicht input adapter by design/name; entire class is renderer-only |
| `output/output_renderer.cpp`, `mode/graphical_mode.cpp` | Stub renderer factories | Commented-out `IrrlichtRenderer` adapter construction (not yet wired); falls back to console renderer either way |
| `tests/test_zoning_graphics_integration.cpp`, `test_beneficial_spell_casting.cpp`, `test_inventory_model_view.cpp`, `test_door_loading.cpp` | Whole-file, `GTEST_SKIP()` idiom | Construct/use a real `EQT::Graphics::IrrlichtRenderer`/`irr::` types directly to test actual rendering (race-model bounding boxes, door scene nodes, RDP/VNC-visible windows) — genuinely cannot run without the renderer; skip cleanly under `EQT_GRAPHICS=OFF` rather than silently vanish |

**Investigated, not a bug**: `eq_entities.cpp`'s two spawn-processing
`PlayerSpawnIdSet` pushes (~396, ~1209) look, at first read, like the same
misclassification as `OnPetCreated` (bridge push gated for no renderer
reason). Traced every consumer: `PlayerSpawnIdSet` is consumed *only* by
`irrlicht_bridge.cpp` (renderer player-node marking), and the *general*
`ClientUpdate` handler (eq_entities.cpp ~1474-1531, unguarded since before
this unit) already publishes the same `EntitySpawned` + `PlayerSpawnIdSet`
pair unconditionally once `m_my_spawn_id` is known — this is the path a
headless build actually relies on. The two spawn-processing sites are a
harmless early/renderer-only duplicate of state the general path
re-establishes regardless, not a silent gap; left as-is rather than
un-guarded for consistency's sake alone, since un-guarding them would add
duplicate event traffic with no headless consumer.

### Sites needing a null-check instead of un-guarding

**None found.** Every function this unit un-guarded already null-checks
its own graphics-only pointers internally (`if (m_inventory_manager)`,
`if (m_bridge)` inside `UpdateInventoryStats()`/`OnPetCreated()`/
`OpenBankWindow()`/etc.), and `m_bridge` is unconditionally attached by
`Application::initialize()` before any game-thread code runs (either the
`IrrlichtBridge` path or the `ConsoleBridge`+`GameStateSyncBridge` headless
path, D25/T40). No site pushed to a bridge/renderer pointer that could be
null at the point these fixes now reach it.

### The pet-delta fix (the plan's named residual)

All three `OnPetCreated()` call sites in `eq_entities.cpp` (the
`ZoneProcessSpawnAll` second pass, `ZoneProcessNewSpawn`'s inline check, and
`ZoneProcessClientUpdate`'s deferred pet-detection loop) are now unguarded.
`OnPetCreated()` itself (`eq_bridge.cpp`) is a pure bridge-event publish
with no renderer dependency. Verified `tests/test_zone_connection_flow.cpp`'s
existing `PreExistingPetSurvivesHeadlessZoneInViaSnapshot` (T41) still
passes unmodified in both trees — it was already written to accept either
"delta fired" or "snapshot-only" shape, so this fix simply flips which
branch it takes in the `EQT_GRAPHICS=OFF` tree (from snapshot-only to
delta-fires-too), without changing its assertions.

### The regression test

Added `FullStateSnapshot.HeadlessZoneInPopulatesGameState` to
`tests/test_zone_connection_flow.cpp`, extending that file's own
`ReplayZonePacket`/`BuildPayload`/`RecordingBridge` idiom (already used by
`test_session_replay` and this file's own T41 pet-snapshot test) rather than
inventing a new harness. It drives a full headless zone-in purely through
synthetic `HC_OP_NewZone`/`HC_OP_PlayerProfile`/`HC_OP_ZoneSpawns`/
`HC_OP_CharInventory`/AA-tribute-guild-gate/`HC_OP_ClientUpdate` packets
(no network, no server, no renderer), then drains the bridge and asserts,
by exact value match (not "no crash"):

- `PlayerStatsChanged` with the PlayerProfile's HP/mana (**player stats**)
- `InventorySlotChanged` at the primary-weapon slot with the CharInventory
  item's exact itemId (**inventory slots** — via a new `BuildItemString()`
  helper that constructs a minimal but wire-format-valid Titanium serialized
  item string, the same format `TitaniumItemParser::parseItem()` expects)
- `SpellGemChanged` with the PlayerProfile's memorized spell id
  (**spell gems**)
- `PetCreated` for a pre-existing pet in the zone spawn list (**pet**)

Passes in both `build/` (graphics-ON) and `build-nogfx/`
(`EQT_GRAPHICS=OFF`) trees.

**Of the five historical instances, this test would have caught three
outright:**
- **P28** (`eq.h` guards enclosing non-graphics members): the
  `EQT_GRAPHICS=OFF` tree wouldn't have compiled this test binary at all —
  a build failure is "failed immediately" in the bluntest sense.
- **T41** (`m_inventory_manager` never constructed): the
  `InventorySlotChanged` assertion would find no populated slot.
- **T42** (`HC_OP_ItemPacket`/dispatch drop over a wider gate): even with
  the manager constructed, the CharInventory replay wouldn't have reached
  it under the old gate.

**Two would NOT be caught by this test** (by design, not a gap):
- **T29** (no slash commands registered) needs a command-availability
  check, not a zone-in state-population check.
- **T42**'s `SetEQClientPath()` fix only bites over a *real* network
  connection (`LoadZoneSources()` calling `RequestQuit()` on an empty
  client path) — `ReplayZonePacket` bypasses networking entirely. This is
  exactly why the plan's verification bar separately requires the live
  eqemu acceptance leg below.

### Graphics-mode no-op-ness

Established by, in order: (1) every `.cpp` edit this unit made either
*removed* a preprocessor conditional around code that was already reachable
unconditionally in the graphics-ON build path (the `#ifdef` branch's body is
now what always runs — same code, same behavior, on the graphics-ON side,
since `config.graphicsEnabled`/`m_graphicsInitialized` checks inside those
bodies, where present, are untouched), or (2) `#else` branches removed
entirely only where the `#else` was itself dead for graphics-ON builds
(the CLI-help/`/bank`/`/train` cases). No logic inside any guarded body was
changed, renamed, or reordered — only the preprocessor boundary moved
outward or was deleted. (3) Full 6-step verification bar run on both
`build/` and `build-gles2-tests`-equivalent (`build/` here, since this
worktree's primary dev/test tree is already the GLES2-config tree per
protocol): `ctest` **1836/1837** (`build/`) passed, sole failure the
documented `AudioManagerTest.PreloadCommonSounds` ALSA host exception;
`visual_gate` **PASS** in both `build/` and `build-nogfx/`; `git status`
shows zero changes under any golden/capture path in either tree. (4) The
live eqemu acceptance leg below ran the identical `EQT_GRAPHICS=OFF`
scenario a graphics-ON run would take through `Application::initialize()`'s
now-shared (no longer graphics-gated) hotbar-load call site, with no crash
or behavior deviation logged.

### Verification bar results

1. **Both configures build clean.** `build/` (graphics-ON, GLES2):
   `cmake --build build -j24` — 0 errors (pre-existing `-Wpointer-arith`
   warnings only, unrelated to this unit). `build-nogfx/`
   (`EQT_GRAPHICS=OFF`): `cmake --build build-nogfx -j24` — 0 errors, same
   pre-existing warnings.
2. **`ctest` both trees.** `build/`: **1836/1837** passed — sole failure
   `AudioManagerTest.PreloadCommonSounds` (known ALSA host exception).
   `build-nogfx/`: **1405/1406** passed — same sole failure. No RDP
   failures, no golden-replay `Skipped`.
3. **Golden replay ran, unchanged, both trees.** `visual_gate` test **PASS**
   in both `ctest` runs above; `git status` confirms no changes under any
   golden/capture path.
4. **`bash scripts/build-gles2-x86.sh`**: clean, `build-gles2/bin/willeq` +
   `gles2_zone_benchmark` produced.
5. **`bash scripts/build-arm-noble.sh`**: clean, all 14 binaries produced
   including `willeq`/`model_viewer`.
6. **Live headless acceptance** (`benchmarks/audits/T43/`): eqemu lock taken
   (`T43 <timestamp>`, deleted after clean shutdown), `up.sh qeytoqrg` only,
   `down.sh` at the end. Ran `build-nogfx/bin/willeq` against the real
   server with `--headless --script t43_script.txt` (`wait 15` then
   `/quit`) — `t43_live_nogfx_run.log` shows a real zone-in
   (`[LOADING][Phase 15/15] (100%) Ready!`, `Loaded 100 skills from
   PlayerProfile (14 non-zero)`, `Loaded 97 spawns in zone`,
   `Publishing full state snapshot to bridge`, `Fully connected to zone!`),
   stayed connected for the full 15 real seconds (confirmed via `time`:
   `real 0m15.6s`), then quit cleanly (exit 0) — the exact scenario T42
   found force-quitting within ~2 seconds, now holding for the full
   scripted duration. Character's actual stored zone (`zone_id=36`) turned
   out to already be `befallen`, not `qeytoqrg` as the restore-position
   comment's zone name implied (`zone_id` 36 in this DB maps to `befallen`,
   not the akk-stack instance T42 used) — a pre-existing DB/naming
   mismatch, not something this unit introduced or needed to fix; the
   scenario (real headless zone-in, stays connected, state populated) is
   unaffected by which zone it landed in. Restored Testchar to
   `zone_id=36, x=-77.9232, y=21.9086, z=3, heading=190` via direct
   `UPDATE character_data` (verified with a follow-up `SELECT`) before
   `down.sh`. Lock file removed after clean shutdown.
7. **`check_file_sizes.sh --files`** (absolute paths, all touched
   non-test/test files): no new FAILs. `eq.cpp` remains the one pre-existing
   FAIL (already over threshold at the `fable` baseline per P28's Review);
   `application.cpp`/`eq_entities.cpp`/`eq_bridge.cpp`/`eq_player_info.cpp`
   remain pre-existing WARNs. This unit's diff to each file is 5-24 lines
   (comments + guard removal), not new bulk code.

### STOP conditions hit

None. The misclassified set (8 fix sites, all single call-sites or small
guarded blocks) was well within the ~49 game/boundary-layer occurrences the
plan anticipated — nowhere near "unreviewably large." No un-guard required
restructuring beyond removing a preprocessor conditional. No guard turned
out to protect against something real that neither oracle (build + test)
caught. No graphics-mode behavior change occurred.

### Deferred to its own unit

Nothing new. The one pre-existing item this unit's audit surfaced but did
not touch is `eq.cpp`'s file-size FAIL (already over threshold before this
unit, per P28's Review) — a file-splitting concern, not a graphics-gate
concern, and explicitly out of this unit's territory
(`refactor/F_file_split.md` owns that).
