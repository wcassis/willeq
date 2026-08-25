# T221 — Option A stage 2: fold the transforming projections into source-side writes on `EverQuest::m_game_state`

Slot: slot1. Stage 1 (T220) MERGED at f906f697 — the bridge now projects on the game thread; your case-body edits will not collide with it. x86 only. NO EQEMU (unit tests + builds only; the state written here
becomes reader-visible only after stage 3). Decision: docs/decisions.md
2026-08-25 "projection model is the contract" + Will's A1 approval (stage 1 =
T220 in flight; stage 2 = THIS; stage 3 = swap the six references, delete
Application::m_gameState and the bridge switch). Background:
refactor/T78_two_gamestate_analysis.md; framing: refactor/T220_*.md.
All line numbers at fable d7925e08.

## Defect / Goal
`GameStateSyncBridge::applyEvent` (src/client/bridge/game_state_sync_bridge.cpp:10-437)
is the ONLY writer of several READ-model domains, and some cases do more than
copy. After stage 3 deletes the switch, anything living only there is lost.
Census, switch vs. source (`grep -rn "m_game_state\." src/client`):

| bridge case (line) | source-side writer today | verdict |
|---|---|---|
| Pet* (14-32) | NONE — `pet()` never written outside the bridge | ADD |
| SpellGemChanged (43-60), SpellCastStarted/Complete (61-85) | NONE — `spells()` never written outside the bridge | ADD |
| EntityMoved (159-170) | NONE — `entities().updateEntityPosition` 0 hits outside state/bridge | ADD |
| EntityAppearanceChanged (171-199) | NONE — T81's own comment (176-179) admits the source is stale | ADD |
| TargetChanged (326-354), AutoAttackChanged (355-366) | NONE — `combat().setTarget/clearTarget/setAutoAttacking` 0 hits; source combat() writes are movement-only (eq_movement.cpp:266-326) | ADD (AutoAttack is 1:1 but equally unwritten; census missed it) |
| InventorySlotChanged (382-397) | NONE — and `inventory()` has ZERO action-layer readers today | ADD |
| GroupMemberUpdated (254-267) | COMPLETE: `SyncGroupMemberToGameState` eq_player_info.cpp:374-388 takes isLeader from `member.is_leader` set in the same handler (eq_group.cpp:420/436/550); leaderName at :403/:547 | nothing to add; delete at stage 3 |
| TradeItemUpdated (288-302), TradeMoneyUpdated (303-312) | COMPLETE (T103): eq_merchant.cpp:585 `setPartnerItem`, :605 `setMoney(isOwn,..)` — who-mapping already at the source | nothing to add; delete at stage 3 |
| tradeskill | NOT a bridge case; `tradeskill()` has no writer on either side (game_state.h:109) | out of scope; note in deferred.md |

Weight: `pet().hasPet` is the most-read action-layer field (17 reads in
action_dispatcher.cpp/command_processor.cpp); `combat().hasTarget/targetId` 14.

## Scope (every new write goes BEFORE any `if (!m_bridge) return;` guard)
1. **Pet.** src/client/eq_bridge.cpp: `OnPetCreated` (:540) add
   `m_game_state.pet().setPet(pet.spawn_id, pet.name, pet.level)` above the
   `if (m_bridge)`; `OnPetRemoved` (:555) `pet().clearPet()`;
   `OnPetButtonStateChanged` (:568) `pet().setButtonState(uint8_t(button), state)`.
   `ZoneProcessMobHealth` pet branch (src/client/eq_entities.cpp:1907-1919) is
   INSIDE `if (m_bridge)` (:1876): hoist the `m_pet_spawn_id == spawn_id` check
   and `pet().updatePetStats(hp_percent, manaPercent)` out; leave the pushEvent in.
   HAZARD: `OnPetRemoved()` at eq_entities.cpp:1828 is under `#ifdef
   EQT_HAS_GRAPHICS`, so headless would never clear the source pet — drop that
   ifdef exactly as T43 did for OnPetCreated (:477-483 comment); eq_zone.cpp:283
   is already ungated. Snapshot eq.cpp:666 needs nothing.
2. **Spells.** src/client/spell/spell_manager.cpp (`m_eq` is `EverQuest*`,
   spell_manager.h:266; `GetGameState()` public, eq.h:1261; null-guard m_eq):
   `publishCastStarted` (:1258) above the guard at :1259 ->
   `spells().setCasting(true, spell_id, target_id, cast_time_ms)`;
   `publishCastComplete` (:1274) above :1275 -> `clearCasting()`;
   `publishGemChanged` (:1285) above :1286 -> `setGem(gem_slot, spellId,
   static_cast<SpellGemState>(m_gem_states[gem_slot]))` and if Refresh
   `setGemCooldown(gem_slot, getGemCooldownRemaining(gem_slot),
   m_gem_cooldown_duration[gem_slot])`. Enums are value-identical
   (spell_constants.h:309-315 vs spell_state.h:26-32; bridge:54 already casts).
   Server echo eq_player_info.cpp:475-485 (ZoneProcessBeginCast, ANY caster):
   add `if (spawn_id == GetEntityID()) m_game_state.spells().setCasting(true,
   spell_id, data.targetId, cast_time)` — the projection re-applies the echo for
   self (bridge:68) and may overwrite castTimeMs with the server's; keep that.
   Snapshot eq.cpp:536-555 needs nothing; note it sends `cooldownTotalMs = 0`
   (:544), so the READ model lost cooldown totals at zone-in where the source
   will not — an improvement, record in the Review.
3. **Entity position.** src/client/eq_entities.cpp: after the two `m_entities`
   updates (self :1711-1721, others :1737-1747), OUTSIDE the `#ifdef
   EQT_HAS_GRAPHICS` wrapping `OnSpawnMovedGraphics` (:1723, :1749):
   `m_game_state.entities().updateEntityPosition(spawn_id, x, y, z, <the heading
   passed to OnSpawnMovedGraphics>, dx, dy, dz, uint8_t(animation))`.
   Ordering: D24 (game_state_bridge.h:73-81) coalesced EntityMoved per spawn so
   the projection applied only the LAST move per drain; the source applies every
   packet — final state identical (last write wins). Unknown spawn is a no-op on
   both sides. Do NOT touch `player().setPosition` (eq.cpp:3184, T55/T58).
4. **Entity appearance.** New private helper (eq_entities.cpp, decl in eq.h)
   `void ApplyAppearanceToGameState(const eqt::state::EntityAppearanceChangedData&)`
   = bridge:182-197's partial merge (raceId, gender, face, hair*, beard*,
   equipChest2<-texture, helm, equipment[9], equipmentTint[9]; NOT size) on
   `m_game_state.entities().getEntityMutable(spawnId)`. Call with the built
   `data` at: ZoneProcessWearChange (:2128-2147), UpdatePlayerAppearanceFromInventory
   (:2224 — its equipment comes from INVENTORY items :2205-2222 and is never
   written to `m_entities`, so merging `data`, not `entity`, is the only way to
   match the projection; its early return at :2152 is `!m_bridge ||
   !m_inventory_manager` — split it, keep the inventory guard), ZoneProcessIllusion
   (:2277-2296). Place before the `if (m_bridge)` at :2128/:2277.
5. **Combat.** `CombatManager` is a friend (eq.h:1324). src/client/combat.cpp:
   `PublishTargetChanged` (:329) above `if (!m_eq->m_bridge) return;` (:330) ->
   `m_eq->m_game_state.combat().setTarget(entity.spawn_id, entity.name,
   entity.hp_percent, entity.level)`; `PublishTargetCleared` (:354) ->
   `clearTarget()`; `PublishAutoAttackChanged` (:363) -> `setAutoAttacking(enabled)`.
   MobHealth target re-publish (eq_entities.cpp:1885-1905): hoist a
   `combat().setTarget(spawn_id, it->second.name, hp_percent, it->second.level)`
   out of `if (m_bridge)` (:1876). The eq_bridge.cpp publishes (:634-641 despawn
   clear, :820-839 TargetIntent) are redundant with ClearTarget():317 /
   SetTarget():253 per combat.cpp's comments (:246-252, :310-316) — no write there.
6. **Inventory occupancy.** New `void EverQuest::SyncInventorySlotToGameState(int16_t slot, bool hasItem)`
   (eq_merchant.cpp) = bridge:389-395 using include/client/inventory/inventory_constants.h
   (`isEquipmentSlot/isGeneralSlot/isBankSlot`, `GENERAL_BEGIN`=22 :40,
   `BANK_BEGIN`=2000 :56 — the SAME header the bridge includes at bridge:4, so
   nothing is missing source-side). Call at all six publish sites with the
   event's (slot, hasItem), before each `if (m_bridge ...)`: eq_merchant.cpp:71-84
   (MoveItem, both slots), :98-108 (DeleteItem), :811-827 (loot), :1062-1075
   (vendor sell); eq.cpp:622-634 (snapshot, keep for zone-in parity), :1083-1096
   (ItemPacket fallback). Not inside InventoryManager (D18a "bridge-only"; it
   has no GameState access).
7. **Bridge cases: functionally UNTOUCHED** (READ model must stay correct until
   stage 3). Add one comment line per ADD case: `// T221: source write at
   <file:fn>; delete in stage 3`, and `// T221: source already complete (...)`
   on the two group/trade cases. No case is rewritten to a "pure copy" — it
   buys nothing and risks the READ model while T220 lands on the same file.
8. **Ordering facts for the Review** (all same-thread, handler-order):
   GroupMemberUpdated's isLeader came from the PRECEDING GroupChanged's
   leaderName (eq_group.cpp:466 before :480); the source uses `is_leader` set in
   the same handler — no ordering dependency. SpellCastStarted for self fires
   TWICE (local :263/:322, then echo eq_player_info.cpp:484); step 2 keeps both.
   `PublishFullStateSnapshot` (eq.cpp:485) re-publishes state the source already
   holds; every snapshot-path write is idempotent (setPet/setGem/setMember/
   occupancy). GROUP_ACT_LEAVE (eq_group.cpp:486-520) rebuilds members
   source-side but publishes NO GroupMemberUpdated — the READ model is stale
   after a leave today, the source is not: a defect stage 3 closes for free.

## Tests
A. New `tests/test_game_state_source_writes.cpp`, registered like
   test_eq_action_handler_trade (tests/CMakeLists.txt:1329-1336: `willeq_core`,
   Irrlicht include guard). Build EverQuest as tests/test_eq_action_handler_trade.cpp:71-73
   (`EverQuest eq("127.0.0.1",0,"test","test","test","Testchar")`,
   `RecordingBridge`, `eq.setBridge(&bridge)`) AND once with no bridge (proves
   the writes precede the guards). Per domain, drive the real path and assert
   on `eq.GetGameState().<domain>()`:
   - pet: `OnPetCreated(Entity)` -> `pet().hasPet()/petSpawnId()`; `OnPetRemoved()`
     -> !hasPet; button toggle; MobHealth packet (opcode+spawn+hp, 5 bytes) for
     the pet id -> pet HP. Needs `Test_SetMySpawnIdForSeam` (eq.h:808) and a
     way to set m_pet_spawn_id — add a `Test_SetPetSpawnIdForSeam` next to it
     if none exists (one line, same Test_ convention).
   - spells: real SpellManager via `eq.GetSpellManager()` as
     test_eq_action_handler_spell_pet.cpp:47-100 (willeq.db registered in
     main()); beginCastFromGem -> `spells().isCasting()` + gem Casting;
     interrupt -> !isCasting + gem Ready; cooldown path (:1240-1248) -> Refresh
     + cooldown total.
   - move: seed self via spawn/`SyncEntityToGameState`, feed the movement
     handler -> `entities().getEntity(id)->x/y/z/heading`.
   - appearance: WearChange packet -> `getEntity(id)->equipment[slot]`.
   - combat: `GetCombatManager()->SetTarget(id)` -> `combat().hasTarget()/
     targetId()/targetName()`; `ClearTarget()`; MobHealth on the target -> HP;
     Enable/DisableAutoAttack -> `isAutoAttacking()`.
   - inventory: DeleteItem/MoveItem packets -> occupancy getters.
   Measure RED per hunk (revert one source file at a time; table like T219's).
B. tests/test_game_state_sync_bridge.cpp stays green and UNEDITED — it is the
   stage-3 net (Pet 48-100, Inventory 162-213, Spell 250-407, Entity 594-736,
   Combat 769-844, Group 870-986, Trade 1017-1150).
C. Existing EverQuest-harness suites stay green: test_eq_action_handler_spell_pet,
   test_spell_manager, test_spell_manager_timing, test_beneficial_spell_casting,
   test_eq_action_handler_{trade,tradeskill,transforms,enum_guards}.

## Size
Production ≈130 lines, no new types: eq_bridge.cpp +12 (OnPet* only),
eq_entities.cpp +45/-2 (helper, hoists, ifdef), eq_merchant.cpp +25 (helper +
5 calls), eq.cpp +4, eq_player_info.cpp +4, spell_manager.cpp +12, combat.cpp
+6, eq.h +4 (2 decls, 1 seam), game_state_sync_bridge.cpp comments only (+12).
New test ≈300 lines + CMake ≈8.

## Territory
src/client/eq_bridge.cpp (OnPet* ONLY — not ProcessBridgeEvents/PreRenderTick,
T220's), eq_entities.cpp, eq_merchant.cpp, eq_player_info.cpp (BeginCast only),
eq.cpp (the two publish sites only), spell/spell_manager.cpp, combat.cpp,
include/client/eq.h (additive), game_state_sync_bridge.cpp (comments inside
existing cases only), tests/. EXCLUDED: include/client/bridge/*.h, bridge
queue/drain plumbing, application.cpp, action_dispatcher.cpp,
command_processor.cpp, state/*.cpp, any reader swap (stage 3). Rebase vs T220
should be trivial (T220 edits the bridge file's head/drain, not case bodies).

## STOP
- A source write needs data the handler lacks (none found: inventory constants,
  spell enums, target level, pet level are all in hand) -> STOP, do not approximate.
- Writing `m_game_state` from SpellManager/CombatManager needs an accessor
  beyond public `GetGameState()` / the existing friend grant -> STOP (layering).
- Any test in B goes red -> STOP; this unit must not change the READ model.

## Verification
1. `cmake --build build -j24`; full ctest (only the documented ALSA failure).
2. New test binary: RED table (per-file revert), then GREEN; B and C green.
3. `build-gles2-x86.sh`, `build-arm-noble.sh`, `check_file_sizes.sh` (expect the
   pre-existing eq.h/eq_entities.cpp WARNs only).
4. Visual gate PASS — the renderer event stream is unchanged by construction
   (no pushEvent added/removed except OnPetRemoved now also firing headless).
5. Review lists, per domain, the exact new source line and the bridge case it
   makes redundant — that list IS stage 3's deletion list.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, branch `task/T221-fold-projections`,
base fable tip `1e602814` (verified at checkout). x86 only, no eqemu, no Pi.
Evidence: `/mnt/projects/fable/review/T221/` (README indexes every file).
No STOP condition hit: every source write had its data in hand; SpellManager
uses the public `GetGameState()` and CombatManager the existing friend grant;
`tests/test_game_state_sync_bridge.cpp` is UNEDITED and green (ctest).

Line numbers in the plan were verified against `1e602814` before editing:
all within 0-4 lines of the d7925e08 draft; no site had moved or changed
shape. During the unit `fable` advanced to `c87b553b` (T222/T225 landed;
`eq_merchant.cpp` gained a `TradeEndReason` hunk in `SetupTradeManagerCallbacks`,
far from this unit's sites) -- the RED measurement below is pinned to
`1e602814`, not to the moving `fable` ref (the first attempt against `fable`
produced a compile error, caught and redone).

### Headline

Every READ-model domain the projection was the sole writer of is now written
at the source, before the bridge guard, by the handler that owns the data.
New `tests/test_game_state_source_writes.cpp`: 24 cases x {WithBridge,
NoBridge} = 48, all green; the NoBridge leg is the proof that every write
precedes its `if (!m_bridge)` guard. Per-file RED table (revert ONE file to
`1e602814`, rebuild the test binary, run):

| file reverted | RED (of 48) | cases |
|---|---|---|
| src/client/eq_bridge.cpp | 6 | Pet_Created*, Pet_Despawn*, Pet_ButtonStateChanged* (x2 legs) |
| src/client/eq_entities.cpp (+ no-op `ApplyAppearanceToGameState` stub so it links) | 14 | Move_ClientUpdateForOtherEntity, Appearance_{WearChange,Illusion,UpdatePlayerAppearanceFromInventory}, Combat_MobHealthOnTarget, Pet_MobHealthForPet, Pet_SeamSetPetSpawnId (x2) |
| src/client/eq_merchant.cpp (+ no-op `SyncInventorySlotToGameState` stub) | 6 | Inventory_MoveItem*, Inventory_MoveItemToMinusOne*, Inventory_DeleteItem* (x2) |
| src/client/spell/spell_manager.cpp | 6 | Spells_BeginCastFromGem*, Spells_InterruptCast*, Spells_GemCooldown* (x2) |
| src/client/combat.cpp | 6 | Combat_SetTarget*, Combat_ClearTarget*, Combat_AutoAttackToggle* (x2) |
| src/client/eq_player_info.cpp | 2 | Spells_ServerBeginCastEchoForSelf* (x2) |
| src/client/eq.cpp | **0** | see below |

`eq.cpp` 0 RED, flagged honestly: its two sites are the zone-in
`PublishFullStateSnapshot` loop (runs only under a bridge after a full
zone-in) and the generic ItemPacket fallback (needs a serialized item
packet). Neither is reachable from a headless unit test without a fixture
this unit did not build; both are one-line calls to the same
`SyncInventorySlotToGameState` helper the other four inventory sites prove.
Also: `Pet_DespawnClears*` did NOT go red when `eq_entities.cpp` was reverted
-- this GLES2 tree defines `EQT_HAS_GRAPHICS`, so the old `#ifdef` still
called `OnPetRemoved()`; the ifdef drop only changes the headless configure
(not built here), exactly as the plan's HAZARD said.

### Stage-3 deletion list (per domain: source line -> bridge case now redundant)

All bridge cases are marked in place (`// T221: ...; delete in stage 3`);
this is the authoritative list for the stage-3 executor.

| domain | source write (file:line at this commit) | `GameStateSyncBridge::projectEvent` case to delete |
|---|---|---|
| Pet | `eq_bridge.cpp:543` `OnPetCreated` -> `pet().setPet` | `PetCreated` |
| Pet | `eq_bridge.cpp:560` `OnPetRemoved` -> `pet().clearPet` (and `eq_entities.cpp:1844` ifdef dropped) | `PetRemoved` |
| Pet | `eq_bridge.cpp:577` `OnPetButtonStateChanged` -> `pet().setButtonState` | `PetButtonStateChanged` |
| Pet | `eq_entities.cpp:1895-1900` `ZoneProcessMobHealth` -> `pet().updatePetStats` | `PetStatsChanged` |
| Spells | `spell_manager.cpp:1261-1263` `publishCastStarted` -> `spells().setCasting` | `SpellCastStarted` (local half) |
| Spells | `eq_player_info.cpp:479-482` `ZoneProcessBeginCast` self echo -> `spells().setCasting` | `SpellCastStarted` (echo half) |
| Spells | `spell_manager.cpp:1281-1283` `publishCastComplete` -> `spells().clearCasting` | `SpellCastComplete` |
| Spells | `spell_manager.cpp:1299-1307` `publishGemChanged` -> `spells().setGem` (+`setGemCooldown` on Refresh) | `SpellGemChanged` |
| Entity | `eq_entities.cpp:1726-1728` (self) and `1756-1758` (others) `ZoneProcessClientUpdate` -> `entities().updateEntityPosition` | `EntityMoved` |
| Entity | `eq_entities.cpp:2189` `ApplyAppearanceToGameState` called at `2176` (WearChange), `2284` (UpdatePlayerAppearanceFromInventory), `2359` (Illusion) | `EntityAppearanceChanged` |
| Combat | `combat.cpp:332` `PublishTargetChanged` -> `combat().setTarget`; `combat.cpp:359` `PublishTargetCleared` -> `clearTarget`; `eq_entities.cpp:1891-1893` MobHealth target HP -> `setTarget` | `TargetChanged` |
| Combat | `combat.cpp:369` `PublishAutoAttackChanged` -> `combat().setAutoAttacking` | `AutoAttackChanged` |
| Inventory | `eq_merchant.cpp:126` `SyncInventorySlotToGameState`, called at `eq_merchant.cpp:78` (MoveItem, both slots), `:108` (DeleteItem), `:845` (loot), `:1097` (vendor sell), `eq.cpp:628` (snapshot), `eq.cpp:1086` (ItemPacket fallback) | `InventorySlotChanged` |
| Group | already complete: `eq_player_info.cpp` `SyncGroupMemberToGameState` (T99 census) | `GroupMemberUpdated` |
| Trade | already complete: `eq_merchant.cpp` `setPartnerItem` / `setMoney(isOwn,..)` (T103) | `TradeItemUpdated`, `TradeMoneyUpdated` |

Not on the list (stage 3 must NOT delete these on this unit's evidence):
`PlayerSpawnIdSet`, `Vendor/Bank/Trainer*`, `EntityStateSnapshot`,
`EntityDespawned`, `ZoneUnloading`, `GroupChanged`, `GroupInviteReceived`,
`TradeRequestReceived/Started/AcceptStateChanged/Cancelled/Completed`,
`PlayerFlagsChanged` -- the plan's census found their sources already
complete or out of this unit's scope; stage 3 re-verifies each against
`test_game_state_sync_bridge.cpp` before removing the switch.

### Ordering / semantics facts recorded for stage 3

- SpellCastStarted for self fires TWICE (local `publishCastStarted`, then
  the server echo in `ZoneProcessBeginCast`); both write `setCasting` and
  the echo may overwrite `castTimeMs` with the server's -- identical to the
  projection's behaviour. `Spells_ServerBeginCastForOtherCaster*` pins that
  a non-self echo writes nothing.
- The zone-in snapshot (`eq.cpp:544`) publishes `cooldownTotalMs = 0`, so
  the READ model lost cooldown totals at zone-in; the source never did (its
  `setGemCooldown` runs on the real Refresh transition with the real
  duration, `Spells_GemCooldown*` asserts progress < 1.0). An improvement
  stage 3 gets for free.
- EntityMoved: D24 coalesced per spawn so the projection applied only the
  last move per drain; the source applies every packet. Last write wins on
  both sides -- final state identical; unknown spawn is a no-op on both.
- `UpdatePlayerAppearanceFromInventory()` merges the built PAYLOAD (equipment
  from inventory items), not `m_entities` -- the only way to match what the
  projection held; the test pins that `m_entities` is deliberately NOT
  written by that path. Its early return was split: the inventory guard
  stays, the bridge guard moved to the publish.
- MobHealth pet mana keeps T40's honest 0 (no pet mana source).
- GROUP_ACT_LEAVE rebuilds members source-side but publishes no
  GroupMemberUpdated -- READ model stale after a leave today, source is not
  (plan step 8); closes for free at stage 3.
- `player().setPosition` (T55/T58) untouched.

### Verification bar

| Step | Result |
|---|---|
| 1. `cmake --build slot1/build -j24` | exit 0 (`build_full.log`) |
| 2. ctest | **2830/2831**; sole failure `AudioManagerTest.PreloadCommonSounds` (ALSA, documented). 48 new `WithAndWithoutBridge/SourceWrites.*` cases registered (`ctest_full.txt`). Suites B (`GameStateSyncBridge*`, unedited) and C (spell_pet, spell_manager, spell_manager_timing, action_handler_{trade,tradeskill,transforms,enum_guards}) green |
| New test binary | 48/48 green (`test_source_writes_green.txt`); RED table above (`red_run_*.log`) |
| 3. `build-gles2-x86.sh` | exit 0 |
| 4. Visual gate | **PASS**, goldens untouched (`visual_gate.txt`; `git status` shows only this unit's files) |
| 5. `build-arm-noble.sh` | exit 0 |
| 6. `check_file_sizes.sh` | run both vs `fable` and `--files` on this unit's 11 files: no NEW FAIL. `eq.cpp` FAIL (3940 > 3000) is pre-existing (3934 at `1e602814`, +6 here); `irrlicht_renderer.cpp` FAIL is untouched by this unit. WARNs: the pre-existing oversize eq.h/combat.cpp/eq_bridge.cpp/eq_entities.cpp/eq_merchant.cpp/eq_player_info.cpp/tests CMakeLists |

### Deviations (flagged)

- Test file uses T17's `#define private public` on `eq.h`/`spell_manager.h`
  (the plan's "real path" for pet buttons and the gem-cooldown transition
  are private choke points: `OnPetButtonStateChanged`,
  `UpdatePlayerAppearanceFromInventory`, `SpellManager::startGemCooldown`).
  Pet creation/removal are driven through the real packet paths (NewSpawn
  with is_pet/pet_owner_id, DeleteSpawn), not the private hooks.
- The DB-dependent cast case ran (willeq.db present), it did not skip.
- `eq_merchant.cpp`'s vendor-sell block was restructured from
  `if (m_bridge) { build; push }` to `{ build; sync; if (m_bridge) push }`
  to keep the re-read-post-mutation in one place; the published payload is
  byte-identical.
- No production file outside the plan's territory was touched. `eq.h` got
  the two declarations and one `Test_SetPetSpawnIdForSeam` seam (additive,
  as allowed). `deferred.md` was NOT edited (plan asked to note
  `tradeskill()` having no writer on either side -- recorded here instead,
  for the orchestrator to carry to the board: `GameState::tradeskill()` has
  no writer on either side and no bridge case; out of scope).
