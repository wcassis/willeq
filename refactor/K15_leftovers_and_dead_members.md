# K15 — The S2 leftovers and the dead members the batch surfaced

Batch: `refactor/K_component_extraction.md`. **Runs after K13 merges**;
alongside K14 (disjoint territory).

Every item below is on `refactor/BOARD.md`, confirmed at the tip named
there. This unit closes them and strikes them. It is the first NON-move-only
K unit: deletions and two signature changes. The proof is the compiler plus
the suite, not a multiset.

## Items

| board | what | action |
|---|---|---|
| B005 | `src/client/ucs.cpp`, `include/client/ucs.h` `UCSConnection`, zero callers; the commented connect path in `WorldProcessSetChatServer` (now in `login_world_component.cpp`); the three `m_ucs_*` members K10 moved | delete TU, header, CMake line, the commented block, the members; tombstone comment at the old call site naming T297's measurement |
| B006 | S2 leftovers: `InitGraphics(int, int, IrrlichtRenderer*, GameStateBridge*)` — read it; if the renderer pointer only reaches `BridgeComponent::InitGraphics`, narrow the parameter to the interface the bridge component actually calls (`initLoadingScreen` / `initDeferredGraphics` — see `src/client/eq_bridge.cpp` history) or keep and RECORD why it cannot narrow; `door_manager.cpp`'s `EverQuest::GetDebugLevel()` → the logging module's level query (find what `LOG_*` macros use); any remaining `irrlicht_renderer.h` include outside `src/client/graphics` that no symbol needs | narrow / replace / delete; measure with the compiler |
| B019 (the first one — `m_guild_name`) | zero references | delete |
| B030 | `m_swim_up`/`m_swim_down` + `SetSwimUp`/`SetSwimDown` | delete (or wire if a swim-path caller exists — measure; expect delete) |
| B031 | `m_last_movement_update` | delete |
| B033 | 14 `IrrlichtRenderer` declarations defined nowhere | delete the declarations |
| B034 | 8 `IrrlichtRenderer` members with zero references | delete |
| B035 | `ZoneComponent::m_spawn_appearance_sent` / `m_client_spawned`, write-only | delete the members and their resets |
| B037 | `m_audio_config_use_3d_audio` settable only by editing the header | wire from `application_config.cpp` like its siblings (read how `m_audio_config_enabled` is wired) |
| numbering | `BOARD.md` carries two **B019** entries | renumber the second (the placeables gate) to the next free ID and fix its one cross-reference |

## STOP conditions

- A "zero references" claim is false at your tip → leave the item, say so.
- Narrowing `InitGraphics` needs a renderer interface that does not exist →
  record the shape, do not invent one.

## Territory

The files each item names, root `CMakeLists.txt` (UCS line),
`include/client/eq.h`, `include/client/graphics/irrlicht_renderer.h`
(declarations/members only — no Batch L work), `include/client/zone/zone_component.h`
+ its `.cpp`, `include/client/bridge/bridge_component.h`,
`src/client/application_config.cpp`, `src/client/graphics/door_manager.cpp`,
`refactor/BOARD.md`, both allowlist numbers (ratchet `eq.h` AND
`irrlicht_renderer.h`). If `door_manager.cpp` or `irrlicht_renderer.h`
changes, the visual gate and ARM steps are NOT skippable.

## Verification / Review

Full bar. `ReachabilitySweep`, `DuplicateStateStores` unchanged. Strike every
closed item on the board with the commit hash.

## Review

Executed alone in `wt/slot2` on `task/K15-leftovers`, branched from `fable` at
**`7c125b28`** (verified). K14 ran concurrently in slot1; no file of its
territory was edited — see "One item touched K14 territory" below.

Every "zero references" claim was RE-MEASURED at this tip with a comment- and
string-filtered `grep -rw` over `src/`, `include/`, `tests/` and `tools/`, and
then by the compiler. **Three claims were FALSE and are recorded as such.**
**One STOP condition fired** (the `InitGraphics` narrowing).

### Per item

| board | measured at `7c125b28` | action | result |
|---|---|---|---|
| **B005** | `UCSConnection` had zero occurrences outside its own two files; the `ConnectToUCS` call site was still commented out; the TU published no `GameEventType` (correction (ff) check: no `pushEvent` / `GameEventType::` in either file) | deleted the TU, the header and the `CMakeLists.txt` line; replaced the commented call with a tombstone naming T297's measurement and the recovery hash | **CLOSED**. **Deviation: the three `m_ucs_*` members were KEPT** — see below |
| **B006** | four sub-claims, split 2 true / 1 false / 1 STOP | see the four rows below | **PARTLY CLOSED** |
| B006 a | `application_config.cpp` includes `irrlicht_renderer.h`, zero `IrrlichtRenderer` tokens | include deleted | closed, compiler-proven |
| B006 b | `application_frame.cpp` — **CLAIM FALSE** | include RESTORED | left; see "the grep that lied" |
| B006 c | `door_manager.cpp` line 946 really does call the static `EverQuest::GetDebugLevel()` | replaced with the logging module's own level query; the game-header include deleted from the TU | closed |
| B006 d | `EverQuest::InitGraphics(int,int,IrrlichtRenderer*,GameStateBridge*)` | **STOP fired** — read it, recorded the shape, invented nothing | left open on the board |
| **B019** (`m_guild_name`) | 1 occurrence tree-wide (its own declaration) | deleted | **CLOSED** |
| **B030** | already closed by K13 — `m_swim_up`/`m_swim_down`/`SetSwimUp`/`SetSwimDown` have **zero** occurrences; only two historical comments remain | verified, skipped | already closed, no change |
| **B031** | 1 occurrence (its own declaration) | deleted | **CLOSED** |
| **B033** | all 14 had exactly 1 occurrence each (their own declaration); the real `adjustOffset*`/`getRotation*` pair is on `EntityRenderer`, and `stepToString` is defined nowhere | 14 declarations deleted | **CLOSED**. The linker never saw them, so nothing could have called them |
| **B034** | **6 of 8 true, 2 FALSE** | 6 members deleted, 2 kept | see below |
| **B035** | `m_spawn_appearance_sent` / `m_client_spawned`: declaration + one `CleanupZone` reset each, plus one already-commented-out line in `src/client/eq.cpp` — no reader | both members and both resets deleted | **CLOSED** |
| **B037** | `m_audio_config_use_3d_audio` had zero writers **and zero readers** | wired end to end, both halves | **CLOSED** |
| numbering | `BOARD.md` carried two `B019`; the highest ID in use was `B039` | the placeables-gate entry (class C) renumbered to **B040**; its one cross-reference, `refactor/K00_truth_reset.md` line 246, fixed with it | **CLOSED** |

### The three false claims

1. **B006's `application_frame.cpp`** — *the grep that lied.* The TU contains
   zero `IrrlichtRenderer` tokens, which is what the S2 survey measured. It
   nevertheless drives the renderer through a typed member pointer:
   `m_renderer->getDriver()`, `m_renderer->processFrame()`,
   `m_renderer->saveScreenshot()`. Removing the include produced three
   `invalid use of incomplete type 'class EQT::Graphics::IrrlichtRenderer'`
   errors. **A name-token census cannot see a use through a typed member
   pointer; only the compiler can.** Same class as K11's correction (cc).
   The include is restored and the item is boarded as corrected.

2. **B034's `chatPrefillMutex_` / `pendingChatPrefill_`** — the Batch L survey
   is older than **T319**, which wired them. `requestChatPrefill()` writes on
   the game thread from `IrrlichtRenderer::handleActionResult`
   (`src/client/graphics/irrlicht_renderer.cpp`), `takeChatPrefill()` reads on
   the render thread in `src/client/graphics/renderer_frame_input.cpp`, and
   `tests/test_reply_to_tell_key.cpp` covers the round trip. Both KEPT. Six of
   the eight were true and are gone.

3. **B037's premise in `bridge_component.h`** — the K11 note said the flag "is
   still read by `InitializeAudio()`". It was not read by anything at all. Had
   K15 wired only a config source, as the item table asked, the result would
   have been a config knob that silently does nothing — the exact defect class
   this programme hunts. Both ends were wired instead.

### The `InitGraphics` decision (B006 d) — STOP, with the shape

`EverQuest::InitGraphics` is a sanctioned forwarder; the body is
`eqt::bridge::BridgeComponent::InitGraphics`
(`src/client/bridge/bridge_component.cpp`). **Read at this tip, the `renderer`
parameter reaches exactly two calls and nothing else:**

    if (!renderer->initLoadingScreen(config)) { ... }    // bool, takes RendererConfig
    if (renderer) { renderer->initDeferredGraphics(); }  // void, no args

Nothing else in the body dereferences it — the renderer is not stored, not
passed on, and not compared. So the parameter CAN narrow, and the interface it
would narrow to is exactly:

    // a new header under include/client/graphics/ -- DOES NOT EXIST TODAY
    namespace EQT { namespace Graphics {
    class IGraphicsInit {
    public:
        virtual ~IGraphicsInit() = default;
        virtual bool initLoadingScreen(const RendererConfig& config) = 0;
        virtual void initDeferredGraphics() = 0;
    };
    }}

with `class IrrlichtRenderer : public IGraphicsInit` and the parameter of both
`EverQuest::InitGraphics` and `BridgeComponent::InitGraphics` becoming
`IGraphicsInit*`.

Two facts make this cheap when someone rules on it: `RendererConfig` already
lives in `include/client/graphics/renderer_types.h`, **not** in
`irrlicht_renderer.h`, so the new header would not drag the 1,902-line renderer
header into `eq.h`; and both methods are already non-virtual publics with a
single implementation.

**The plan's second STOP condition says the interface does not exist and must
not be invented, so K15 stopped here.** It is boarded under B006 as the open
half. Note the two callers that would also change: `tests/` has two
(`test_zoning_graphics_integration.cpp`, `test_beneficial_spell_casting.cpp`),
plus `src/client/application.cpp` — and both `eq.h` and `bridge_component.h`
would lose an `IrrlichtRenderer` forward declaration, which is the actual
coupling win.

### B037 — the wiring, end to end

    "audio": { "use_3d": false }   (JSON)   ─┐
    --no-3d-audio                  (CLI)   ─┴─> AppConfig::audioUse3D
                                                 (include/client/application.h)
      -> Application::init: m_eqClient->Bridge().SetUse3DAudio(config.audioUse3D)
      -> BridgeComponent::m_audio_config_use_3d_audio
      -> InitializeAudio(): ctx_.audio_manager->setUse3DAudio(...)
      -> AudioManager::use3DAudio_
      -> AudioManager::playSoundInternal(): gates the sfxManager_->playSpatial()
         branch. Off = every sound plays centred at masterVolume_*effectsVolume_,
         no distance attenuation and no panning.

`playSoundInternal` already had exactly the spatial/centred branch this switch
needed, so the consumer is one condition, not a new feature. This is a small
territory extension beyond the item table (which named only
`application_config.cpp`): `include/client/application.h`,
`src/client/application.cpp`, `include/client/audio/audio_manager.h` and
`src/client/audio/audio_manager.cpp` were touched, each by a few lines. Flagged,
not absorbed.

### Deviations

1. **B005: the three `m_ucs_*` members were NOT deleted.** The item table asked
   for them; the plan's own STOP condition forbids it. `m_ucs_host`,
   `m_ucs_port` and `m_ucs_connection_type` are written by
   `WorldProcessSetChatServer` and read through the public
   `GetChatServerHost()` / `GetChatServerPort()` / `GetChatConnectionType()` by
   **13 assertions** across `tests/test_login_world_flow.cpp` and
   `tests/test_login_world_component.cpp`. They carry the four-field wire
   contract T13 fixed, which is worth keeping whether or not a UCS client ever
   exists. Deleting them would have deleted tested behaviour, not dead code.
2. **B037's territory extension**, above.
3. **One item touched K14 territory and was left alone.** `src/client/eq.cpp`
   line 846 carries `// m_client_spawned = false; // Deprecated` — an
   already-commented-out line naming a member B035 deletes. It is inert and
   `eq.cpp` is K14's file this wave, so it was NOT edited. One-line follow-up
   for whoever merges second.
4. **The board strikes name the unit, not the commit hash.** A commit cannot
   contain its own hash. Strikes read "CLOSED by K15"; the hash is in the final
   report and in the merge commit.
5. **`include/client/eq.h` needed a comment trim.** The first build left it at
   917 lines against a 916 allowlist — the replacement tombstones were one line
   longer than what they replaced, and the ratchet is no-grow. Two comment lines
   were tightened; final 915.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** (first attempt exit 2 — the `application_frame.cpp` include, see false claim 1) |
| `ctest --test-dir build -j6` | **3604 / 3605**, sole failure `AudioManagerTest.PreloadCommonSounds` (the ALSA host quirk) |
| `ReachabilitySweep` (5 cases) | all Passed, unchanged |
| `DuplicateStateStores.EverQuestKeepsNoCopyOfAStateModuleMember` | Passed, unchanged |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/ci/visual_gate.sh` | **=== VISUAL GATE: PASS ===**, goldens untouched (`git status` clean under `scripts/ci/goldens`) |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `scripts/refactor/check_file_sizes.sh --all` | 830 files, **0 fail, 0 warn** |

An intermediate ctest run also failed `FileSizeCeiling` (the 917-line eq.h) and
`DocCitations` (BOARD.md citing the just-deleted UCS paths and an include
spelling that did not resolve to a repo path). Both were real and both are fixed; the final
run is the one above.

### Header ratchet

| header | before | after | delta |
|---|---|---|---|
| `include/client/eq.h` | 916 | **915** | -1 |
| `include/client/graphics/irrlicht_renderer.h` | 1914 | **1902** | -12 |

Both allowlist entries in `scripts/refactor/file_size_allowlist.txt` ratcheted
to the new counts.

### Board

Struck: **B005**, **B019** (`m_guild_name`), **B031**, **B033**, **B035**,
**B037**. **B006** partly struck with its two open halves spelled out.
**B034** left open deliberately as the record of its own correction — six
members deleted, two proven live. **B030** verified already closed by K13.
The duplicate **B019** (C, placeables gate) renumbered to **B040**.
