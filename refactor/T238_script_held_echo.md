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

## Review (executed 2026-08-25, slot1, branch task/T238-script-held)

### Fix
`Application::updateScript()` (src/client/application.cpp): when a command
step is gated by `!m_fullyConnected`, print ONE `CMD outcome=held` line via
`printCommandResult()` with the identical `CommandDispatchResult` T30's
`processPreConnectionLifecycleCommands()` builds (`registry=pending`,
`recognized=false`, `message="waiting for connection"`). A new
`m_scriptHeldAnnounced` flag (application.h, next to the script state)
suppresses repeats for the same step and is cleared when the step is
dispatched, so a later step held again announces again.

The held line as logged (review/T238/script_run_no_server.log, line 11):

    CMD outcome=held registry=pending recognized=false message="waiting for connection" cmd="/fps"

### Deviation (flagged): line wording
The plan's sketch said `registry=none ... reason=not-connected`; the plan
also says "matching the T30 wording exactly (grep it)". T30's actual line
is `registry=pending recognized=false message="waiting for connection"`,
and the "one vocabulary" goal is the point, so the T30 form was used
verbatim. No `reason=` field exists in printCommandResult().

### Test (tests/test_headless_command_refusal.cpp, +3, in-process)
A `friend struct ApplicationScriptTestPeer` (application.h) reaches
`updateScript()`, `m_scriptSteps`, `m_scriptIndex`, `m_fullyConnected`.
- HeldScriptCommandAnnouncesOnceNotPerTick: 300 ticks not connected ->
  exactly 1 held line, exact T30 wording, step not consumed.
- ConnectedScriptCommandAdvancesWithoutHeldLine: connected -> 0 held lines,
  exactly 1 `CMD outcome=` line, index advanced.
- HeldThenConnectedAnnouncesOncePerCommand: held/attack -> connect ->
  dispatch -> disconnect, /sit held: 2 held lines total, second names /sit.
Red (review/T238/test_red.log): 2/3 fail on the unfixed tree (the
connected case passes trivially, as expected). Green: 6/6 in the binary.

### Scope item 3
T233's exact script (`wait 2`, `/fps`, `/camera`, `wait 2`, `/quit`) run
`--no-graphics` against TEST-NET-1 (192.0.2.1) for 25s (timeout 124):
`Script: wait 2.0s (step 1/5)` then the single held line for `/fps`, and
nothing else for the remaining ~23s. Before T238 this run logged no CMD line
at all (review/T233/script_run_no_server.log).

### Verification bar
1. `cmake --build build -j24`: OK (build_green.log).
2. ctest: **2851/2852**, sole failure AudioManagerTest.PreloadCommonSounds
   (ALSA host quirk). +3 tests. ctest_full.log.
3. `build-gles2-x86.sh`: OK (build_gles2.log).
4. Visual gate: not run -- plan narrows to "Full ctest"; no renderer code
   touched (application.cpp script path + header + test only).
5. `build-arm-noble.sh`: not run (same narrowing; the change is
   platform-neutral C++ in a TU every tree compiles).
6. `check_file_sizes.sh fable`: 0 FAIL; application.cpp WARN pre-existing
   (2365 -> 2379 lines). fable had moved to the T236 merge, so the script
   also listed inventory_manager.* (T236's, unchanged here).
