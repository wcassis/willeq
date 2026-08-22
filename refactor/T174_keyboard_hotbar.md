# T174 — Keyboard hotbar fires by slot content type, not always castSpell

**Slot: x86. No Pi, no server, no display** — the defect is client-side action
routing and the acceptance is unit tests measured against `fable`.

**Territory:** `refactor/T174_keyboard_hotbar.md`,
`include/client/action/input_action_bridge.h`,
`src/client/action/input_action_bridge.cpp`, `src/client/application.cpp` (the
one wiring line), `tests/test_input_action_bridge.cpp`, and the `tests/`
CMake list that builds that test binary.

## The bug

`InputActionBridge::processHotbarRequests()` turned **every** keyboard hotbar
press into a spell cast, regardless of what the slot actually held:

```cpp
// For now, we treat all hotbar slots as spell gems (1-indexed)
reportAction("CastSpell", m_dispatcher.castSpell(hotbarReq->slot + 1));
```

So a keyboard press on a Skill, Item or Emote slot could never do anything but
cast the gem at that hotbar position. This is the second, divergent hotbar
activation path: T156 fixed the mouse path (`eq_bridge.cpp`'s
`HotbarActivateIntent` switch, which routes by `HotbarSlotType`) and explicitly
left this one open — see T156's "Findings outside this unit's scope" #1 and
"Still owed" #2. The keyboard path had no access to `HotbarModel` at all, which
is why it could not be fixed there.

## The fix

`processHotbarRequests()` now reads the slot's real content type from the one
`HotbarModel` the rest of the client uses and routes each type to its matching
dispatcher call:

| slot type | route | argument |
|---|---|---|
| Spell | `castSpell` | `slot + 1` (position → 1-based gem, **preserved**) |
| Skill | `useSkill` | `slot.id` (skill id, no offset) |
| Item | `useItem` | `static_cast<int16_t>(slot.id)` (item id) |
| Empty / Emote / Macro / Combat | — | DEBUG-log and skip |

The `switch` on `HotbarSlotType` lists every enumerator with **no `default:`**,
so `-Wswitch` catches the next enumerator that grows (or fails to grow) a
keyboard route — the same defensive shape T156 gave the mouse switch in
`eq_bridge.cpp`.

**Indexing, confirmed against the other paths:**

- **Spell** keeps `castSpell(slot + 1)`. `HotbarRequest.slot` is the 0-based
  hotbar position (0-9); `ActionDispatcher::castSpell` validates a 1-based gem
  (1-12). This is the *only* route whose behaviour is unchanged, and it is
  deliberately position-based, **not** `slot.id`-based. It therefore diverges
  from the mouse path, which reads `slot.id` (a spell id) and searches the
  memorized gems for it. The task asked to preserve the current spell behaviour;
  a keyboard spell press still means "cast the gem at this hotbar position".
- **Skill** passes `slot.id` straight through, matching the mouse path's
  `activateSkill(i.id, …)`. No off-by-one — skill ids are not indices.
- **Item** passes `slot.id`. See the limitation below.

## Design decision — how the model is injected

`InputActionBridge` held `m_state`, `m_dispatcher`, `m_input`,
`m_commandProcessor`, `m_consoleCommandSource` and `m_unifiedDispatcher`, but not
the hotbar model. Every one of those collaborators except the two constructor
references is injected by a **setter** (`setInputHandler`, `setCommandProcessor`,
`setConsoleCommandSource`, `setUnifiedCommandDispatcher`). The model is created
*after* the bridge (the bridge is built in `Application::initialize`; the model
is owned by `EverQuest` and built in its constructor), so constructor injection
would mean re-ordering construction. Matching the established pattern instead:

- `void setHotbarModel(const HotbarModel* model)` + `const HotbarModel*
  m_hotbarModel = nullptr;`, forward-declared `class HotbarModel;` in the header
  (a pointer, so the `json/json.h` weight `hotbar_model.h` pulls in stays out of
  every TU that includes the bridge header).
- Wired in `application.cpp` right after the bridge's other setters:
  `m_inputBridge->setHotbarModel(m_eqClient->GetHotbarModel());`.
  `m_eqClient` exists by then (created earlier in `initialize`) and
  `GetHotbarModel()` is non-null (built in the `EverQuest` constructor).

**No duplicate model.** The bridge reads the *same* `HotbarModel` instance the
renderer and config persistence use — it holds a `const` pointer to it and never
mutates or copies it.

**Null-model fallback.** If no model is wired (`m_hotbarModel == nullptr`) the
bridge degrades to the exact pre-T174 behaviour — `castSpell(slot + 1)` — so it
is safe if ever constructed without a model, and the existing bridge tests
(which wire no model) keep their meaning.

## Acceptance

- **New tests measured against old code.** Four new
  `InputActionBridgeTest.Hotbar*` cases drive a real `HotbarModel` (populated by
  the real `assignSlot`, read by the real `getSlot` — nothing reconstructed by
  hand) through the bridge into the `RecordingActionHandler` behind a real
  `ActionDispatcher`. **Reverting only `input_action_bridge.cpp` to `fable` and
  rebuilding fails 3 of them** —
  `HotbarSkillSlot_ReachesUseSkillNotCastSpell` (old code calls `castSpell(1)`,
  not `useSkill(73)`), `HotbarItemSlot_ReachesUseItemNotCastSpell` (old calls
  `castSpell(4)`, not `useItem(1234)`), and
  `HotbarEmoteAndEmptySlots_AreSkippedNotCast` (old casts both instead of
  skipping) — and passes the two that assert preserved behaviour
  (`HotbarSpellSlot_StillReachesCastSpellByPosition`,
  `HotbarRequests_NoModelFallsBackToSpellGem`). Restored → all green. That
  failing-against-baseline set is the proof the tests bite.
- **`test_input_action_bridge`: 37/37 pass** with the fix (was 33 on `fable`;
  +4 new, and the old `HotbarRequests_SlotMapsToGemSlotPlusOne` was renamed to
  `HotbarRequests_NoModelFallsBackToSpellGem` and re-commented to say it is now
  the null-model fallback path).
- **`willeq` main target builds clean** (`-DEQT_GRAPHICS=OFF` on this host — no
  patched Irrlicht available; the change is graphics-independent). The one
  wiring line in `application.cpp` compiles against the real `EverQuest`.

## Known limitation / follow-ups

1. **Item route passes an item id where `useItem` wants an inventory slot.**
   `HotbarModel` stores an *item id* in `slot.id` (the mouse path resolves it
   with `InventoryManager::findItemSlotByItemId`). `ActionDispatcher::useItem`
   takes an inventory slot and the action layer here has no `InventoryManager`
   to do that resolution, so the bridge forwards `slot.id` directly. This makes
   the Item route *reach* `useItem` (the point of this unit) but the handler
   below still needs the item-id → inventory-slot hop to click the right item.
   Owed: either give the action layer an id→slot resolver, or teach `useItem`'s
   handler to accept an item id. Item ids can also exceed `int16_t`; the cast is
   part of the same debt.
2. **Emote/Macro on the keyboard path are skipped, not fired.** The mouse path
   runs `ProcessChatInput(emoteText)` through the command processor; the
   dispatcher has no equivalent route that takes a hotbar emote's command text
   (`sendChatMessage(Emote, …)` would send the literal text as an emote-channel
   message, not execute a `/wave`-style command). Skipped with a DEBUG log
   rather than invent one. `Combat` is unwired everywhere (no producer, no
   consumer) — same latent state T156 recorded.

## Deviations

- **`tests/CMakeLists.txt`**: `test_input_action_bridge` now also compiles
  `hotbar_model.cpp` and links `${JSONCPP_LIBRARIES}` (the model reads/writes
  jsoncpp). `hotbar_model.cpp` is dependency-light — `event_bus` (already in the
  test's state source set) plus an inline `GameStateBridge::pushEvent`.
- No live probe. Correct for this unit: it claims no wire behaviour and no eqemu
  was involved. A live check would press a Skill/Item hotkey and confirm the
  matching packet goes out — owed once a server is up, and gated on limitation
  #1 for the Item leg.
