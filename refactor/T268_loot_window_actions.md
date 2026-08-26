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
