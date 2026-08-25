# T235 — spell-gem hotkeys: the same render-thread consumer that starved the hotbar (T230 #12)

Slot: slot2. x86. OWNS eqemu briefly (noclobber) for the keypress proof.
Class A. Territory: src/client/graphics/renderer_frame.cpp
(`processChatInput` spell-gem consume only), src/client/input/
graphics_input_handler.cpp if needed, tests. Do NOT touch action/*,
eq_action_handler*, tests/test_eq_action_handler_* (T234 slot1 unmerged).

## Defect
`IrrlichtRenderer::processChatInput()` (render thread) calls
`getSpellGemCastRequest()` — a destructive exchange — and does nothing
useful with it, while `GraphicsInputHandler::consumeSpellCastRequest()`
(game thread) is the intended consumer. Exactly the T230 hotbar defect
(`review/T230/README.md`, "two-consumer race"). Expected: spell-gem keys
never cast live.

## Scope
1. Confirm live FIRST (before/after discipline): T230's Xvfb + `--egl-x11`
   + xdotool driver, a gem memorised (`/memspell` or config), press the
   gem key: expect NO cast on the pre-fix binary. Record.
2. Delete the render-thread consume (keep any non-destructive read if the
   renderer genuinely needs it — check what it did with the value).
3. After: same press → `BeginCast` from the server, spell effect. Record.
4. Unit test on the receiver: after a key event, the request survives a
   render-frame `processChatInput()` and is still consumable by the game
   thread (fake renderer harness like tests/test_renderer_command_echo.cpp
   or test_input_action_bridge.cpp). Red/green.
5. Grep `renderer_frame.cpp`/`graphics_input_handler.cpp` for any OTHER
   `exchange`/consume pair with two callers; list them (fix only if same
   shape and one line).

## Verification
Full ctest; visual gate; Review; evidence review/T235/. Commit:
`T235: spell-gem keys reach the game thread -- render-thread consumer removed`.
