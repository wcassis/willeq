# T233 — renderer-registry commands lose their console echo (T142 leftover)

Slot: slot2. x86. NO eqemu. Class A. Territory: the renderer's `chat()`
lambda / slash-command reply path in src/client/graphics/ (irrlicht_renderer*
processSlashCommand reply plumbing) and the console/log echo it should reach;
tests. Do NOT touch combat.cpp, action/*, eq_action_handler (T230 unmerged),
state/tradeskill*, eq_merchant.cpp (T232).

## Defect (T142, confirmed still open by T223's Review)
When a registry-3 (renderer) command replies via the renderer's `chat()`
lambda, the text reaches the in-game chat panel but NOT the console log /
headless-style `CMD outcome=` echo, so scripted runs and the log show
nothing for e.g. `/newskills`, `/fps`, `/wireframe`. T223 fixed the
headless refusal but explicitly left this (different site).

## Scope
1. Find the site: how registry-3 replies are emitted vs how action-layer
   replies are (`AddChatSystemMessage` also logs). Route the renderer reply
   through the same echo so a `--script` run logs it. Keep the chat-panel
   behaviour identical.
2. Test: a renderer command in the graphics test harness (or a unit test on
   the reply plumbing) produces the log/echo line. Red/green measured.
3. Run a short `--script` with `/fps` (or similar) under the normal build
   and show the echo line in the log (no server needed if the command is
   pre-connection safe — check T223's note that pre-connection everything
   but /quit and /help is held; if held, use the graphics test harness).

## Verification
Full ctest; visual gate PASS (renderer touched); Review; evidence
review/T233/. Commit: `T233: renderer command replies echo to the console log`.
