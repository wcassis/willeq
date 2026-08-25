# T239 — keyboard hotbar Emote / Macro / Combat slots fire (T174 leftover)

Slot: slot3. x86. OWNS eqemu briefly (noclobber) for the keypress proof
(T230's Xvfb + xdotool driver in review/T230/). Class A. Territory:
src/client/action/input_action_bridge.cpp hotbar arms, action_dispatcher /
IActionHandler / EqActionHandler for a "process command text" route,
tests. Do NOT touch renderer_frame.cpp / graphics_input_handler.cpp (T235
slot2), application.cpp script path (T238 slot1).

## Gap
`processHotbarRequests` routes Spell → castSpell, Skill → useSkill,
Item → useItemById (T230); **Emote / Macro / Combat are SKIPPED with a
DEBUG log** because they need command-text processing. The mouse path
handles them via `ProcessChatInput` (eq_bridge.cpp HotbarActivateIntent
arm — read it). T230 proved the keyboard route is now live.

## Scope
1. Find how the mouse arm executes Emote/Macro/Combat slots (the exact
   call: `ProcessChatInput(text)` or a command dispatcher). Expose ONE
   handler method (`IActionHandler::processCommandText(text)` or reuse an
   existing one) and route the three arms through it from the keyboard
   path; extract the shared body into one function both paths call (the
   T230 `UseInventoryItem` pattern) so they cannot drift.
2. Tests (real EverQuest harness + RecordingBridge, like
   test_eq_action_handler_hotbar_item.cpp): Emote slot → the emote
   command runs (observable: the OP_Emote/OP_ChannelMessage bytes or the
   recorded bridge event); Macro slot with `/say hi` → chat send; Combat
   slot → auto-attack toggle. Red/green.
3. Live: hotbar slot pre-assigned (type per HotbarSlotType) in config,
   `xdotool key 2`, observer client sees the emote / say text.

## Verification
Full ctest; visual gate not needed unless graphics files change; Review;
evidence review/T239/. Commit: `T239: keyboard hotbar fires emote, macro and combat slots`.
