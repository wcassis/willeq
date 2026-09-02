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

---

## Review

Branch `task/T319`, cut from `fable` at **`f5893bcd`** ("Batch J: fold J01's six
template corrections into the batch plan; write Wave 1 (J02/J03/J04)"). All base
comparisons below are against `$(git merge-base fable HEAD)` = that same hash,
not `fable`, which moved during the dispatch (running `check_file_sizes.sh fable`
un-scoped pulled slot3's `entity_renderer.cpp`/`eq_guild.cpp`/`spell_effects.cpp`
into the check — see Deviations).

### The R key: it reaches the chat input, and the value is right

**Yes — end to end, and pinned.** The gap the plan described was real and was
exactly where it said, but closing it needed a *second* piece the plan flagged:
with only the `case` added, the prefill would have stopped at a debug log.

The completed chain, all production code:

| Step | Where | Thread |
|---|---|---|
| `KEY_KEY_R` → `HotkeyAction::ReplyToTell` | `hotkey_manager.cpp:508` (already existed) | render |
| → `RendererAction::ReplyToTell` | **new** enumerator + **new** `case HA::ReplyToTell: pushBridgeAction({RA::ReplyToTell})` in `RendererEventReceiver::OnEvent` | render |
| → `bridgeQueue_` | via `pushBridgeAction()` only — no raw member access added | render→game |
| → `InputAction::ReplyToTell` | **new** `case RA::ReplyToTell` in `GraphicsInputHandler::updateFromEventReceiver()`, gated on `chatFocused` like its siblings | game |
| → `ActionDispatcher::openReplyToTell()` → `Success("/tell <Name> ")` | `input_action_bridge.cpp:160-163` (already existed, T316) | game |
| → the action callback | **new** `m_inputBridge->setActionCallback(...)` in `Application` — the **first production caller** — whose body is `IrrlichtRenderer::handleActionResult()` | game |
| → `requestChatPrefill()` | **new**, parks the text under `chatPrefillMutex_` | game |
| → `takeChatPrefill()` → `chatPanelState_.inputText` | **new** block at the top of `IrrlichtRenderer::processChatInput()` | render |

Design notes:

- The decision ("is this a ReplyToTell success carrying text?") lives in
  `IrrlichtRenderer::handleActionResult()`, **not** in the lambda, so the test
  drives the same code the app does rather than a copy of it.
- `bridgeQueue_`'s invariant is intact: the one new writer is a
  `pushBridgeAction()` call. `grep -n 'bridgeQueue_' include/client/graphics/irrlicht_renderer.h`
  still shows the member and the three accessors and nothing else.
- The prefill needed its own cross-thread channel because the natural one — a
  `GameEventType` case in `irrlicht_bridge.cpp` — is **slot3's territory this
  dispatch**. The mutex-guarded park is deliberately the same shape as
  `bridgeQueue_` (no raw access; one writer method, one taker method) rather
  than a new pattern.
- The take runs **before** the Enter/slash checks and regardless of current
  focus. Dropping the text when chat happened to be focused would lose it
  silently, which is the failure class this unit exists to close.
- The callback is cleared in `Application::shutdown()` immediately before
  `m_renderer.reset()`: `m_renderer` (declared at `application.h:507`) is
  destroyed *before* `m_inputBridge` (`:474`), so the captured raw pointer
  would otherwise outlive its target.

**Evidence — `tests/test_reply_to_tell_key.cpp` (new, 5 tests).** Drives the real
`RendererEventReceiver::OnEvent`, the real `GraphicsInputHandler`, the real
`InputActionBridge` + `ActionDispatcher`, the same callback `Application`
installs, and the real `processChatInput()` on a bare never-`Init()`'d
`IrrlichtRenderer` (the T235/T66 no-device idiom). Only the `IActionHandler` is a
fake. It asserts on `chatPanelState_.inputText` — the value a user sees — not on
the dispatcher's return.

- `RKeyOpensChatPrefilledWithTheLastTellSender` — `"/tell Grimjaw "`, cursor after
  the trailing space, chat focused.
- `NoTellReceived_OpensNothing` — the T316 refusal half; no box, no `"/tell  "`.
- `PrefillIsTakenExactlyOnce` — later frames don't re-open over typed text.
- `RIsNotAHotkeyWhileChatIsFocused` — bridge queue stays empty.
- `PrefillFollowsTheMostRecentSender` — read at keypress time, not cached.

**Measured RED**: with only `case HA::ReplyToTell:` removed and everything else in
place, 3 of the 5 fail (`inputText` is `""`). Restored and re-verified green.

### Delete-or-wire verdicts

| # | Item | Verdict | One-line reason |
|---|---|---|---|
| 1 | `IOutputRenderer::updateCurrentTargetHP` | **WIRE** | "Nobody should" is wrong: `printStatusLine()` prints `Target: <name> <hp>%` from the value this method is the only other writer of, so headless target HP was **frozen at acquisition** — a live staleness bug, not an unused method. |
| 2 | `Application::setupHotbarCallback()` | **DELETE** | The route it was a placeholder for is already live (`HotbarModel::setSlot`/`clearSlot` → `publishSlot()` → `HotbarSlotAssigned`; `publishAllSlots()` at zone-in) — an empty body plus a stale TODO, with nothing left for it to carry. |
| 3 | `EverQuest::StartMove*` / `StartTurn*` | **KEEP + comment** | Not four stray functions: they are the head of a complete, live path (flags → `UpdateMovement()`'s keyboard branch → `UpdateKeyboardMovement()` → `SetPosLocal`/`FixZ`/`CheckZoneLine`/`SendPositionUpdate`); deleting them deletes the only game-side mover the client has and leaves a `Stop*` half that switches off something nothing can switch on. |
| 4 | `CameraController::update()` | **KEEP + comment** | It is the **only** implementation of `CameraMode::Free`, and Free is reachable two ways (`cycleCameraMode()` via the ToggleCameraMode hotkey, and `--camera free`) — deleting it removes the body and leaves both doors into a mode where the view simply freezes. |
| 5 | `RendererEventReceiver::currentMode_` | **DELETE** | `HotkeyMode` has two values, `getAction()` checks `Global` first in every case, nothing ever wrote the member and nothing ever called `getCurrentMode()` — configuration that could not be configured; `OnEvent` now passes `HotkeyMode::Player` literally. |

**Every zero-caller claim was re-verified at `f5893bcd`, not taken from
`7e0c7823`.** All five still held, with one correction to the plan's framing:
item 5's `currentMode_` *is* read (twice, in `OnEvent`) — what it lacks is a
*writer*, so it is a compile-time constant wearing a member's clothes.

**On item 1 and the test fixture** (the plan's explicit caution): the
`tests/command_processor_test_fixture.h:70` stub was **not** vestigial and was
**not** the reason to keep the method. The method earned its place on its own —
`ConsoleRenderer`'s status line reads the field it writes. The stub is now what
pins real behaviour rather than a stub of nothing.

**On item 3 and autorun** (the plan's explicit caution): these are kept because
the keyboard-movement path is the only game-side mover the client has, **not**
on autorun's account. The header comment in `eq_movement.cpp` says so out loud
and names what would wire them — an `IActionHandler`/`ActionDispatcher` start-half
next to the existing `stopAllMovement()`, driven from `InputActionBridge`, *plus*
a decision about what the renderer sees, because `SetPosLocal()` publishes no
`PlayerMoved` (the same wall T316 hit). Board item 10 (autorun) was annotated
accordingly, not closed.

**Item 4's comment** names the concrete wiring: a `cameraMode_ == CameraMode::Free`
branch in `IrrlichtRenderer::updatePlayerMovement()`, which is the one legal home
for it because that function is already the single owner of the destructive
mouse-delta read (T312/T315). It is left undone because "does the player keep
moving while the camera flies free?" is a design question, not a cleanup one.

**What proved each deletion dead:**

- `setupHotbarCallback()`: grep over `src/`, `include/`, `tests/` — three
  occurrences total (declaration + definition + two call sites), body empty; the
  live replacement route traced through `publishSlot()` to `pushEvent()`. Suite
  green after removal.
- `currentMode_` / `getCurrentMode()`: grep — the getter had zero callers
  anywhere; the member had zero writers; the two readers are literal-substituted
  with no behaviour change (the value was and is `HotkeyMode::Player`). Suite
  green.

**Evidence for item 1 — `tests/test_console_target_hp.cpp` (new, 3 tests).**
Drives a real `ConsoleRenderer` over a real `GameState` EventBus and asserts on
the captured status line:

- `StatusLineFollowsTheTargetsHpDown` — 100% → 37% → 4%.
- `AnotherEntitysStatsDoNotTouchTheTarget` — every entity publishes
  `EntityStatsChanged`; only the current target's is taken.
- `NoTargetMeansNoTargetSection`.

**Measured RED**: with the `updateCurrentTargetHP()` call removed,
`StatusLineFollowsTheTargetsHpDown` fails (still prints 100%). Restored and
re-verified green. `updateCurrentTargetHP()` also now takes `m_outputMutex`,
which `printStatusLine()` already held for the read — it was an unlocked write to
a mutex-protected field the moment it acquired a caller.

### Verification

| Step | Result |
|---|---|
| `cmake --build build -j24` (GLES2 tree) | **PASS**, EXIT=0 |
| `ctest --test-dir build -j6 --output-on-failure` | **PASS** — 3461/3462; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk). Suite grew by the 8 new tests. |
| `bash scripts/build-gles2-x86.sh` | **PASS**, EXIT=0 |
| `scripts/ci/visual_gate.sh` | **PASS**; `git status` clean of any golden — only the 10 sources + 2 new tests are modified/untracked |
| `bash scripts/build-arm-noble.sh` | **PASS**, EXIT=0 (first invocation hit the 600 s Bash ceiling mid-build and was re-run per protocol) |
| `check_file_sizes.sh --files <this unit's 11 files>` | **No new FAILs** — `irrlicht_renderer.cpp` was already FAIL at the base (3274 > 3000); everything else is ok/WARN at its pre-existing level |

### Line counts for Batch J re-measurement

| File | Base `f5893bcd` | After T319 | Δ |
|---|---|---|---|
| `src/client/application.cpp` (**J06**) | 2356 | **2368** | +12 |
| `src/client/graphics/irrlicht_renderer.cpp` (**J11**) | 3274 | **3296** | +22 |

⚠ **Both files GREW.** The plan predicted deletions would shrink them; four of the
six items resolved to wire-or-keep, and the two genuine deletions
(`setupHotbarCallback`, `currentMode_`) were small. Of `application.cpp`'s +12,
the deletion removed 12 lines and the `setActionCallback()` wiring (with its
comment) added 24. Of `irrlicht_renderer.cpp`'s +22, ~20 are the
`handleActionResult()` comment block. **J06 and J11 should re-measure from these
numbers, not from a shrink.** Also affected, same base→after: `renderer_frame.cpp`
1731→1752, `eq_movement.cpp` 2070→2106 (all comment), `irrlicht_renderer.h`
2472→2519.

### Deviations

1. **Two files outside the plan's stated territory were edited**, both because the
   chain physically runs through them and neither is claimed by a concurrent slot:
   `src/client/input/graphics_input_handler.cpp` (+9, the `RA::ReplyToTell` case —
   the chain cannot cross threads without it) and
   `src/client/graphics/renderer_frame.cpp` (+21, `processChatInput()`'s take — the
   plan named `irrlicht_renderer.cpp` + header, but `processChatInput()` lives in
   this sibling TU of the same class). `entity_renderer.cpp`, `irrlicht_bridge.cpp`,
   `spell_effects.cpp` and `eq_guild.cpp` were **not** touched.
2. **The plan's item-5 wording ("dead configuration") was slightly off** —
   `currentMode_` is read, not unread; it has no writer. Verdict unchanged.
3. **The plan's item-1 impact assessment was wrong in the board's favour.** Board
   item 20 said "*Impact:* none today". It was a live headless bug (frozen target
   HP on the status line). Corrected on the board.
4. **`check_file_sizes.sh fable` (un-scoped) reported FAILs for slot3's files** —
   `fable` moved under the branch, so the base-rev diff picked up T318's territory.
   Re-run with `--files` scoped to this unit's actual changed set, per the dispatch's
   warning. The scoped run is the one reported above.
5. **`refactor/deferred.md` was edited** (items 13, 20, 21 closed; item 10 annotated;
   two AUDIT-section historical mentions and the two "not swept" bullets struck).
   It is shared with concurrent slots and may conflict on merge.

### Found and deliberately not fixed

- **`ConsoleInputHandler` never raises `InputAction::ReplyToTell`.** Left alone,
  and noted on the board as deliberate: the console already has `/reply`, which is
  the same capability by the route console users actually use.
- **`CameraMode::Free` is a mode in which the camera does not move.** Item 4's
  comment is the record; wiring it is a design call (see above), not cleanup.
- **The game-side keyboard-movement path has no writer.** Item 3's comment is the
  record; board item 10 carries the autorun half.
- **`printStatusLine()`'s target section is the only reader of `m_targetHpPercent`.**
  Fine now that the value is fresh, but nothing shows target HP in the *graphical*
  headless path through `IOutputRenderer` — that goes through `IrrlichtBridge`'s
  own `setCurrentTargetInfo()`, a separate route. Not unified; not a defect.
- **`InputActionBridge::setActionCallback()` now has exactly one production
  caller, and it only handles `"ReplyToTell"`.** Every other dispatch result still
  reaches only the debug log in `reportAction()`. That is correct for today (no
  other action produces a value a user is meant to see), but the callback is now a
  real UI channel and the next action that wants one should extend
  `handleActionResult()` rather than invent a second route.
