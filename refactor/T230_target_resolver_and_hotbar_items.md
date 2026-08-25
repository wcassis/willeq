# T230 — one target-name resolver; keyboard hotbar fires Item slots

Slot: slot3. x86. OWNS eqemu briefly for the live leg (noclobber; T227/T229
may hold it — code first, report pending if held). Class A. Territory:
src/client/combat.cpp (SetTarget(name) only), src/client/action/
input_action_bridge.cpp + action_dispatcher / eq_action_handler for the
hotbar Item route, inventory lookup (InventoryManager::findItemSlotByItemId
already exists, eq_bridge.cpp:~1328 mouse path), tests. Do NOT touch
application.cpp, bridge/*, eq_merchant.cpp (T227), eq_gm.cpp (T229).

## 1. CombatManager::SetTarget(const std::string&) (T173, combat.cpp ~262-279)
A second, case-sensitive, UNREACHED name resolver that bypasses T173's
corpse-aware ranking. Grep callers: if zero, DELETE it; if any, route them
through the one ranked resolver T173 built (command_processor's /target
path — find it) and delete the duplicate. Test: a name that only the ranked
resolver handles (case-insensitive prefix, corpse ranking) resolves the same
way from every caller.

## 2. Keyboard hotbar Item slots are SKIPPED (T174, input_action_bridge.cpp ~424-437)
`processHotbarRequests` routes Spell→castSpell, Skill→useSkill, and skips
Item with a DEBUG log because `slot.id` is an ITEM id while `useItem` takes
an INVENTORY SLOT. The mouse path resolves via
`InventoryManager::findItemSlotByItemId` (eq_bridge.cpp ~1328). Expose that
resolution through the action handler (`IActionHandler`/`EqActionHandler`)
so the keyboard route can call `useItem(resolvedSlot)`; if the item is not
in inventory, fail with the same message the mouse path gives. Leave
Emote/Macro skipped (needs command-text processing — note it). Test: a
HotbarModel with an Item slot → key press → useItem called with the
resolved inventory slot; item absent → failure, no send. Red/green.
Live: put a clickable item on a hotbar slot via the mouse path or
/hotbar command if one exists, press the key, observe the server-side
effect (item use packet in the client log + server reaction).

## Verification
Full ctest; Review in this file; evidence review/T230/. Commit:
`T230: one target-name resolver; keyboard hotbar uses Item slots`.
