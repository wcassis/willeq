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

## Review (executor, 2026-08-25/26, slot3, branch task/T239-hotbar-emote off fable 584ff5b5)

### 1. How the mouse arm executed the three slots (finding)

`eq_bridge.cpp` HotbarActivateIntent switch at 584ff5b5: **Emote** ->
`if (!i.emoteText.empty()) ProcessChatInput(i.emoteText);` inline; **Macro**
and **Combat** were `break;` no-ops ("Not wired to anything yet") -- so the
"mouse handles them" premise held for Emote only. The keyboard arm
(`InputActionBridge::processHotbarRequests`) logged and skipped all three.
A Macro slot has no producer in the UI yet; its only text field is
`HotbarSlot::emoteText`, and that is what config JSON / assignSlot fill, so
Macro == "a command line in emoteText", same as Emote, at every layer.

### 2. The shared route (design)

One body, two entry points -- the T230 `UseInventoryItem` pattern:

- `EverQuest::ActivateHotbarCommandSlot(HotbarSlotType, const std::string& text)`
  (eq.h, defined in eq_bridge.cpp). Emote/Macro: empty text ->
  `AddChatSystemMessage("Hotbar slot has no command")`, false; else
  `ProcessChatInput(text)` -- the chat window's unified dispatch entry
  (registry 1, then Application's fallback registries 2/3, where /wave and
  /say actually live), true. Combat: no combat manager -> false; off with
  no target -> "You must have a target to attack", false; else
  `CombatManager::ToggleAutoAttack()`, true. Any other type -> false, no
  side effect (-Wswitch-exhaustive, no default).
- Mouse: the HotbarActivateIntent Emote/Macro/Combat arms collapse to one
  `ActivateHotbarCommandSlot(i.type, i.emoteText)` call. Behavioural change
  to the EXISTING mouse path: Macro and Combat clicks now do something;
  an empty Emote click now says "Hotbar slot has no command" instead of
  nothing.
- Keyboard: `IActionHandler::activateHotbarCommandSlot(type, text)` (ONE new
  pure virtual, the plan's "processCommandText" -- typed rather than
  text-only because Combat carries no text) -> `EqActionHandler` ->
  `EverQuest::ActivateHotbarCommandSlot`. `ActionDispatcher::
  activateHotbarCommandSlot` gates: zone connection, type must be
  Emote/Macro/Combat (`Failure("Not a command hotbar slot")` -- a Spell/
  Skill/Item slot can never be run as command text), Emote/Macro empty text
  (`Failure("Hotbar slot has no command")`) -- all before the handler.
  `InputActionBridge` routes the three arms to
  `reportAction("HotbarCommand", m_dispatcher.activateHotbarCommandSlot(
  slot.type, slot.emoteText))`; Empty stays a logged skip.
- `CombatManager::ToggleAutoAttack()` (new): the on->off / off->on-only-
  with-target rule moved out of `EqActionHandler::toggleAutoAttack` (which
  now delegates) so /aa and the Combat slot cannot drift. Scope growth,
  flagged.
- `HotbarModel`: Emote AND Macro slots are named from their text (first 10
  chars) regardless of the name resolver -- the resolver (eq.cpp) only
  knows spells/skills and returned "" for Emote whenever it was set, so a
  live client's Emote button had no label. Cosmetic, flagged.

### 3. Red/green (measured, files in review/T239/)

RED measured with the two bodies stubbed back to the old behaviour (bridge
arm -> log-and-skip; `ActivateHotbarCommandSlot` -> `return false`) against
the new tests, API in place:
- `red_input_action_bridge.txt`: **4 FAILED** / 36 (`HotbarEmoteSlot_
  ReachesCommandRouteWithItsText`, `HotbarMacroSlot_ReachesCommandRouteWithItsText`,
  `HotbarCombatSlot_ReachesCommandRoute`, `HotbarMacroSlot_EmptyText_FailsWithoutDispatch`).
- `red_eq_action_handler_hotbar_command.txt`: **6 FAILED** / 7 (every
  Emote_/Macro_/Combat_ test; `SharedBody_RefusesNonCommandTypes` passes
  against the stub too, as it must).
- The 4 new `ActionDispatcherTest.ActivateHotbarCommandSlot_*` tests have no
  runtime red: the method did not exist (red = compile failure).
GREEN: `green_input_action_bridge.txt` 36/36, `green_action_dispatcher.txt`
55/55, `green_eq_action_handler_hotbar_command.txt` 7/7.

New binary `test_eq_action_handler_hotbar_command` (tests/CMakeLists.txt;
real EverQuest + zonetest harness). Observability deviation from the plan's
literal text: the harness has no zone connection and no Application, so
neither OP_Emote/OP_ChannelMessage bytes nor registries 2/3 exist there
(`SendChatMessage` bails "Not connected to zone server"; registry 1 has no
/wave or /say). What the tests pin instead: the text reaches
`ProcessChatInput`'s unified dispatch -- registry 1 misses and the line
(minus '/') arrives at `SetUnrecognizedCommandFallback`, the exact hook
Application wires to CommandProcessor (`"wave"`, `"say hi"`); with no
fallback the "Unknown command: /wave" SystemMessage event proves the same
reach. Combat is pinned by the recorded `AutoAttackChanged` bridge events
(true, then false) and `IsAutoAttackEnabled()`, and the no-target refusal.
The wire is the live leg's job.

### Verification bar

| step | result |
|---|---|
| 1. cmake --build build -j24 | OK (`build_full.txt`; 0 project warnings) |
| 2. ctest | `ctest_full.txt`: 2867/2869 under `-j24`. AudioManagerTest.PreloadCommonSounds (documented ALSA quirk) + `visual_gate` -- the latter a **timing** failure under the 24-way parallel run (`qeynos2: p95 CPU ms 152.386 exceeds ceiling 105.152`), not a render diff; rerun standalone at step 4 it PASSES. Net 2868/2869. |
| 3. build-gles2-x86.sh | OK (`build_gles2_x86.txt`) |
| 4. visual_gate.sh | **PASS**, goldens untouched (`visual_gate.txt`; memoryCoverage SKIP is the known no-eqe1_client gap) |
| 5. build-arm-noble.sh | OK (`build_arm_noble.txt`) |
| 6. check_file_sizes.sh fable | 19 checked: 10 ok, 9 warn (pre-existing), **0 fail** (`check_file_sizes.txt`) |

Test delta: `HotbarEmoteAndEmptySlots_AreSkippedNotCast` replaced by
`HotbarEmptySlot_IsSkippedNotCast` + 4 command-route tests (bridge 32 -> 36);
dispatcher 51 -> 55; harness +7 (new binary). The per-binary numbers are the
authority; the suite total at 584ff5b5 already differed from T230's 2862
because other units merged in between.

### Deviations
- `CombatManager::ToggleAutoAttack()` and the `HotbarModel` naming change
  (both above) are outside the plan's literal file list; flagged.
- The handler method is typed (`activateHotbarCommandSlot(type, text)`)
  rather than the plan's `processCommandText(text)` sketch, so Combat
  (no text) shares the one route and the type gate lives in the dispatcher.
- Harness observability as described in section 3.
- `refactor/deferred.md` NOT edited (live units touch it); the T174
  "Emote/Macro/Combat skipped" entry is closed by this unit -- orchestrator
  strikes.

## Live leg (executor, 2026-08-26 00:15-00:24, lock T239, befallen, Testchar + Oraclerogue)

**Result: PASS for Macro and Combat from a real X key press; Emote reaches
the dispatcher but its wire delivery is a pre-existing /emote question
(flagged below).** Two runs, evidence in review/T239/ (README there).

Setup: Oraclerogue (char 81) DB-staged into befallen 8 units from Testchar
and restored after; B = headless DEBUG observer, A = windowed EGL client
with `"hotbar":{"buttons":[Emote, Macro "/say T239 macro fired from key 2",
Combat]}` in its config (`Loaded hotbar config` confirmed); A's --script
targets Oraclerogue and prints a marker, the driver then sends `xdotool key
1`, `2`, `3`, `3`.

Per press (both runs identical in shape; run 2 lines quoted):

    Key pressed: 1 (...) action=HotbarSlot1  ->  reportAction: HotbarCommand -> success=true
      run 1 (slot "/wave"): [System] You wave           <- CommandProcessor::cmdWave, from the key
    Key pressed: 2 (...) action=HotbarSlot2  ->  reportAction: HotbarCommand -> success=true
      B: [CHAT] Testchar: T239 macro fired from key 2   <- OBSERVER, server-relayed /say
    Key pressed: 3 (...) action=HotbarSlot3  ->  EnableAutoAttack called (has_target=true, target_id=402)
      Auto attack ENABLED (sent both AutoAttack and AutoAttack2) ... reportAction: HotbarCommand -> success=true
    Key pressed: 3 (...) action=HotbarSlot3  ->  Combat state changed to: 0  (auto-attack off)

Screenshots `run2_after_combat_on_1024x768.png` etc.: chat shows both
"Testchar says" lines, Oraclerogue targeted, hotbar labels `/emo` and `/say`
(the new text-derived names).

Emote, flagged: `/wave` is an OP_Animation the headless observer does not
log; run 2's `/emote waves at Oraclerogue` (cmdEmote -> SendChatMessage
"emote" -> OP_ChannelMessage channel 22) reached the dispatcher (success,
no "Unknown command") but produced no [CHAT] line on B and no echo on A.
The hotbar key and a typed `/emote` take the identical path from
ProcessChatInput on, so this is the /emote command's own wire behaviour
(the Titanium client sends /emote as OP_Emote, a different struct), not
the hotbar route. Not chased; owed.

Restore: both characters' rows restored to `db_before.txt` values;
down.sh clean; lock deleted.
