# T268 — loot window: take one item, loot all, destroy, close (T264 F1)

Slot: slot3. x86. OWNS eqemu (noclobber) for a live corpse loot with an
observer. Class A. Territory: the loot intents T264 KEPT (see its Review:
the loot arms in eq_bridge.cpp, `IActionHandler::lootItem/lootAll`,
`ActionDispatcher`), CommandProcessor `/loot` family (T134 migrated it),
the static loot popup if one exists (static_panels), the two drifted
loot-all impls (`EverQuest::LootAllFromCorpse` vs `CombatManager::LootAll`
-- keep ONE), tests. Do NOT touch race loader (T267 slot1), EQE1 resolver
(T266 slot2).

## Gap
`/loot` opens the corpse and, with `/autoloot` on, takes everything; with
it off there is no way to take ONE item, loot all, destroy, or close. The
handler methods and intents exist with no producer (T264 table).

## Scope
1. Commands (scriptable first, T156 rule): `/loot <slot|name>` takes one
   item, `/lootall`, `/lootdestroy <slot>` (if eqemu supports destroy on
   corpse -- check `Handle_OP_LootItem`), `/lootclose`. Route through the
   kept handler hops; delete the drifted duplicate so both paths share one
   body (T230/T239 pattern).
2. Mouse/hotkey: if a loot popup exists, its click → the same
   `LootItemIntent`; if not, note it (UI-gesture-only reachability stays
   on the board, T48).
3. Tests: real EverQuest harness — loot request → OP_LootItem bytes per
   item; loot all → N packets + OP_EndLootRequest; close → EndLoot only.
   Red/green.
4. Live: kill a beetle (T247's auto-defence recipe), `/loot`, take one
   item by name, `/lootall`, `/lootclose`; DB rows move to Testchar's
   inventory; observer sees the corpse empty/disappear. Restore rows.

## Verification
Full ctest; Review; evidence review/T268/. Commit:
`T268: loot window actions reachable -- take, loot all, close`.

## Review

Executed in `/mnt/projects/fable/wt/slot3`, branch `task/T268-loot-actions`,
off fable `23251315`. Full evidence: `/mnt/projects/fable/review/T268/`
(README + red/green test logs + three live client sessions with an observer
+ the DB before/after/restored triple).

### The duplicate: `CombatManager::LootAll` KEPT, `EverQuest::LootAllFromCorpse` DELETED

The two drifted loot-all implementations named in the plan were not two
copies of one algorithm -- they were a POLICY and a SENDER tangled together
twice, each owning half of what it needed:

| | `EverQuest::LootAllFromCorpse` (deleted) | `CombatManager::LootAll` (kept) |
|---|---|---|
| sequencing | `m_loot_all_in_progress` + `m_loot_all_remaining_slots`, advanced from the server's item-arrival callback in `ZoneProcessLootedItemToInventory` | the T26 `LootSubState` machine, ticked once per game frame from `Application::updateGameState` |
| pacing | none (as fast as the server answers) | `kInterItemDelayMs`, `kClosingDelayMs`, named constants |
| corpse-gone / deadline exits | none -- a corpse that decayed mid-loop left `m_loot_all_in_progress` true forever | every state has both (`CorpseStillPresent`, `TickAwaitingItems`) |
| item names | had them (`m_loot_items` is `slot -> {itemId, name}`) | had only slot numbers |
| closes the window | never (the loop just stopped) | yes, via `Closing` |

Kept the one with the deadlines, the pacing and the exits (T26 paid for
those); gave it the names it lacked by making `EverQuest::m_loot_items` the
single window model and feeding `CombatManager`'s slot queue from the ONE
item-packet handler. The result is one policy owner and one sender per
packet:

    /loot <corpse>  -> CombatManager::LootCorpse -> EverQuest::RequestLootCorpse   -> OP_LootRequest
    /loot <item>    -> EqActionHandler::lootItem -> EverQuest::LootItemFromCorpse  -> OP_LootItem
    /lootall        -> CombatManager::LootAll (LootingItems drain) -> the same LootItemFromCorpse
    /lootclose      -> EverQuest::CloseLootWindow -> OP_EndLootRequest

`CombatManager` no longer builds a single loot packet (its private
`LootingItem_Struct` copy is gone), and `EverQuest` no longer sequences a
loot-all. `CombatManager::CloseLootWindow()` routes through
`EverQuest::CloseLootWindow()`, which calls back `OnLootWindowClosed()` --
so however the window closes (command, intent, corpse despawn, abort) the
combat state leaves `COMBAT_STATE_LOOTING` exactly once.

### The state that was missing: `LootSubState::ManualOpen`

With auto-loot OFF, T26's `AwaitingItems` hard deadline (5 s) CLOSED the
window. That is what actually made "take ONE item" impossible -- not a
missing command. `ManualOpen` is the new terminal-until-acted-on state: no
deadline, exits only on `LootAll()`, a close, or the corpse going away.
`AwaitingItems` enters it on quiescence or the hard deadline when auto-loot
is off AND there are items; with NO items the hard deadline still closes the
session, unchanged (`AwaitingItemsHardDeadlineClosesWhenNothingArrived`).

### Second parser deleted

`EverQuest::ZoneProcessLootItem` (eq_combat_actions.cpp) was a SECOND parser
of the loot ItemPacket, used only by the "headless mode looting" dispatch
branch, and it read the item name from `fields[11]` where the live parser
(`ZoneProcessLootItemToUI`) reads `fields[12]` -- the same off-by-one
`ZoneProcessLootItemToUI` carries a fixed comment about. It also fed
`CombatManager::AddLootItem` while the UI path did not, which is why the two
loot-all impls existed in the first place. Deleted; `ZoneProcessLootItemToUI`
now feeds both views. `CombatManager::ProcessLootWindow()` (no caller) went
with it. The dispatch branch it served is unreachable now that
`RequestLootCorpse` is on every open path -- including
`CombatManager::BeginLootRequest`, which previously sent the request itself
and never set `m_player_looting_corpse_id`.

### Commands (scriptable first, T156)

`/loot` reads its argument IN CONTEXT of the window, which is what a player
means by "loot that":

| state | `/loot 24` | `/loot vampire fang` |
|---|---|---|
| no window | corpse id 24 (T34's form, unchanged) | nearest CORPSE whose name contains it |
| window open | corpse SLOT 24 | the window item whose name matches |

Plus `/lootall` and `/lootclose` (alias `/closeloot`). The corpse-name
resolver is deliberately NOT `EntityManager::findEntityByName` (T244): that
one ranks a LIVING entity above its own corpse, the exact opposite of what
looting wants. It filters on T173's ONE shared predicate
(`IsCorpseByFields`) and takes the nearest match. Refusals name the
offending token (T178).

Command-driven takes send `auto_loot=1` (eqemu puts the item straight into
inventory); the UI-gesture `LootItemIntent` keeps `auto_loot=0` (to the
cursor, as the real client does). This is not cosmetic: with an item on the
cursor eqemu's `Character:CheckCursorEmptyWhenLooting` refuses the NEXT
take, and a scripted `/loot` has no way to clear a cursor -- run 2's live log
is that failure, verbatim, seven times.

### DEVIATION: `DestroyAllLootIntent` deleted rather than wired

The plan asked to check whether eqemu supports destroy-on-corpse
(`Handle_OP_LootItem`). It does not: there is no destroy path, and the
client's own `DestroyAllCorpseLoot` body was a comment saying so followed by
a call to `CloseLootWindow` -- a second name for the close. `/lootdestroy` is
therefore NOT implemented and the intent is deleted (variant 34 -> 33) rather
than left as a producer-less alias. Items left on a corpse decay with it,
which is the game's own "destroy".

### DEVIATION (found live, fixed here): `markAsCorpse` had no caller

`/loot narcissist` failed live with "No corpse found" while `/list corpse`
showed the corpse one line earlier. `EntityManager::markAsCorpse()` has zero
production callers, so the GameState entity -- what every by-name command
resolves against -- kept the LIVING name and `isCorpse=false` for the whole
session. `/target <corpse name>` had the identical hole (T185 fixed the
local `EQ::Entity` copy only). Fixed by calling the existing
`SyncEntityToGameState()` at both corpse-transition sites (the `OP_MobRename`
handler and `ZoneProcessDeath`). Outside the plan's literal file list but the
same defect the unit exists to close -- flagged, not absorbed silently.

### Reachability

`tests/test_reachability_sweep.cpp` updated as intended: `lootItem`/`lootAll`
came off `kBlockedHandlerMethods` (now EMPTY) and `DestroyAllLootIntent` came
off `kBlockedIntents`. The other three loot intents STAY on the blocked list:
they are the UI-GESTURE leg of the same feature and there is no loot popup to
click (T48's UI-gesture-only class, as this plan's item 2 anticipated). Their
arms are live and now pinned by `test_loot_window_actions`
(`LootItemIntent_SendsLootItemToCursor`, `LootAllIntent_DrivesTheSameMachine`,
`CloseIntent_SendsEndLootRequestOnly`), so a popup only has to push them.

### Test seam

`EverQuest::Test_RecordQueuedPacket` (armed by
`Test_SetCaptureQueuedPacketsForSeam`) captures opcode + body in
`QueuePacket()` BEFORE its connection gate -- the same shape as the T88/T217/
T229 seams. Without it a loot action is unobservable headlessly
(`QueuePacket` silently no-ops with no zone connection), and the tests would
have to re-derive the packet sequence instead of reading the production
encoder's output. Off by default; production behaviour is unchanged.

### Verification

| gate | result |
|---|---|
| `cmake --build build -j24` | exit 0 |
| full `ctest` | **3004/3005** (only `AudioManagerTest.PreloadCommonSounds`, the ALSA host quirk) |
| `build-gles2-x86.sh` | exit 0 |
| visual gate | PASS, goldens untouched |
| `build-arm-noble.sh` | exit 0 |
| `check_file_sizes.sh fable` | 1 FAIL = `src/client/eq.cpp` 3930 lines, PRE-EXISTING and exactly its fable baseline; the new commands went into `src/client/action/command_processor_loot.cpp` so `command_processor.cpp` stays under the 3000 ceiling |

Live: `review/T268/README.md` has the full timeline table, the observer's
independent despawn line, and the DB triple (`diff db_before.txt
db_restored.txt` is EMPTY). eqemu lock taken atomically, released, deleted;
GM leftovers in zone 36: 0 before, 0 after.
