# T319 — The unreachable sweep: wire the R key, delete the rest

**Slot: slot2. Branch `task/T319` cut from `fable`. x86 only. No Pi.
No eqemu — slot1 holds it for T297. Headless acceptance throughout.**

Closes **tier 3 items 20 and 21** of `refactor/deferred.md`'s class-A list plus
the reachability findings T315, T316 and T317 each reported and could not fix in
their own territory. **This is the tenth-plus instance of the T264 pattern** —
implemented, plausible-looking, and reached by nothing. Every item below was
verified by grep at `7e0c7823`; **re-verify each yourself before acting, and if
any has grown a caller, leave it and say so.**

## Wire it (one item — it completes a user-facing feature)

**The `ReplyToTell` key (R) produces no action at all.** T316 fixed
`ActionDispatcher::openReplyToTell()` — it now returns a real
`/tell <Name> ` prefill or an honest failure — but nothing reaches it from a
keypress. The binding exists (`hotkey_manager.cpp:508`, `KEY_KEY_R`) and the
consumer exists (`input_action_bridge.cpp:160-163`), and **the gap is in the
middle**: `HotkeyAction::ReplyToTell` has **no case in `IrrlichtRenderer`'s
hotkey-action switch** and no `RendererAction` enumerator, so the key decodes
to nothing. `ConsoleInputHandler` never raises it either.

Wire it — the surrounding `bridgeQueue_` cases in that switch are the template.
⚠ **T312 put `bridgeQueue_` behind a mutex and made `pushBridgeAction()` the
only writer; there is no raw access to the member anywhere and the invariant is
grep-enforceable. Keep it that way.**

⚠ Also from T316: **`InputActionBridge::setActionCallback()` has no production
caller**, so the prefill currently reaches only a debug log. Wiring the key is
pointless if the result goes nowhere — **follow the value all the way to the
chat input and prove it arrives**, or report precisely where it stops.

## Delete or wire (say which, and why, for each)

1. **`IOutputRenderer::updateCurrentTargetHP` — zero production callers**
   (item 20). Declared `include/client/output/output_renderer.h:197`;
   implemented `console_renderer.cpp:311` and `null_renderer.cpp:69`; the only
   other occurrence is a test fixture (`tests/command_processor_test_fixture.h:70`).
2. **`Application::setupHotbarCallback()` is an empty body still called twice**
   (item 21) — `application.cpp:1883-1886`, called from `:1752` and `:1865`.
   The hotbar-changed callback is genuinely not wired, so a hotbar mutation from
   the game side does not notify the renderer by this route.
3. **`EverQuest::StartMoveForward` / `StartMoveBackward` / `StartTurnLeft` /
   `StartTurnRight` have zero callers** (found by T316) — only the `Stop*` half
   is reachable, so the game-side keyboard-movement system can be switched off
   but never on. ⚠ **Do NOT delete these on autorun's account** — autorun is a
   separate, still-open item and T316 established that a handler-side
   implementation cannot work anyway (game-side movement writes through
   `SetPosLocal()`, which deliberately fires no `PlayerMoved`). Decide these on
   their own merits and say what autorun would need if it ever lands.
4. **`CameraController::update()` — constructed, never called** (found by T315).
5. **`RendererEventReceiver::currentMode_` — dead configuration** (found by T315).

## The judgement this unit is actually for

Deleting is usually right, but **"nobody calls it" is not the same as "nobody
should"**. For each item, say in one line which it is. Item 3 in particular is a
whole input path, not a stray function — if deleting it removes the only
plausible foundation for a feature the project wants, keeping it with a comment
that says *what would wire it* is the better answer. T304 set this precedent:
Will kept `GraphicalMode`'s console path deliberately, and what got removed was
the misleading silent fallthrough, not the capability.

⚠ **Do not delete a public interface method solely because only a test uses it**
(item 1's fixture) — say whether the test is pinning real behaviour or is itself
vestigial.

## Acceptance

- The R key path pinned **end to end**, from the keypress decode to the value
  arriving where a user would see it — not at the dispatcher.
- Every deletion: the suite green, and a one-line note in the Review saying what
  proved it dead.
- Full `ctest -j6` (**NOT -j24**), zero new failures vs `refactor/test_baseline.txt`.
- Visual gate PASS, goldens untouched.
- `check_file_sizes.sh --files` no new FAILs. Deletions here shrink two files
  Batch J is queued to split (`application.cpp` = J06, `irrlicht_renderer.cpp` =
  J11) — **report the new line counts** so those plans can be re-measured.

## Territory

`src/client/output/`, `src/client/application.cpp`,
`src/client/graphics/irrlicht_renderer.cpp` + header,
`src/client/eq_movement.cpp`, the camera controller, plus your tests.
⚠ Concurrent: slot1 T297 (verification only, no `src/` changes expected),
slot3 T318 (`spell_effects.cpp`, `eq_guild.cpp`, `entity_renderer.cpp`,
`irrlicht_bridge.cpp`). **`entity_renderer.cpp` is slot3's — stay out.**
