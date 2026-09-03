# K06 — `EntitiesComponent` (family: entities — spawns, appearance, doors, world objects)

Batch: `refactor/K_component_extraction.md` §Mechanism (REVISED). Wave 2;
runs alongside K05/K07.

## Measured at `feb6b10b` (2026-09-03)

Four TUs, 2,641 lines, 28 bodies: `eq_entities.cpp` (830),
`eq_entities_appearance.cpp` (653), `eq_entities_updates.cpp` (767),
`eq_entities_world.cpp` (391). Four root `CMakeLists.txt` sites. Move into
`src/client/entities/`; header `include/client/entities/entities_component.h`.

**`m_entities` stays in `EverQuest` as a core member** (`ctx_.entities`): 11
families use it and the batch's core rule applies. This component OWNS the
handlers that mutate it, not the map itself — say so in the header comment.

Caller discovery by compiler (K05 rule). Lead clusters: `eq_zone_dispatch.cpp`
(17 `ZoneProcess*`), `eq_zone.cpp` / `eq_zone_entry.cpp` (`ZoneProcessClientUpdate`,
`ZoneProcessNewSpawn`, `ZoneProcessZoneSpawns`, `ClearWorldObjects`),
`eq_bridge_intents.cpp` (`SendClickDoor`, `SendClickObject`, `SendCloseContainer`,
`SendSpawnAppearance`, `UpdatePlayerAppearanceFromInventory`),
`eq_action_handler*.cpp`, `eq_commands*.cpp`, `action/command_processor_social.cpp`
(`FindEntityByName`), `eq_movement.cpp` (`FindEntityByName`, `ListEntities`),
`eq_player_social.cpp` / `eq_gm.cpp` / `eq_movement_state.cpp` / `eq.cpp`
(`SendSpawnAppearance`, `SendAnimation`), `skill/skill_manager.cpp`
(`SendAnimation`), `include/client/application.h` (`ZoneProcessMobHealth` — read
it), `include/client/world_object.h` (`ZoneProcessGroundSpawn` — a comment?).
`ApplyAppearanceToGameState`: no external caller — internal or dead (§5).

Members:

| member | refs | elsewhere | decision |
|---|---|---|---|
| `m_pending_door_clicks` | 2 | none | **move** |
| `m_loot_complete_corpse_id` | 3 | `eq_bridge_ui_audio.cpp` | shared: `owner_.` (loot cluster → K11) |
| `m_exp`, `m_exp_aa` | 3 / 2 | `eq_zone_entry.cpp`, `eq.cpp` | shared with zone: `owner_.`; K09 moves them |
| `m_player_graphics_entity_pending`, `m_req_new_zone_sent`, `m_send_exp_zonein_received`, `m_server_filter_sent`, `m_weather_received`, `m_zone_entry_sent`, `m_zone_lines`, `m_update_running` | 1–3 | zone / handshake / movement | shared: `owner_.`; zone-family members, K09 |
| `m_world_objects` | 7 | `eq_player_misc.cpp`, `eq_action_handler_spells_trade.cpp`, `eq_bridge_intents.cpp`, `action/action_dispatcher.cpp` | shared non-core: `owner_.`; **candidate to move here when K08/K11/K12 retarget their readers — record the four reader sites** |
| `m_guild_id` | 2 | `eq_zone_entry.cpp`, `guild_component.cpp` | `owner_.` |
| core: `m_bridge` 57, `m_entities` 57, `m_game_state` 55, `m_combat_manager` 14, `m_trade_manager` 12, `m_inventory_manager` 11, `m_character` 7, `m_zone_connection` 5, `m_active_tradeskill_object_id` 4, `m_audio_manager` 2, `m_buff_manager` 2 | | | `ctx_.` |

Predicted `eq.h` delta: −28 declarations −1 member −banners +4 ≈ **−25 or
better**.

## Territory

`include/client/entities/` + `src/client/entities/` (new), the four
`eq_entities*.cpp` (move), `eq.h`, `eq.cpp` (ctor), root `CMakeLists.txt`
(four lines), `tests/test_entities_component.cpp` + cmake, allowlist,
`docs/module_map.md`, CALL-SITE-ONLY edits in the caller files the compiler
names. `test_reachability_sweep.cpp` may scan `eq_entities.cpp` by name —
grep `tests/` for `eq_entities` as a STRING before you move anything; if it
does, the repoint is territory (K13-shape correction 27).

## Verification / Review

Executed in `wt/slot2` on `task/K06-entities-component`, branched from
`fable` at `7726c50a`. §Mechanism REVISED followed as written; no STOP
condition hit.

### The three `eq.h` numbers

| | lines |
|---|---|
| at dispatch (`fable` `7726c50a`) | **1,967** |
| predicted | **≤1,942** (−25 or better) |
| measured after | **1,943** (−24) |

Matches the prediction almost exactly, but only after a correction made
mid-unit: the first pass left a short "K06 (Batch K): X moved to
`EntitiesComponent`" pointer comment at every one of the six declaration
sites the 28 methods were scattered across (plus the necessary `Entities()`
accessor/friend/include/member scaffolding), and `eq.h` shrank only
**1,967 → 1,962 (−5)** — nowhere near the prediction. K02's Review already
warned the ratchet is driven by external-caller rate and one-time
scaffolding, not method count; this unit adds: **per-cluster pointer
comments are themselves a scaffolding cost, and at six clusters (vs. K02's
two) they ate 20 of the 25 predicted lines.** Trimmed every pointer comment
down to the two structurally necessary ones (the `Entities()` accessor and
the friend line) and deleted the rest outright, matching K03's leaner
style — `eq.h` then landed at 1,943. `scripts/refactor/file_size_allowlist.txt`
lowered **1,967 → 1,943**; `FileSizeCeiling` passes.

### Measured vs the plan's leads

**Methods.** 28 `EverQuest::` bodies confirmed across the four TUs (5 in
`eq_entities.cpp`, 9 in `_appearance.cpp` including `FindEntityByName`, 5 in
`_updates.cpp`, 9 in `_world.cpp` including `GetWorldObject`) — exactly the
plan's count. All 28 declarations deleted from `eq.h` per §Mechanism 5; no
forwarders.

| lead | measured |
|---|---|
| `ApplyAppearanceToGameState`: "no external caller — internal or dead (§5)" | **WRONG in the same shape as K03's `ZoneSendEmote`**: it has THREE in-family callers (`eq_entities_appearance.cpp` lines 452/562/636, all inside the moving TU). Not dead — moved as a private-surface method on the component, zero external callers, zero retargets needed. Confirms §Mechanism 5 already reads "including in-family" after K03's correction; the plan's own lead table still used the stale "external" framing and would have deleted `/appearance` mid-session updates had it been followed literally. |
| `eq_zone_dispatch.cpp` (17 `ZoneProcess*`) | confirmed exactly 17 bare calls, all inside `DispatchZonePacket`, all retargeted to `m_entities_component->ZoneProcess*(p)` |
| `eq_bridge_intents.cpp` (`SendClickDoor`, `SendClickObject`, `SendCloseContainer`, `SendSpawnAppearance`, `UpdatePlayerAppearanceFromInventory`) | confirmed, one bare call site each, inside `EverQuest::ProcessBridgeIntents`; all retargeted to `m_entities_component->` |
| `eq_action_handler*.cpp` | `eq_action_handler.cpp` (`SendClickDoor`, 1), `eq_action_handler_extended.cpp` (`ListEntities`, `SendAnimation`, 1 each), `eq_action_handler_spells_trade.cpp` (`SendClickObject`, `GetWorldObject`, 1 each) — all via `m_eq.Foo(...)`, retargeted to `m_eq.Entities().Foo(...)` |
| `action/command_processor_social.cpp` (`FindEntityByName`) | **not a real caller** — grepped, zero hits; the plan's lead was stale |
| `eq_movement.cpp` (`FindEntityByName`, `ListEntities`) | 6 bare `FindEntityByName` call sites confirmed (not `ListEntities` — that one isn't called from this file); all retargeted to `m_entities_component->FindEntityByName(...)` |
| `eq_player_social.cpp` / `eq_movement_state.cpp` / `eq.cpp` (`SendSpawnAppearance`) | confirmed: 3 bare sites in `eq_player_social.cpp`, 8 in `eq_movement_state.cpp`, 1 in `eq.cpp` — all retargeted |
| `eq_gm.cpp` (`SendSpawnAppearance`) | **stale lead** — `eq_gm.cpp` was moved to `gm/gm_component.cpp` by K02 and does not call `SendSpawnAppearance` at all; zero hits |
| `skill/skill_manager.cpp` (`SendAnimation`) | confirmed, 1 bare-`m_eq->` site, retargeted to `m_eq->Entities().SendAnimation(...)` |
| `include/client/application.h` (`ZoneProcessMobHealth` — read it) | confirmed a comment (prose in a doc block), not a reference — no edit |
| `include/client/world_object.h` (`ZoneProcessGroundSpawn` — a comment?) | confirmed a comment |
| `eq_zone.cpp` (`ClearWorldObjects`) | confirmed, 1 bare site, retargeted |
| `eq_commands_movement.cpp` (`FindEntityByName`, via the "`eq_commands*.cpp`" lead cluster) | confirmed, 1 site, retargeted |

**Test call sites**, measured with `grep -rw` per moved method name (not the
plan's table, which named none): `GetWorldObject` — 7 sites across
`test_eq_action_handler_tradeskill.cpp` (3), `test_tradeskill_combine.cpp` (2),
`test_interact_world_object.cpp` (2), all retargeted to `eq.Entities().` /
`h.eq.Entities().`, assertions untouched. `UpdatePlayerAppearanceFromInventory`
— 1 site (`test_game_state_source_writes.cpp`), retargeted. Every other moved
method has zero test call sites (the `ZoneProcessDeleteSpawn`/`NewSpawn`/
`ClientUpdate`/`ClickObjectAction` mentions in `test_zone_connection_flow.cpp`
and `tests/cmake/packets_skills.cmake` are trailing comments naming the
opcode, confirmed by reading them — no edit).

**Members.** The plan's table is confirmed exactly for every member it
named, with **two members it did not name at all**, found only by grepping
the moved bodies: `m_loot_items` and `m_pending_loot_slots`, both referenced
once in `entities_component_appearance.cpp`'s `ZoneProcessDeleteSpawn` (the
close-loot-window-on-corpse-despawn branch). **Name-collision trap, same
shape as K03's `SendChatMessage`**: `CombatManager` also declares its own
`m_loot_items` (a `std::vector<uint32_t>`, `include/client/combat.h:339`) —
a bare `grep -rw m_loot_items` over the tree returns both classes' members
interleaved and would have looked like a much bigger dependency than it is.
Confirmed by reading the body: both are `EverQuest`'s own members (a
`std::map<int16_t, GameLootItem>` and a `std::vector<int16_t>`,
`eq.h` 1602/1608), part of the loot cluster already flagged by
`m_loot_complete_corpse_id`'s row — **decision: shared, `owner_.m_*`, K11
owns the eventual move**, same as the plan's existing loot-cluster call.

**Undocumented dependencies needing §Mechanism 4's third substitution form
(bare `Foo()` → `owner_.Foo()`), found only by attempting the build** — far
more than any prior K unit, because this family's four TUs still call a wide
spread of `EverQuest` accessor/mutator primitives that never got their own
family: `SetLoadingPhase`, `SetMySpawnId`, `PosX`/`PosY`/`PosZ`, `HeadingDeg`,
`SetPosLocal`, `SyncEntityToGameState`, `OnPetCreated`,
`OnPetButtonStateChanged`, `UpdateDayNightState`, `ZoneSendSetServerFilter`,
`ZoneSendClientReady`, `AddChatSystemMessage`, `OnPetRemoved`,
`OnSpawnRemovedGraphics`, `RemoveEntityFromGameState`, `UpdateInventoryStats`,
`IsFullyZonedIn`, `CalculateDistance`, `ZoneSendReqNewZone`,
`OnSpawnAddedGraphics`, `SetHeadingLocal`, `StartUpdateLoop`,
`OnGameStateComplete`, `OnSpawnMovedGraphics`, `MySpawnId` — 27 distinct
methods (`MySpawnId` alone has 30+ call sites across the four files).
`IsTrackedTarget` looked like a 26th but is a **free function**
(`common/logging.h`), not an `EverQuest` member — left bare, correctly. The
third substitution (`s_debug_level` → `EverQuest::s_debug_level`) was needed
at 21 sites, the most of any K unit so far.

### Naming: `m_entities` was already taken

§Mechanism 2 says the owning pointer is named `m_<family>` (`m_guild`,
`m_gm`, `m_chat`). For this family that collides with the pre-existing core
member `m_entities` (the map itself, staying on `EverQuest`). **Named the
component pointer `m_entities_component`** and its accessor stays `Entities()`
(no collision there — `GetEntities()` is the map's own accessor and is
untouched). Flagged as a correction: any future family whose name is also an
existing `EverQuest` member name needs the same disambiguation; state it in
§Mechanism 2 rather than leaving it to be rediscovered.

### Multiset residual (§Mechanism 8)

Normalisation: `s/ctx_\.<core-member>/m_<core-member>/` for each of the 11
core members this family actually reads (measured: `game_state`, `bridge`,
`entities`, `character`, `combat_manager`, `trade_manager`,
`inventory_manager`, `buff_manager`, `zone_connection`,
`active_tradeskill_object_id`, `audio_manager` — 4 of the full 15-member core
go unused here: `spell_manager`, `skill_manager`, `zone_connection_manager`,
`command_registry`), then `s/owner_\.//`, `s/eqt::entities::EntitiesComponent::/EverQuest::/`,
then `s/EverQuest::s_debug_level/s_debug_level/` (the third substitution form
maps back to the SAME name, unlike a member move, so it needs its own
un-mapping step or every qualified site shows up as a false residual — a
new wrinkle K01/K02/K03 didn't hit because none of them needed
`s_debug_level` at this volume). Filtered blank/comment/`#include` lines,
sorted both sides.

Old side: the four `eq_entities*.cpp` at `7726c50a` (2,641 lines, 1,702 code
lines after filtering). New side: the four `entities_component*.cpp` files
concatenated (2,705 lines, 1,702 code lines after filtering).

**Residual on code lines: ZERO** (`diff` exit 0) — same strength of proof as
K03's. Every line in the unfiltered diff is a new file-header comment, a new
`#include`, or (in the four files that got a K06 note appended to their
existing J08 header) an existing comment line unchanged.

### Retarget list

| file | sites | change |
|---|---|---|
| `include/client/eq.h` | 28 declarations | **deleted**: all 17 `ZoneProcess*`, `SendSpawnAppearance`, `SendAnimation`, `SendClickDoor`, `SendClickObject`, `SendCloseContainer`, `GetWorldObject`, `ClearWorldObjects`, `ListEntities`, `FindEntityByName`, `ApplyAppearanceToGameState`, `UpdatePlayerAppearanceFromInventory`. `Entities()` and the `m_pending_door_clicks` member are all that changed besides |
| `src/client/eq_zone_dispatch.cpp` | 17 | `ZoneProcess*(p)` → `m_entities_component->ZoneProcess*(p)` |
| `src/client/eq_bridge_intents.cpp` | 5 | `SendClickDoor`/`SendClickObject`/`SendSpawnAppearance`/`SendCloseContainer`/`UpdatePlayerAppearanceFromInventory` → `m_entities_component->` |
| `src/client/eq_movement_state.cpp` | 8 | `SendSpawnAppearance(...)` → `m_entities_component->SendSpawnAppearance(...)` |
| `src/client/eq_movement.cpp` | 6 | `FindEntityByName(...)` → `m_entities_component->FindEntityByName(...)` |
| `src/client/eq_player_social.cpp` | 4 | `SendSpawnAppearance` ×3 + `SendAnimation` ×1 → `m_entities_component->` |
| `src/client/eq.cpp` | 1 | `SendSpawnAppearance` (`SetSneak`) → `m_entities_component->` |
| `src/client/eq_zone.cpp` | 1 | `ClearWorldObjects()` → `m_entities_component->ClearWorldObjects()` |
| `src/client/eq_commands_movement.cpp` | 1 | `FindEntityByName` → `m_entities_component->FindEntityByName` |
| `src/client/eq_action_handler.cpp` | 1 | `m_eq.SendClickDoor` → `m_eq.Entities().SendClickDoor` |
| `src/client/eq_action_handler_extended.cpp` | 2 | `m_eq.ListEntities` / `m_eq.SendAnimation` → `m_eq.Entities().` |
| `src/client/eq_action_handler_spells_trade.cpp` | 2 | `m_eq.SendClickObject` / `m_eq.GetWorldObject` → `m_eq.Entities().` |
| `src/client/skill/skill_manager.cpp` | 1 | `m_eq->SendAnimation` → `m_eq->Entities().SendAnimation` |
| `tests/test_eq_action_handler_tradeskill.cpp` | 3 | `eq.GetWorldObject` → `eq.Entities().GetWorldObject`. **Assertions untouched** |
| `tests/test_tradeskill_combine.cpp` | 2 | `h.eq.GetWorldObject` → `h.eq.Entities().GetWorldObject`. **Assertions untouched** |
| `tests/test_interact_world_object.cpp` | 2 | `eq.GetWorldObject` → `eq.Entities().GetWorldObject`. **Assertions untouched** |
| `tests/test_game_state_source_writes.cpp` | 1 | `eq.UpdatePlayerAppearanceFromInventory()` → `eq.Entities().UpdatePlayerAppearanceFromInventory()`. **Assertions untouched** |

Total: 28 declarations deleted, 55 real call sites retargeted across 16
files (11 production, 4 test, 1 skill-manager) — far more than any prior K
unit, matching this family's much wider caller spread.

### Text readers of the moved TUs (§Mechanism 7 / K03 correction 1)

`grep -rn eq_entities tests/ scripts/` before moving anything: every hit
(`test_zone_entities.cpp`, `test_spell_manager.cpp`,
`test_zone_connection_flow.cpp`, `test_position_store_coherence.cpp`,
`test_game_state_source_writes.cpp`, `test_position_frame_roundtrip.cpp`,
`test_entity_size_change.cpp`, `zone_stream_test_utils.h`,
`scripts/analyze_load_log.sh`, `tests/cmake/action_handlers.cmake`) is a
**prose comment naming the old filename**, not a text-reading guard. No file
reads `eq_entities*.cpp` as data the way `test_chat_channel_names.cpp` read
`eq_chat.cpp`. `test_reachability_sweep.cpp` and
`test_duplicate_state_stores.cpp` both scan `eq.h`, not the TU, and both
**pass unchanged** — confirmed by `grep -rw m_entities\|m_pending_door_clicks`
over both, zero hits.

### Doc citations (found by `DocCitations`, not the plan)

`docs/CLAUDE.md:161` cited `src/client/eq_entities.cpp` (the load-bearing
heading-asymmetry note) — retargeted to
`src/client/entities/entities_component.cpp`, where the `/4.0f` conversion
now lives. A second, non-citation prose mention at `docs/CLAUDE.md:40` (inside
a code-comment example, not backtick-cited, so not checked by the tool) was
updated too for accuracy. `docs/module_map.md`'s four `eq_entities*.cpp` rows
replaced with the four `entities_component*.cpp` rows (measured line counts:
847/668/784/406) plus the family-grouping table's `entities` row marked
**K06 DONE**.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** on the final state; five intermediate compiler-driven retarget passes (each named a fresh set of call sites, per §Mechanism 5's "callers found by the compiler" rule) — no timeout, no exit 143 |
| `ctest --test-dir build -j6 --output-on-failure` | **3,531 / 3,532 passed**, 49 skipped. **1 failure: `AudioManagerTest.PreloadCommonSounds` only** (the known ALSA quirk). `FileSizeCeiling`, `DocCitations` (217 ok / 0 fail / 1 skip), `DuplicateStateStores.EverQuestKeepsNoCopyOfAStateModuleMember`, all 5 `ReachabilitySweep.*` cases, and `visual_gate` (20.67s, inside this same run) all **PASS** |
| `tests/test_entities_component.cpp` (new) | 7/7 pass: `ComponentExistsAndIsStable`, `ContextEntitiesIsTheSameMapAsGetEntities`, `FindEntityByNameReturnsNullptrWhenNoSpawnMatches`, `FindEntityByNameFindsASpawnInjectedViaZoneProcessNewSpawn`, `ListEntitiesDoesNotThrowOnAnEmptyOrPopulatedTable`, `GetWorldObjectReturnsNullptrForAnUntrackedDropId`, `ClearWorldObjectsIsSafeToCallOnAnEmptyTable` |
| `scripts/refactor/check_file_sizes.sh --all` | **806 ok, 0 warn, 0 FAIL** with the ratchet lowered to 1,943 |
| `bash scripts/build-gles2-x86.sh` / standalone visual gate / `bash scripts/build-arm-noble.sh` | **not run standalone, correctly so**: `git diff --name-only $(git merge-base fable HEAD)..HEAD -- src/client/graphics include/client/graphics` is **empty** — no graphics file changed anywhere in this unit's 21 touched/new files. `visual_gate` still ran and passed inside the full `ctest` run above, which is strictly more evidence than the standalone script would add |

### Corrections to §Mechanism / the plan

1. **Per-cluster pointer comments are a scaffolding cost that scales with the
   number of declaration SITES, not the number of methods.** A family whose
   28 methods are scattered across six separate spots in `eq.h` (as opposed
   to one contiguous block) pays for six pointer comments if it leaves one at
   each spot — 20 of this unit's first-pass 25-line miss. **Leave at most the
   two structurally required comments (the accessor, the friend line);
   deleting a declaration cluster needs no comment of its own** — the
   family's own file header and the plan/Review carry that information. Add
   this to §Mechanism 6 as a specific instruction, not just "predict the
   delta before dispatch."
2. **A family name that collides with an existing `EverQuest` member name
   needs its pointer disambiguated.** `entities` collides with the
   pre-existing `m_entities` (the map). Named the pointer
   `m_entities_component`; the accessor `Entities()` had no such collision.
   §Mechanism 2 should say to check for this before naming the pointer.
3. **§Mechanism 5's "in-family callers count" (K03 correction (a)) is
   already right in the mechanism text — the DEFECT is in how a per-unit
   plan's lead table gets written.** `ApplyAppearanceToGameState` repeated
   K03's `ZoneSendEmote` mistake: the plan's own prose called it "no external
   caller — internal or dead" without re-checking for in-family callers
   before writing that conclusion. No process change needed beyond what K03
   already established; recorded here as a second confirmed instance so a
   third makes it three-for-three and worth automating (e.g. the plan
   template requiring an explicit "in-family callers:" column, not prose).
4. **A `grep -rw <member>` lead is unreliable when TWO classes declare a
   member of the same name.** `m_loot_items` exists on both `EverQuest`
   (`std::map<int16_t, GameLootItem>`) and `CombatManager`
   (`std::vector<uint32_t>`). Same shape as K03's `SendChatMessage`/
   `DispatchSlashCommand` collisions (K03 correction 3) but for DATA members
   rather than methods — extend that correction's "record the receiver
   expression, not the name" rule to member greps as well: confirm which
   class's member a hit belongs to by reading the body, not by name alone.
5. **A family with many scattered EverQuest-primitive dependencies (§Mechanism
   4's second substitution form) needs a wider net than "grep for bare
   `Foo()` calls" (K02 correction 3) can catch efficiently by hand.** This
   unit's 27 distinct stays-behind methods were found by cross-referencing
   every `identifier(` call in the four moving files against every method
   `eq.h` declares, then checking each hit's receiver by reading it in
   context — a mechanical, repeatable step, not a compiler error (the
   compiler DOES catch these, but one-at-a-time, at the cost of a rebuild per
   name). Worth productising as a script for K07+ (movement, the next
   family, is likely to have a similar spread).
6. **`s_debug_level`'s un-mapping needs its own normalisation rule.** Unlike
   a moved MEMBER (`m_character` → `ctx_.character`, mapped back by
   substituting the whole token), the third substitution form
   (`s_debug_level` → `EverQuest::s_debug_level`) changes only the
   QUALIFICATION, so the multiset check's core-member mapping (K01 correction
   3) doesn't touch it and it must be un-mapped separately
   (`s/EverQuest::s_debug_level/s_debug_level/`) or it shows up as a false
   residual on every one of its 21 sites. Add to §Mechanism 8.
7. **No STOP condition was hit.** Every moved body needed only the
   substitutions §Mechanism 4 already allows (all three forms; this family is
   the heaviest user yet of the third form). No test asserted on a member by
   name that could not be repointed. `m_pending_door_clicks` (the only member
   that moved) has an inline-accessor lead of zero (confirmed — no `eq.h`
   accessor touches it), so K04's inline-accessor correction did not apply
   here.

### Left for the board

Nothing new. Re-checked the `m_world_objects` "four reader sites" note in the
plan's member table: **only one of the four is a real reference** —
`eq_bridge_intents.cpp` (3 real sites, `TradeskillCloseIntent`'s handler,
reading `m_world_objects` bare as `EverQuest`'s own member — untouched by
this unit, correctly, since that member stayed on `EverQuest`).
`eq_player_misc.cpp:315`, `eq_action_handler_spells_trade.cpp:324` and
`action/action_dispatcher.cpp:537` are all **comments** naming the member in
prose, not code references. The plan's phrase "four reader sites" describes
four FILES that mention it, not four call sites — worth a word of caution for
whichever K unit eventually moves `m_world_objects`. This unit did not touch
it either way; K08/K11/K12 still own the eventual move.
