# T238 — a --script command held pre-connection emits `CMD outcome=held` (T233 finding)

Slot: slot1. x86. NO eqemu. Class A. Territory: src/client/application.cpp
script-advance / held-command path (`updateScript` and the T30 held
intercept), tests. Do NOT touch renderer_frame.cpp / graphics_input_handler
(T235), inventory/*, command_processor equip path, eq_merchant (T236).

## Defect
`Application::update()` gates `--script` advance on `m_fullyConnected`, so a
scripted tour with no server sits at step 1 forever and logs NOTHING —
whereas T30's held intercept prints `CMD outcome=held` for piped-stdin
commands. Scripted runs therefore cannot distinguish "held" from "hung"
(T233 `review/T233/script_run_no_server.log`).

## Scope
1. When the script runner would advance but is gated, log ONE
   `CMD outcome=held registry=none cmd="<text>" reason=not-connected` line
   for the pending command (once per command, not per frame), matching the
   T30 wording exactly (grep it) so log consumers see one vocabulary.
2. Test in tests/test_headless_command_refusal.cpp or the script-runner
   test (grep for existing `--script` tests): pending command + not
   connected → exactly one held line; connected → advances, no held line.
   Red/green.
3. Run the T233 no-server script for 5s and show the single held line.

## Verification
Full ctest; Review; evidence review/T238/. Commit:
`T238: held script commands say so once`.
