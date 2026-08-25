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

## Review (executor, 2026-08-25, slot2, off fable ea2f7ca6)

Evidence: /mnt/projects/fable/review/T235/ (README.md indexes every file).

### Confirmed live FIRST (Scope 1) -- BEFORE, pre-fix binary
Testcaster (wizard, gems 1+2 = spell 54 Frost Bolt, server-side
`character_memmed_spells`), befallen, Xvfb :78 + `--egl-x11` +
`--eqe1-path`, xdotool `alt+1`, `alt+1`, `alt+2`. Log
`before_live_client_testcaster_debug.log`:
- 3x `[INPUT] Key pressed: ... action=SpellGem1|SpellGem1|SpellGem2` --
  the receiver saw every press.
- ZERO `reportAction: CastSpell`, zero `BeginCast`. Swallowed, as planned.
- No `Spell gem N pressed` either: the render-thread consumer was a pure
  exchange-and-discard because `spellGemCastCallback_` was never set (see
  "Also found" 1). The plan's "does nothing useful with it" is exact.

### Fix (Scope 2)
- `src/client/graphics/renderer_frame.cpp` `processChatInput()`: the
  `getSpellGemCastRequest()` exchange + callback block deleted; T235 note
  next to the T230 hotbar note. Nothing non-destructive to keep -- the
  renderer never used the value for anything of its own.
- Dead plumbing removed with it (deviation -- outside the listed
  territory but the only callers of the deleted block, and leaving them
  would create a fresh implemented-but-unreached member):
  `IrrlichtRenderer::setSpellGemCastCallback`/`spellGemCastCallback_`
  (include/client/graphics/irrlicht_renderer.h),
  `GraphicalRenderer::SpellGemCastCallback`/setter/member
  (include/client/output/graphical_renderer.h), and the wiring lambda in
  `Application::connectRendererCallbacks()` (src/client/application.cpp,
  replaced by a comment). No other references existed (grep, tests
  included). T234 (tests/ only) and T236 (InventoryManager) untouched.
- `graphics_input_handler.cpp` needed no change: its
  `consumeSpellCastRequest()` was already the intended single consumer.

### AFTER, fixed binary (Scope 3) -- race fixed, press still does not cast
`after_live_client_testcaster_debug.log`, same three presses:
- 3x `InputActionBridge::reportAction: CastSpell -> success=true`, each
  within ~10ms of its `Key pressed` line. The request reaches the game
  thread every time. That is the defect this unit names, closed.
- ZERO `BeginCast` / `SpellAction` / cast packet. The plan's expected
  "BeginCast from the server, spell effect" did NOT happen, and the
  reason is one step past this unit's territory:
  `InputActionBridge::processSpellCasts()` -> `ActionDispatcher::castSpell`
  -> `EqActionHandler::castSpell()` -> `CombatManager::CastSpell(SpellSlot)`
  which reads `CombatManager::m_spell_gems` -- a SECOND gem map written
  only by `CombatManager::MemorizeSpell()` (caller:
  `EqActionHandler::memorizeSpell(gem, id)`), never by the player-profile
  load. The real gems live in `EQ::SpellManager::m_spell_gems`
  (`/gems` in the same run lists `[1] Frost Bolt Ready [2] Frost Bolt
  Ready`; `/cast` uses `beginCastFromGem` on that store). So the hotkey
  returns success on an empty map and sends nothing.
  `src/client/eq_action_handler.cpp` is T234's excluded territory
  (`eq_action_handler*`), so NOT fixed here. Follow-up is the T101 shape
  (repoint `castSpell`/`castSpellOnTarget` at `SpellManager` the way
  `interruptCast` was) -- one unit, with the `CombatManager::m_spell_gems`
  map deleted alongside.
- Screenshots `before_*`/`after_*_alt2_press_1024x768.png`: real renders
  (verified by eye: HUD, /gems chat lines, gem panel showing "Fros" in
  A1/A2), nothing to judge -- no cast either way.

### Unit test (Scope 4) -- measured red/green
`tests/test_spell_gem_key_race.cpp` (new; registered in
tests/CMakeLists.txt; two hand-expanded `friend` lines + the fixture in
irrlicht_renderer.h, T116 idiom). Real `RendererEventReceiver::OnEvent`
(LMENU down then KEY_1..8 -- the exact X11 delivery, Alt tracked via
keyIsDown_), real `IrrlichtRenderer::processChatInput()` on a bare
renderer, then `GraphicsInputHandler::consumeSpellCastRequest()`.
- RED (pre-fix): 2/2 FAIL -- `red_spell_gem_key_race.txt`.
- GREEN: 2/2 PASS -- `green_spell_gem_key_race.txt`.

### Scope 5 -- other exchange/consume accessors with two callers
Destructive `RendererEventReceiver` accessors and who calls them
(renderer_frame.cpp = render thread, graphics_input_handler.cpp = game
thread):

| accessor | render thread | game thread | verdict |
|---|---|---|---|
| `getSpellGemCastRequest()` | was renderer_frame.cpp:830 | GIH:190 | THE defect; fixed |
| `getHotbarActivationRequest()` | none (T230 removed) | GIH:202 | single consumer, OK |
| `enterKeyPressed()` | renderer_frame.cpp ~853 (opens chat) | GIH ~148 (`InputAction::OpenChat`) | two consumers, same shape |
| `slashKeyPressed()` | renderer_frame.cpp ~861 (opens chat with "/") | GIH ~151 (`OpenChatSlash`) | two consumers, same shape |
| `escapeKeyPressed()` | none in renderer_frame.cpp | GIH ~154 (`CloseChat`) | single consumer, OK |
| `popKeyEvent()` / `clearPendingKeyEvents()` | renderer_frame.cpp ~875/~922 (typed chat text) | GIH:242/257 (`IInputHandler::popKeyEvent`) | shared queue, two drainers |
| `getCameraZoomDelta()` / volume deltas | renderer_frame.cpp ~802-821 | none | single consumer, OK |
| `drainActions()` / `drainBridgeActions()` | render / game respectively | -- | split by design (actionQueue_ vs bridgeQueue_), OK |
| `wasLeftButtonClicked()` etc. | renderer_frame.cpp:527 | GIH:71 | plain reads reset per frame, not exchanges -- not this class |

Not fixed (not one line, and not the same effect): the Enter/`/`
exchanges are raced, but in graphics mode the render thread is the
consumer that does the work (opening the chat input), and the game-thread
`OpenChat`/`OpenChatSlash` actions have NO consumer at all
(`grep InputAction::OpenChat src/ -- only graphics_input_handler.cpp`), so
whichever side wins, the visible behaviour is right or a no-op, never a
lost keypress that mattered. The `pendingKeyEvents_` queue has the same
property: the game-thread `popKeyEvent()` has no production caller. Both
are dead second consumers, listed for the reachability sweep rather than
raced-and-lost defects.

### Also found (not fixed, out of territory)
1. `Application::connectRendererCallbacks()` is dead in its entirety:
   `dynamic_cast<output::GraphicalRenderer*>` -- nothing in the tree
   derives from that class (grep), so the function logs "Renderer
   callbacks connected" and sets nothing. BEFORE log line 48 shows exactly
   that (no "Connecting renderer callbacks to action dispatcher" before
   it). The target/door/chat-submit callbacks it sets on the abstract base
   are likewise never reached; `IrrlichtRenderer`'s own parallel
   `setTargetCallback`/`setDoorInteractCallback`/`setChatSubmitCallback`
   have NO callers either (grep src/) -- the live routes are bridge
   intents (e.g. `ChatSubmitIntent` from `processChatInput()`).
   Reachability pattern; report only.
2. The `CombatManager::m_spell_gems` dead end above.

### Verification bar
1. `cmake --build slot2/build -j24` -- OK.
2. ctest: **2823/2824**; sole failure AudioManagerTest.PreloadCommonSounds
   (ALSA, documented) -- `ctest_full.txt`. (+2 new tests vs the 2745 baseline
   plus other units' additions since.)
3. `build-gles2-x86.sh` -- OK (exit 0).
4. Visual gate **PASS**, goldens untouched -- `visual_gate.txt`; `git status`
   shows only this unit's five files + the new test.
5. `build-arm-noble.sh` -- OK (exit 0).
6. `check_file_sizes.sh fable` -- 0 fail (4 pre-existing WARNs on >1500-line
   files) -- `check_file_sizes.txt`.

### eqemu
Lock taken atomically (noclobber) as T235, server up on befallen only for
the two live legs, Testcaster moved into befallen at Testchar's spot for
the runs and restored to zone 25 / -259 / -1201 / -5.75 / 0; Testchar
verified at zone 36 / -77.9232 / 21.9086 / 3 / 190 (`db_restored.txt`);
`down.sh` clean; lock deleted.

### Deviations
- Removed the dead callback plumbing in three files outside the listed
  territory (see Fix). No overlap with T234/T236.
- Used Testcaster instead of Testchar: Testchar is a level-2 warrior with
  no spells; a gem key needs a memorised gem, and Testcaster already had
  two from the server profile. Testchar's row was never written.
- Scope 3's "BeginCast, spell effect" not achieved -- root cause named
  above, in excluded territory; the race itself is proven fixed by the
  three `reportAction: CastSpell` lines.
