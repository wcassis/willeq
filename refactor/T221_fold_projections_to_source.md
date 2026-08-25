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
