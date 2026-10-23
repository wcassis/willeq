# K11 — `BridgeComponent` (family: bridge — event/intent pumps, loot window, config I/O, audio/graphics lifecycle, zone-load snapshot)

Batch: `refactor/K_component_extraction.md` §Mechanism (REVISED through
K10). **Wave 4; runs alongside K12 AFTER K09 merges.**

The batch table originally said this family "becomes bridge/intent handlers,
not a component". That is a design step, not a mechanical one; the mechanical
step is the same extraction as K01–K10, and it is what makes the later design
step delegable. Do the extraction. Folding `BridgeComponent` into
`GameStateBridge`/`IrrlichtBridge` is K13-or-later.

## Measured at `dfd36b06` (2026-09-03) — re-measure at your tip

Three TUs, 45 bodies: `eq_bridge.cpp`, `eq_bridge_intents.cpp`
(`ProcessBridgeIntents`, 580 lines, moves WHOLE — Batch G), `eq_bridge_ui_audio.cpp`.
Callers are the APPLICATION layer: `application*.cpp` (`InitGraphics`,
`ShutdownGraphics`, `GameTick`, `PostRenderTick`, `ProcessBridgeEvents`,
`CreateZoneLoadSnapshot`, `SetZoneBspTree`, `LoadHotbarConfig`,
`LoadInspectMessage`, `wireBridgeToSubsystems`, `setBridge`), the renderer
(`OnGraphicsMovement` from `renderer_player_physics.cpp`; `InitGraphics`
from `renderer_zone_load_env.cpp` — read it, may be a comment), the
extracted components (`OnPet*`, `OnSpawn*Graphics`, `UpdateDayNightState`,
`UpdateInventoryStats`, `PlaySound`, `CloseLootWindow`, `LootItemFromCorpse`,
`RequestLootCorpse` — rewrite as `owner_.Bridge().X()`), `combat*.cpp`,
`eq_action_handler*.cpp`, `input/input_action_bridge.cpp`,
`static_ui_input.cpp`, `command_processor_loot.cpp`.

⚠ `setBridge` / `InitGraphics` / `GameTick` are `EverQuest`'s LIFECYCLE
entry points, called by `Application`. Move their bodies, but keep the
`EverQuest` entry points as the ONLY sanctioned forwarders in the batch —
`Application` must not need to know a component exists to start the game.
Say so in the header comment and count them in the Review (expect ≤ 5).

Members (single-family → move): `m_audio_config_*` (5), `m_config_path`,
`m_is_daytime`, `m_region_maps_path`. `m_instance`/`m_slot`/`m_Struct` are
other classes' names — ignore. Shared → `owner_.`: the zone-environment set
K09 left for this unit (`m_zone_map`, `m_zone_bsp_tree*`, `m_zone_fog_*`,
`m_zone_sky_type`, `m_zone_type`, `m_eq_client_path`, `m_zone_audio_manager`,
`m_hotbar_model`, `m_loot_items`, `m_pending_loot_slots`,
`m_loot_complete_corpse_id`, deleted by B1133) — **this unit moves the loot cluster** (the
loot window is bridge-facing; K05/K06 recorded it as K11's) and the
zone-environment set if their only readers are here + `eq.cpp`; record each.

Predicted `eq.h` delta: −40 declarations −8 members −(loot + env cluster,
≈15) +4 ≈ **−55 or better**.

## Territory

`include/client/bridge/bridge_component.h` (new — `include/client/bridge/`
already holds `GameStateBridge`; do not touch those files),
`src/client/bridge/eq_bridge*.cpp` → `src/client/bridge/bridge_component*.cpp`
(move), `eq.h`, `eq.cpp`, root `CMakeLists.txt`, `tests/test_bridge_component.cpp`
+ cmake, allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in the callers
the compiler names, including `application*.cpp`, `src/client/*/` components
and `src/client/graphics/renderer_player_physics.cpp` (one call site — if it
changes, the visual gate and ARM steps are NOT skippable). K12 runs alongside
and edits `eq_action_handler*.cpp` at its own lines.

## Verification / Review

Executed in `wt/slot1` on `task/K11-bridge-component`, branched from `fable` at
`233ff30b`. Ran alongside K12 (slot2). §Mechanism REVISED followed as written,
including every correction block K03-K10. **No STOP condition was hit.**

### The three `eq.h` numbers

| | lines |
|---|---|
| at dispatch (`fable` `233ff30b`) | **1,226** |
| plan's pre-dispatch estimate | -55 or better |
| **predicted** before any edit (14 explicit line blocks) | **1,073** (-153) |
| **measured** after | **1,078** (**-148**) |

`git diff --numstat -M` on the single file: **174 deletions, 26 insertions =
net -148**. The five-line miss is comment, not code: the notes left in place of
the deleted `setBridge` T46 block, the deleted loot senders and the deleted
audio block are three, six and six lines where the arithmetic assumed a
one-line stub each. The plan's own estimate is beaten by a factor of nearly
three, for the third unit running: the plan counted declarations and members,
not the ~70 banner/comment lines that travel with them.

`scripts/refactor/file_size_allowlist.txt`'s `eq.h` entry lowered
**1,226 -> 1,078**. `check_file_sizes.sh --all`: **821 ok, 0 warn, 0 FAIL**.

### THE FORWARDERS: five, exactly as sanctioned

| forwarder | body now | callers that keep the `EverQuest` receiver |
|---|---|---|
| `setBridge` | `BridgeComponent::setBridge` | `application.cpp` (2) + **~100 test fixture ctor/dtor pairs across 45 test files** |
| `GameTick` | `BridgeComponent::GameTick` | `application_frame.cpp`, `test_spell_manager_timing` (2) |
| `PostRenderTick` | `BridgeComponent::PostRenderTick` | `application_frame.cpp` |
| `InitGraphics` | `BridgeComponent::InitGraphics` | `application.cpp`, `test_beneficial_spell_casting`, `test_zoning_graphics_integration` |
| `ShutdownGraphics` | `BridgeComponent::ShutdownGraphics` | `application.cpp` |

All five bodies are one line, in `src/client/eq.cpp` next to `SetConstrainedConfig`
(not in the moved TU, so the residual proof below stays clean). **`setBridge`
alone justifies the exception**: it is called on `EverQuest` by ~100 test
fixture lines in 45 files that have nothing to do with the bridge -- they wire
a `TestBridge` so the client under test can publish at all.

**`ProcessBridgeEvents` is NOT a forwarder**, even though `Application` calls
it: it is a per-frame drain, not a lifecycle transition, and `GameTick`
already covers the tick contract. It, `ProcessBridgeIntents`,
`CreateZoneLoadSnapshot`, `SetZoneBspTree`, `LoadHotbarConfig` and
`LoadInspectMessage` were retargeted at `Application`'s call sites.
`Application` therefore does name `Bridge()` in six places -- what it does
NOT have to do is know a component exists to *start, tick and stop the game*.

### THE CUT: 45 bodies, 40 moved, 5 deleted

`grep -nE '^[A-Za-z_].*EverQuest::'` over the three TUs at this tip returns
**45**, exactly the plan's count: `eq_bridge.cpp` 21, `eq_bridge_ui_audio.cpp`
22, `eq_bridge_intents.cpp` 2. `ProcessBridgeIntents` (580 lines) moved WHOLE
and needed nothing beyond the standard substitutions.

**Five zero-caller bodies were DELETED** (§Mechanism 5, K03 correction (a)
applied -- zero callers *including in-family*; each had exactly two
occurrences tree-wide, its own declaration and its own definition):

- **`SaveEntityDataToFile(const std::string&)`** -- 41 code lines. Dumped the
  whole `m_entities` map to a JSON file "for debugging". No command, no
  intent, no test reaches it.
- **`PlaySoundAt`, `PlayCombatSound`, `PlaySpellSound`, `PlayUISound`** -- 19
  code lines between them. The positional/typed sound helpers.
  `PlaySound(uint32_t)`, the one with a caller
  (`MovementComponent` water entry), was kept and moved.

`SetUse3DAudio()` (inline, zero callers) went with them, leaving
`m_audio_config_use_3d_audio` write-only-at-default; it is still READ by
`InitializeAudio`, so §Mechanism 3's zero-reference rule does not reach the
member and it moved, marked in the header. Same shape as K09's
`m_spawn_appearance_sent`. Boarded below.

**Six more dead inline accessors were deleted** (K10 correction (s), applied
to the family's name prefixes): `GetEQClientPath`, `GetConfigPath`,
`IsDaytime`, `GetZoneAudioManager`, `GetLootWindowCorpseId`,
`GetConstrainedPreset`/`SetConstrainedPreset`/`GetConstrainedConfig`. Each
had exactly one occurrence tree-wide -- its own definition.
A tree-wide re-check of the whole header afterwards
(every non-inline declaration vs `grep -rl 'EverQuest::<name>' src/`) found
**zero declared-but-undefined methods**: K10's two-dead-promise finding does
not repeat here either.

### Compiler-derived caller list

The whole retarget was computed mechanically BEFORE the first build (K08
correction 4, script rewritten for this family): parse `eq.h` for the class's
declarations and members, mask comments and string literals per line,
classify every `m_*` into core / other-component / mover / stays-behind, scan
the movers for bare `identifier(` against the declaration set, and emit the
substitution list. The declarations were deleted and every call site
retargeted in one pass. **The build then failed with exactly ONE error**
(`EverQuest::GameLootItem` in `merchant_trade.cpp` -- the nested type travels
with the member, K05 correction (j)), then with **22 more in four test files**
that the scanner had been blind to, then went clean.

⚠ **The 22 misses are a scanner bug worth recording (correction 1 below): the
receiver expression was `h.eq.` -- a two-hop chain.** The scan regex anchored
on `(?<![\w:.>])<receiver>?<Name>\(`, which cannot match when the receiver
itself is preceded by a `.`. A word grep would have found them; a
receiver-aware grep did not. The compiler did.

| file | sites | receiver expression | change |
|---|---|---|---|
| `src/client/group/group_component.cpp` | **20** | `owner_.` | `OnPetButtonStateChanged` (the whole `/pet` button block) |
| `src/client/application.cpp` | **12** | `m_eqClient->` | `SetRegionMapsPath`, `SetConfigPath`, the six audio-config setters, `LoadHotbarConfig`, `LoadInspectMessage`, `CreateZoneLoadSnapshot`, `SetZoneBspTree` |
| `src/client/zone/zone_dispatch.cpp` | 8 | `owner_.` | `CloseLootWindow`, `m_loot_items`, `m_pending_loot_slots` (6) |
| `src/client/entities/entities_component_appearance.cpp` | 8 | `owner_.` | `OnPetRemoved`, `OnSpawnRemovedGraphics`, `UpdateInventoryStats`, `m_loot_items`, `m_pending_loot_slots`, `m_loot_complete_corpse_id` (3) |
| `src/client/entities/entities_component_updates.cpp` | 7 | `owner_.` | `OnSpawnAddedGraphics`, `OnPetCreated` (2), `OnPetButtonStateChanged` (2), `OnSpawnMovedGraphics` (2) |
| `src/client/eq_action_handler.cpp` | 7 | `m_eq.` | `GetLootItems` (2), `LootItemFromCorpse`, `FindLootSlotByName`, `CloseLootWindow`, `UseInventoryItem`, `ActivateHotbarCommandSlot` |
| `src/client/merchant/merchant_trade.cpp` | 8 | `owner_.` | `m_pending_loot_slots` (5), `m_loot_items` (2), `EverQuest::GameLootItem` -> `eqt::bridge::BridgeComponent::GameLootItem` |
| `src/client/eq_action_handler_spells_trade.cpp` | 4 | `m_eq.` | `GetInspectMessage` (2), `SetInspectMessage`, `SaveInspectMessage` |
| `src/client/entities/entities_component.cpp` | 3 | `owner_.` | `OnPetCreated`, `OnPetButtonStateChanged`, `UpdateDayNightState` |
| `src/client/combat_rounds.cpp` | 3 | `m_eq->` | `RequestLootCorpse`, `LootItemFromCorpse`, `CloseLootWindow` |
| `src/client/merchant/merchant.cpp` | 2 | `owner_.` | `UpdateInventoryStats` (2) |
| `src/client/eq.cpp` | 1 | bare, in `~EverQuest` | `ShutdownAudio` -> `m_bridge_component->` |
| `src/client/application_frame.cpp` | 1 | `m_eqClient->` | `ProcessBridgeEvents` |
| `src/client/application_zone_loading.cpp` | 1 | `m_eqClient->` | `CreateZoneLoadSnapshot` |
| `src/client/movement/movement_state.cpp` | 1 | `owner_.` | `PlaySound` |
| `src/client/player/player_buffs.cpp` | 1 | `owner_.` | `UpdateInventoryStats` |
| `src/client/player/player_component.cpp` | 1 | `owner_.` | `m_inspect_message` -> `owner_.Bridge().GetInspectMessage()` |
| `src/client/zone/zone_component.cpp` | 1 | `owner_.` | `OnPetRemoved` |
| `src/client/zone/zone_entry.cpp` | 1 | `owner_.` | `UpdateInventoryStats` |
| `tests/test_inspect_answer.cpp` | **23** | `eq.` | `SetConfigPath`, the inspect setter/getter/save/load. **Assertions untouched** |
| `tests/test_zone_connection_flow.cpp` | 9 | `eq.` | `CreateZoneLoadSnapshot` (7), `SetZoneBspTree` (2). **Assertions untouched** |
| `tests/test_eq_bank_coins.cpp` | 9 | `h.eq.` | `ProcessBridgeIntents` |
| `tests/test_tradeskill_combine.cpp` | 6 | `h.eq.` | `ProcessBridgeIntents` |
| `tests/test_scribe_scroll.cpp` | 5 | `h.eq.` | `ProcessBridgeIntents` |
| `tests/test_loot_window_actions.cpp`, `test_position_frame_roundtrip.cpp` | 4 each | `eq.` | `ProcessBridgeIntents` |
| `tests/test_eq_action_handler_cast.cpp` | 3 | `h.eq.` | `ProcessBridgeIntents` |
| `tests/test_eq_action_handler_hotbar_command.cpp` | 3 | `h.eq.` | `ActivateHotbarCommandSlot` |
| `tests/test_beneficial_spell_casting.cpp` | 2 | `eq_->` | `ProcessBridgeEvents` |
| `tests/test_buff_click_off.cpp`, `test_spellbook_memorize.cpp`, `test_read_book.cpp`, `test_game_state_source_writes.cpp` | 2 each | `eq.` / `h.eq.` | `ProcessBridgeIntents`, `OnPetButtonStateChanged` |

**Total: 165 real call sites in 32 files** (18 production, 14 test). Every
retarget in an already-extracted component was written
`owner_.Bridge().Foo()` per K06/K07 correction (r).

**K12's territory was not entered.** `eq_commands*.cpp` and
`eq_combat_actions.cpp` between them carry **eleven** mentions of this
family's names and **every one is a prose comment** (K04 correction (g)) --
`eq_combat_actions.cpp:13` even says "the loot-corpse family ... lives in
eq_bridge.cpp". No code in a K12-moving TU calls a K11-moving method or
touches a K11-moving member, so no cross-family hunk exists to resolve at
merge. The two `eq_action_handler*.cpp` files WERE edited, at this family's
own 11 lines.

### The loot / zone-environment cluster decision table

The plan asked for each member of the K09-punted set to be recorded, with
"move if their only readers are here + `eq.cpp`". The measured answer splits
cleanly: **the loot cluster moved, the zone-environment set did not**, and the
reason is that the zone environment is written by the ZONE family and read by
the bridge, while the loot window is written and read by the bridge and only
touched by the packet handlers that feed it.

| member | readers outside the bridge family | decision |
|---|---|---|
| `m_loot_items` (+ `struct GameLootItem`, + `GetLootItems()`) | `merchant_trade.cpp` (2), `entities_component_appearance.cpp` (1), `zone_dispatch.cpp` (1), `eq_action_handler.cpp` (2 via the getter) | **MOVED**, with `LootItems()` + the const `GetLootItems()`. (`CombatManager::m_loot_items` in `combat_hunting.cpp`/`combat_rounds.cpp`/two tests is a **different class's same-named member** -- K10 correction (t), the fourth instance) |
| `m_pending_loot_slots` | `merchant_trade.cpp` (5), `zone_dispatch.cpp` (6), `entities_component_appearance.cpp` (1) | **MOVED**, `PendingLootSlots()` |
| `m_loot_complete_corpse_id` | `entities_component_appearance.cpp` (3) | **MOVED**, `LootCompleteCorpseId()` [B1133: member and accessor deleted with the DeleteSpawn loot gate] |
| `m_zone_map` (+ `GetZoneMap()`) | `zone_component.cpp` (**8**), `spell_cast_checks.cpp`, `test_eq_action_handler_cast.cpp` | **STAYS** -- zone owns the load and the teardown; the bridge only snapshots it |
| `m_zone_bsp_tree`, `m_zone_bsp_tree_zone_name` | `zone_component.cpp` (13), `movement_state.cpp` (1) | **STAYS**, same reason |
| `m_zone_sky_type`, `m_zone_type`, `m_zone_fog_red/green/blue/minclip/maxclip` | `zone_entry.cpp` -- **all seven are WRITTEN by `ZoneProcessNewZone`** and read only by `CreateZoneLoadSnapshot` | **STAYS**. Moving them would make the zone family write through `owner_.Bridge()` on a packet-parse hot path for no gain |
| `m_eq_client_path` (+ `GetEQClientPath()`) | `zone_component.cpp` (4), `zone_entry.cpp` (2), and `EverQuest::SetEQClientPath` in `eq.cpp` | **STAYS**; its dead getter deleted |
| `m_zone_audio_manager` | `zone_entry.cpp` (2) and **`eq_commands.cpp` (3)** -- a K12-moving file | **STAYS**. Moving it would have forced an edit inside a TU K12 is moving, which the plan forbids without orchestrator resolution; its dead getter deleted |
| `m_hotbar_model` (+ `GetHotbarModel()`) | `eq.cpp` ctor (2, incl. a lambda closing over `m_spell_manager`), `application.cpp` (3), `test_bridge_teardown.cpp` (2) | **STAYS -- deviates from a strict reading of "here + eq.cpp".** It is a SUBSYSTEM HANDLE, constructed and wired in the ctor beside `m_inventory_manager`/`m_command_registry`/`m_skill_manager`, three of which are already `ClientContext` core members, and its public accessor serves the input/Application layer. It belongs in `ClientContext` at K13, not in one family |
| `m_inspect_message` (+ `GetInspectMessage()`) | `player_component.cpp` (1 read, answering an inspect) | **MOVED** -- executing K08's recorded hand-off ("`m_inspect_message` is the bridge's, not the player's"; three of its four writes and `SetInspectMessage()` were always bridge bodies) |
| `m_config_path` (+ `SetConfigPath`/`GetConfigPath`), `m_region_maps_path` (+ `SetRegionMapsPath`) | none outside the family | **MOVED**; `GetConfigPath` was dead and deleted |
| `m_is_daytime` (+ `IsDaytime()`), the seven `m_audio_config_*` (+ seven inline setters) | none outside the family | **MOVED**; `IsDaytime` and `SetUse3DAudio` were dead and deleted |
| `m_graphics_initialized`, `m_constrained_preset`, `m_constrained_config`, `m_loading_status_ptr`, `m_zone_load_requested` | `eq.cpp` (`GetLoadingProgress`, `OnGameStateComplete`, `OnGraphicsComplete`, `SetConstrainedConfig`) | **STAY**, `owner_.m_*` -- renderer-LIFETIME state Application wires around the forwarders, read by four `eq.cpp` bodies that are not this family's. Same standing K09 gave `m_zone_session_established`. Three of their inline accessors were dead and deleted; `SetLoadingStatus`, `ConsumeZoneLoadRequest` and `SetConstrainedConfig` keep their callers |
| core | `ctx_.` | 12 of the 15-member core used: `m_game_state` (61), `m_bridge` (48), `m_audio_manager` (32), `m_entities` (23), `m_combat_manager` (22), `m_inventory_manager` (17), `m_spell_manager` (14), `m_skill_manager` (6), `m_character` (3), `m_buff_manager` (2), `m_trade_manager` (2), `m_active_tradeskill_object_id` (1) | `ctx_.` |

**14 members moved** plus one nested struct (`GameLootItem`) -- the plan
predicted 8 + "the loot cluster + the env cluster, ~15"; the env cluster
stayed and the loot cluster plus `m_inspect_message` came in instead.

**Inline `eq.h` accessors on a moving member (K04 correction (f)): fifteen** --
`GetInspectMessage`, `SetConfigPath`, `GetConfigPath`, `SetRegionMapsPath`,
`GetLootItems`, `IsDaytime`, and the seven `Set*` audio-config setters, plus
`GetLootWindowCorpseId`. **None is an `EverQuest::` body, so no method count
sees them, and between them they carried 41 of the 165 call sites** (23 in
`test_inspect_answer.cpp` alone). Six of the fifteen turned out to have zero
callers and were deleted rather than moved.

### Dependencies no lead table named

Found by cross-referencing every bare `identifier(` in the moving files
against `eq.h`'s declaration set before building: **17 stays-behind methods,
44 call sites**, all rewritten `owner_.Foo()` -- `AddChatSystemMessage` (27),
`CurrentZoneName` (8), `PosX`/`PosY` (8 each), `PosZ` (7), `MySpawnId` (7),
`QueuePacket` (4), `IsBankWindowOpen` (3), `TickNetwork` (3), `HeadingDeg`
(2), and one each of `CurrentZoneId`, `IsFullyZonedIn`, `IsGraphicsReady`,
`IsTrainerWindowOpen`, `IsVendorWindowOpen`, `SetHeadingLocal`, `SetPosLocal`,
`SetPosition`.

Other components reached through `owner_.<Family>()`: `Merchant()` (21),
`Entities()` (5), `Zone()` (5), `Movement()` (3), `Chat()` (2), `Player()` (2).

`EverQuest`-scoped names needing the third substitution: **`s_debug_level`
(2 sites only** -- far fewer than K08's 23 or K09's 29; the bridge layer logs
through `LOG_*`, not through the debug-level ladder).

**`this` -> `&owner_` (K06/K07 correction (n)): 5 sites, and THREE OF THEM
MUST NOT BE SUBSTITUTED** -- see correction 2 below. Two are
`EQ::castRefusalMessage(*this, result)`, which needs an `EverQuest&` and
became `castRefusalMessage(owner_, result)`. Three are lambda CAPTURES
(`std::visit([this]...)` in `ProcessBridgeIntents`, the `networkTick` lambda
in `InitializeAudio`, and the hotbar `setSaveCallback([this]{...})`), where
`this` must stay `this` -- it now captures the COMPONENT, and the lambda body
reaches `owner_`/`ctx_` through it.

### Multiset residual (§Mechanism 8)

Normalisation applied to both sides, longest-name-first (K04 correction (i)):
`eqt::bridge::BridgeComponent::` -> `EverQuest::`;
`owner_.<Family>().` -> `m_<family>->` for the six component accessors;
`ctx_.<core>` -> `m_<core>` for the twelve core members used;
`EverQuest::s_debug_level` -> `s_debug_level` (K06 correction 6);
`castRefusalMessage(owner_,` -> `castRefusalMessage(*this,`;
`owner_.m_` -> `m_`; `owner_.` -> ``; trailing whitespace and trailing `//`
comments stripped (K08 correction (w), with string literals masked);
comment-only, blank and preprocessor lines filtered; both sides sorted.

Old side: the three `eq_bridge*.cpp` at `233ff30b` -- **1,382 code lines**.
New side: the three `src/client/bridge/bridge_component*.cpp` --
**1,322 code lines**.

**Residual: 0 additions, 60 deletions, ZERO modifications.** The 60 are
exactly the five zero-caller bodies' code lines (`SaveEntityDataToFile` 41,
`PlaySoundAt` 5, `PlayCombatSound` 6, `PlaySpellSound` 4,
`PlayUISound` 4). **Every other line is byte-identical between the two sorted
multisets**, including all 580 lines of `ProcessBridgeIntents`. The move is
verbatim.

The scaffolding residual lands where §Mechanism 8 says it must: `eq.h`
(-148), `src/client/eq.cpp` (+34: the component construction plus the five
forwarders) and the new 300-line header. **No body was moved out of
`eq.cpp`**, so K10 correction (u)'s second residual run does not apply.

### Text readers of the moved TUs (K03 correction (c))

`grep -rn 'eq_bridge' tests/ scripts/ docs/ refactor/BOARD.md CLAUDE.md CMakeLists.txt`
BEFORE moving anything: **46 hits.** Forty-two are prose comments. **Four are
CODE, and they are the K03(c) trap in its purest form:**

- `tests/test_reachability_sweep.cpp:221` and `:259` --
  `readFile(kRoot / "src/client/eq_bridge_intents.cpp")`. This is the sweep
  that asserts **every one of the 35 `RendererIntent` variant members has a
  consumer arm in `ProcessBridgeIntents`**. `readFile` on a missing path
  returns an empty string, so the test would have kept passing over an empty
  buffer -- exactly what `test_chat_channel_names` would have done at K03.
- `tests/test_reachability_sweep.cpp:238` and `:239` -- the two
  `if (f.rel == "src/client/eq_bridge*.cpp") continue;` exclusions in
  `ProducerlessIntentsAreExactlyTheBlockedList`. Stale, these would have
  counted the CONSUMER arms as producers and silently emptied the
  producer-less set.

All four repointed in-unit (plus the failure message's file name).
`ReachabilitySweep.EveryIntentHasAConsumerArm`,
`ProducerlessIntentsAreExactlyTheBlockedList` and the other three sweeps all
pass. `test_duplicate_state_stores.cpp` needed **no** edit: K09 widened its
input set to `eq.h` PLUS `include/client/**/*_component.h`, and the new
component header is picked up automatically -- §Mechanism 7's prediction
holds, and every unit from K11 on really does inherit the fix.

Doc citations repointed: `docs/module_map.md` rows 25/52/53 and the family
row 80 (full repo-relative paths, no "(was ...)" prose -- K05 correction (l));
`refactor/BOARD.md` B006 (the S2-leftover TU list) and B025 (the oversized-method
list, which cited `EverQuest::ProcessBridgeIntents` in `eq_bridge.cpp`);
`CLAUDE.md:280` ("the boundary TU"); `docs/decisions.md:433`.
`DocCitations` passes.

**S2 leftovers closed (batch doc, "The S2 leftovers"):** two of B006's eight
vestigial `client/graphics/irrlicht_renderer.h` includes sat in this family
(`eq_bridge_ui_audio.cpp`, `eq_bridge_intents.cpp`) and neither TU names
`IrrlichtRenderer`. Both removed; the build is green without them. B006 is now
down to `application_frame` and `application_config` plus the
`InitGraphics(IrrlichtRenderer*, ...)` parameter and the `EQT::Graphics` asset
loaders.

### Files

| file | lines |
|---|---|
| `include/client/eq.h` | 1,226 -> **1,078** (-148) |
| `src/client/eq_bridge.cpp` -> `src/client/bridge/bridge_component.cpp` | 890 -> **842** (`SaveEntityDataToFile`) |
| `src/client/eq_bridge_intents.cpp` -> `src/client/bridge/bridge_component_intents.cpp` | 703 -> **702** (the vestigial include) |
| `src/client/eq_bridge_ui_audio.cpp` -> `src/client/bridge/bridge_component_ui_audio.cpp` | 744 -> **710** (four dead Play* + the vestigial include) |
| `include/client/bridge/bridge_component.h` | **300 (new)** |
| `tests/test_bridge_component.cpp` | **141 (new)** |
| `src/client/eq.cpp` | 1,081 -> **1,115** (+34: ctor line + the five forwarders) |

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**. Five attempts, none timed out (no exit 143, so no truncation scan was needed): (1) the one-pass declaration-deletion + precomputed retarget, exit 2, **exactly ONE error** (`EverQuest::GameLootItem`); (2) exit 2, 22 errors, all `h.eq.ProcessBridgeIntents` in four test files the receiver-aware scan could not see; (3) exit 0; (4) exit 0 with `test_bridge_component` added; (5) exit 0 after removing the two vestigial `irrlicht_renderer.h` includes |
| `ctest --test-dir build -j6 --output-on-failure` | **3,580 / 3,581 passed**, 17 skipped, 204.75 s. **1 failure: `AudioManagerTest.PreloadCommonSounds` only** (the known ALSA quirk). `FileSizeCeiling`, `DocCitations`, all 5 `ReachabilitySweep.*`, `DuplicateStateStores.*` and `visual_gate` (Test #3576, 25.53 s, inside this same run) all **PASS**. No intermediate red run |
| `tests/test_bridge_component.cpp` (new) | **9/9 pass**: `BridgeReturnsTheSameComponentEveryTime`, `LifecycleEntryPointsStayedOnEverQuest`, `InspectMessageMovedWithItsAccessorPair`, `LootClusterMovedWithItsAccessors`, `FindLootSlotByNameRefusesWithNoOpenWindow`, `TheLootWindowFlagStayedOnEverQuest`, `ConfigPathSettersMovedWithTheirMembers`, `AudioConfigSettersMovedWithTheirMembers`, `TheBridgePumpsMovedAndAreReachedThroughTheAccessor` |
| retargeted suites | `test_inspect_answer`, `test_zone_connection_flow`, `test_eq_bank_coins`, `test_tradeskill_combine`, `test_scribe_scroll`, `test_loot_window_actions`, `test_position_frame_roundtrip`, `test_buff_click_off`, `test_spellbook_memorize`, `test_read_book`, `test_eq_action_handler_cast`, `test_eq_action_handler_hotbar_command`, `test_beneficial_spell_casting`, `test_game_state_source_writes` -- **all pass, every assertion untouched** |
| `bash scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate (standalone) / `bash scripts/build-arm-noble.sh` | **SKIPPED, with evidence**: `git status --short -- src/client/graphics include/client/graphics` is **empty** and so is the same-path `git diff --name-only` against the merge base -- this unit changed no graphics file. The plan's one predicted graphics call site (`renderer_player_physics.cpp`'s `OnGraphicsMovement`) turned out to be **two prose comments**, and `renderer_zone_load_env.cpp:616`'s `InitGraphics` is a comment too (K04 correction (g)). Goldens untouched. `visual_gate` nonetheless ran and PASSED inside the full ctest run above |
| `bash scripts/refactor/check_file_sizes.sh --all` | **821 ok, 0 warn, 0 FAIL** with the ratchet lowered to 1,078 |

### Corrections to §Mechanism

1. **A receiver-aware caller scan MUST allow a receiver CHAIN, not one hop.**
   K03 correction (b) taught executors to record the receiver expression; the
   natural implementation anchors the regex before the receiver, which then
   cannot match `h.eq.Foo()` -- a two-hop chain through a test harness struct.
   Twenty-two call sites in four test files were invisible to the scan and
   were found only by the compiler. §Mechanism 5's "callers are found by the
   COMPILER, not by grep" is what saved it, and this is the concrete reason
   that rule exists. State it as: **the pre-computed retarget is a
   time-saver, never the caller list; only a clean build is.**

2. **`this` -> `&owner_` (K06/K07 correction (n)) must NOT be applied to a
   lambda CAPTURE, and §Mechanism should say so.** K09 correction 2 already
   widened the correction to "every bare `this` in a moved body becomes
   `&owner_`, whatever it is doing". That is wrong for `[this]`: inside a
   moved body the capture now captures the COMPONENT, which is exactly right
   -- the lambda's own references to `ctx_`/`owner_` resolve through it, and
   `[&owner_]` is not even legal capture syntax. Three of this unit's five
   `this` sites are captures (including the `std::visit` visitor that is the
   whole of `ProcessBridgeIntents`) and two are `*this` passed as an
   `EverQuest&`. State it as: **`this` in an EXPRESSION becomes `&owner_`
   (or `owner_` where it was dereferenced); `this` in a CAPTURE LIST stays.**

3. **The zero-caller rule reaches inline accessors on members that STAY, and
   that is where a family's dead API hides.** §Mechanism 5 deletes zero-caller
   METHODS; K04 correction (f) moves inline accessors on MOVING members. The
   gap is the inline accessor on a member the unit decides to leave behind --
   `GetEQClientPath()`, `GetZoneAudioManager()`, `GetConstrainedPreset()`,
   `GetConstrainedConfig()`. Four of this unit's six dead accessors are in
   that gap, invisible to both rules, and each had exactly one occurrence
   tree-wide. State it as: **grep the family's whole eq.h block, not just its
   movers.**

4. **Resolving deletions before the member decision (K09 correction 3) paid
   off in the opposite direction here.** Deleting the four `Play*` helpers did
   NOT strand `m_audio_config_use_3d_audio` (`InitializeAudio` still reads
   it), so the member moved; but `SetUse3DAudio()`'s deletion means the flag
   can now only ever hold its default. That is a write-only-at-default member,
   the same shape as K09's `m_spawn_appearance_sent`, and it is only visible
   because the deletions were settled first. Boarded, not "fixed": changing
   it is a behaviour edit outside a structural unit's licence.

5. **A family may keep forwarders when the ENTRY POINT, not the method, is the
   API.** The batch's no-forwarder rule exists because `eq.h` was the API
   surface. The five kept here are not API in that sense -- they are the
   lifecycle verbs (`wire`, `start`, `tick`, `post-tick`, `stop`) that
   `Application` and ~100 test fixtures drive, and every one of them forwards
   to a single component with no dispatch logic. The test for granting the
   exception, worth stating for K13: **a forwarder is sanctioned only if
   deleting it would make an object that has no business knowing about
   components name one.** `ProcessBridgeEvents` failed that test even though
   `Application` calls it, and was retargeted.

6. **No STOP condition was hit.** No moved body needed an edit beyond the four
   substitutions §Mechanism 4 allows; no test asserted on a member by name
   that could not be repointed; the predicted delta was -153 before any edit
   and the measured delta is -148.

### Left for the board

- **`EverQuest::SaveEntityDataToFile()` deleted** -- 41 lines that wrote the
  whole entity map to JSON "for debugging", with zero callers tree-wide, in or
  out of family. There is no `/dumpentities` command and no test; the
  DumpEntityAppearance pair (which DOES have callers) is the surviving
  entity-introspection path.
- **`PlaySoundAt`, `PlayCombatSound`, `PlaySpellSound`, `PlayUISound` deleted**
  -- zero callers. The client has ONE reachable client-triggered sound call,
  `PlaySound(uint32_t)` from `MovementComponent`'s water-entry check. The
  combat/spell/UI sound hooks were never wired: **combat, casting and the UI
  are silent by omission, not by design**, and re-adding them is a FEATURE
  (class C), not a regression to fix inside a structural unit.
- **`m_audio_config_use_3d_audio` is now settable only by editing the header.**
  `SetUse3DAudio()` had zero callers and was deleted; `InitializeAudio()`
  still reads the flag, so it moved and keeps its `true` default. Note that
  `application_config.cpp` parses every OTHER audio option into a
  `SetX()` call -- 3D audio is the one it never wired.
- **Six dead inline `eq.h` accessors deleted**: `GetEQClientPath`,
  `GetConfigPath`, `IsDaytime`, `GetZoneAudioManager`, `GetLootWindowCorpseId`,
  and the `SetConstrainedPreset`/`GetConstrainedPreset`/`GetConstrainedConfig`
  trio (eight names in total). Every one had exactly its own definition
  tree-wide.
- **`m_hotbar_model` is a `ClientContext` candidate, not a bridge member.**
  It is constructed and wired in the `EverQuest` ctor next to
  `m_inventory_manager`, `m_skill_manager` and `m_command_registry` -- three
  of which ARE core -- and `GetHotbarModel()` serves `application.cpp` and the
  input bridge. K13 should promote it, alongside K03's `AddChat*` trio, K08's
  player-state facade and K09's `m_replay_suppressed_sends`.
- **The zone-environment set is the ZONE family's, and K13 should re-home it
  there rather than here.** All seven fog/sky/zone-type fields are written by
  `ZoneComponent::ZoneProcessNewZone` and read by exactly one bridge body
  (`CreateZoneLoadSnapshot`); `m_zone_map` and `m_zone_bsp_tree*` are loaded
  and torn down entirely inside `ZoneComponent`. They sit on `EverQuest` today
  only because the snapshot reads them. Moving them into `ZoneComponent` and
  having `CreateZoneLoadSnapshot` read `owner_.Zone()` is a smaller cut than
  either alternative -- but it is a DESIGN choice about who owns the zone
  environment, so it is recorded, not taken.
- **The five forwarders are the batch's only ones and K13 owns their fate.**
  `Application` would have to name `Bridge()` in 6 more places and ~45 test
  files in ~100 more to remove them.
