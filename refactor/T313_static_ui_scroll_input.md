# T313 — The static UI has no scroll input at all: chat, skills and loot are pinned to page one

**Slot: slot3. Branch `task/T313` cut from `fable`. x86 only. No Pi.
One live session for the acceptance leg.**

Closes **tier 2 item 12** of `refactor/deferred.md`'s "THE ONE CLASS-A LIST".

## The defect

Three panels carry a `scrollOffset` that **both render and hit-test honour**, and
**nothing but a clamp or a reset ever writes one**:

| panel | field | honoured at | only writes |
|---|---|---|---|
| chat | `include/client/graphics/ui/static_panels.h:67` | `static_panels.cpp:173,199` | `:169-170` clamp |
| skills | `:340` | `static_panels.cpp:846`, `static_ui_input.cpp:269` | — |
| loot | `:386` (+ `maxScrollOffset()` at `:390`) | `loot_panel.cpp:67`, `static_ui_input.cpp:233` | `loot_panel.cpp:192` reset, `:233` clamp |

`StaticUIInput` (`include/client/graphics/ui/static_ui_input.h`) has **no wheel
handler at all** — `processInput()` takes only `mouseX/mouseY/leftClicked` — and
no PageUp/PageDown binding exists (`hotkey_manager.cpp:457` binds `KEY_NEXT` to
StrafeRight).

*Impact:* chat scrollback is unreachable, the skills window shows only its first
page, and **a corpse with more items than fit is partly unlootable**.

## The shape of the fix — one input path, not three

This is the whole point of the unit. The wheel has to arrive once, be routed to
whichever panel is under the cursor (or focused), and move that panel's existing
`scrollOffset` within its existing clamp. Do not add three bespoke handlers, and
do not invent a second scrolling model — the clamps and the `maxScrollOffset()`
helper already define the semantics; you are supplying the missing writer.

1. **Get the wheel event to `StaticUIInput`.** Irrlicht delivers it as
   `EMIE_MOUSE_WHEEL` in `RendererEventReceiver::OnEvent`. There is an existing
   accumulator convention right there for exactly this shape —
   `getCameraZoomDelta()` / `getMusicVolumeDelta()` (`irrlicht_renderer.h:396-399`)
   are read-and-reset floats. Follow it rather than inventing one, and make sure
   a UI-consumed wheel does NOT also zoom the camera (that is a live regression
   if you get it wrong — check what currently consumes the wheel BEFORE you
   change routing, and say what you found).
2. **Route by cursor position**, using the same hit-testing the panels already do.
3. **Also bind PageUp/PageDown** for the focused panel — a keyboard path matters
   because `--script` runs and headless harnesses cannot generate a wheel, which
   is the same gap `/hideui` closed for the UI-hide feature in T301 Leg D. If a
   command is the cheaper reachable path, add one and say why.
4. **Chat's offset is measured from the BOTTOM** (`:67`: "0 = at bottom") and the
   other two from the top. Respect each panel's own convention; do not harmonise
   them.

⚠ **Slot2 is in `RendererEventReceiver` this dispatch (T312 — the bridgeQueue_
thread-safety unit) and will be editing `irrlicht_renderer.h`/`.cpp` and
`renderer_frame.cpp`.** Your step 1 touches the same header. Keep your edit
there to the minimum (the accumulator + its accessor), do it in ONE small hunk,
and flag it in your Review as sibling territory so the orchestrator can order
the merges. Everything else lives in `src/client/graphics/ui/`.

## Acceptance

- **A test per panel** that scrolls past the first page and asserts the rendered
  row set changed — the panels are testable headlessly (`tests/test_ui_atlas_pixels.cpp`
  and the T290 loot tests are the precedent; read them first).
- **Live, with a real wheel**: `xdotool` click 4/5 over each panel. The loot panel
  is the one that matters most — put more items on a corpse than fit, scroll, and
  loot a row that was **not** reachable before this unit. That is the user-visible
  proof; a screenshot pair goes to `/mnt/projects/fable/review/T313/` with a README.
- **Full `ctest -j6`** (NOT -j24), zero new failures vs `refactor/test_baseline.txt`.
- If a golden re-bless looks necessary, **STOP and report** — do not re-bless.

---

## Review

**Branch `task/T313`, cut from `fable` `9ae1460c`. x86 only, no Pi.**

### What currently consumed the wheel: NOTHING. The plan's premise was wrong, and that is the finding.

The plan said to check "what currently consumes the mouse wheel (camera zoom
does)". It does not. `RendererEventReceiver::OnEvent()`'s
`EET_MOUSE_INPUT_EVENT` switch had cases for the four button transitions and
`EMIE_MOUSE_MOVED`, and **no `EMIE_MOUSE_WHEEL` case at all** — every wheel
event fell into `default: break` and was discarded by the entire client.
Camera zoom is `cameraZoomDelta_`, and its only writers are the two hotkey
arms `HA::CameraZoomIn`/`HA::CameraZoomOut` (`irrlicht_renderer.cpp:379-380`,
bound to `+`/`-` in `config/hotkeys.json`); nothing anywhere assigns it from a
wheel. The only other appearance of the wheel in the tree is
`rdp_input_handler.cpp`'s `rdpGetWheelDelta()`, which *synthesises* an
Irrlicht wheel event from RDP flags — and that event then hit the same missing
case and was dropped too.

So the "UI-consumed wheel must not also zoom the camera" hazard could not
arise: the static UI is the wheel's **first and sole** consumer. Two things
keep it that way rather than relying on that being remembered:

- `renderer_frame.cpp` reads `getMouseWheelDelta()` in exactly two places —
  the UI route, and an `else` branch that *drains* it when the static UI is
  off, so a wheel spun with `/newui` disabled cannot accumulate and then fire
  a page of scroll the moment the UI comes back.
- `ScrollSourceInvariants.NothingElseConsumesTheWheelAccumulator` fails if a
  third reader is ever added.

### The routing rule

One router, three entry points, in `static_ui_input.cpp`:

```
scrollTargetAt(mx, my, layout, skills, loot)   ->  ScrollTarget
scrollPanel(target, lines, layout, chat, skills, loot)
```

`scrollTargetAt()` uses the panels' own rects, in the order the panels are
**drawn** — which is also the order `processKey()` already dismisses them with
ESC:

| order | target | condition |
|---|---|---|
| 1 | Loot | cursor inside `layout.centerPopup` **and** `loot.isOpen` |
| 2 | Skills | cursor inside `layout.centerPopup` **and** `skills.isOpen` |
| 3 | Chat | cursor inside `layout.chatPanel` |
| — | None | anywhere else (the wheel is then not consumed) |

The whole `chatPanel` scrolls, not just `chatMessages`: the tab strip and the
input field are part of the box the user is pointing at, and the alternative
is a dead ~16px band at each end of it.

`scrollPanel()`'s `lines > 0` **always** means "towards older / earlier
content", and each panel's own convention is honoured underneath rather than
harmonised:

| panel | offset measured from | up (`lines > 0`) is | upper clamp owned by |
|---|---|---|---|
| chat | BOTTOM (`0` = newest line) | `scrollOffset += lines` | `renderChatPanel()` — the only code that knows the wrapped-line count |
| skills | TOP | `scrollOffset -= lines` | `SkillsPopupState::maxScrollOffset(visibleRows)` (new; the render body never clamped, so an unclamped writer would scroll the list off the panel) |
| loot | TOP | `scrollOffset -= lines` | `LootPanelState::maxScrollOffset()` (already existed) |

Entry points: **wheel** (3 lines/notch, accumulated so several notches in one
frame all count), **PageUp/PageDown** (one page), **`/scroll [chat|skills|loot]
[up|down|top|bottom] [lines]`**.

### The keyboard path, and the one behaviour change outside the defect

`KEY_PRIOR` (PageUp) was unbound. `KEY_NEXT` (PageDown) was a **second**
binding for `StrafeRight` (`hotkey_manager.cpp:457` and `config/hotkeys.json`),
so paging a loot list would also have strafed the character. The suppression
point for that — `graphics_input_handler.cpp`'s movement loop — is slot2's
file territory this dispatch, so suppressing it there was not available.

**Deviation, flagged:** `StrafeRight`'s PageDown binding moved to `KEY_HOME`,
pairing with `StrafeLeft`'s existing `KEY_END`; `Ctrl+D` is untouched.
PageUp/PageDown became `HotkeyAction::ScrollUIUp`/`ScrollUIDown` in **Global**
mode (the two scrolling popups are reachable in any mode). This is a
user-visible default-keybind change beyond the literal defect, made because
the alternative was shipping a scroll key that also moves the player. It
matches the real client, where PageUp/PageDown are chat scrollback. Pinned by
`ScrollBindings.StrafeRightMovedOffPageDownSoScrollingDoesNotAlsoMoveThePlayer`.

`/scroll` exists for the same reason `/hideui` does (T301 Leg D): a `--script`
run or a headless harness owns no X server and can generate neither a wheel
nor a key event. Its body lives in `static_ui_input.cpp`, not the renderer's
slash chain, because `irrlicht_renderer.cpp` is already past the file-size
FAIL ceiling. With no panel named it routes by cursor exactly as the wheel
does; `processScrollKey()` additionally falls back to the open popup (then
chat) when the cursor is nowhere, because a `--script` run never moves the
pointer off (0,0) and requiring it inside the popup would make the keyboard
path useless to exactly the harnesses it exists for.

### Per-panel evidence — `tests/test_static_ui_scroll.cpp`, 38 tests, all pass

| leg | panel | how the ROW SET is asserted |
|---|---|---|
| B | **loot** | `hitTestLootRow()` — `renderLootPopup()`'s twin — reports rows 0..7 unscrolled and rows 4..11 after `scrollPanel(-4)`. Plus the before/after pair below. |
| B | **skills** | a click on visible row 0 produces `SkillActivateIntent{0}` before and `{3}` after `scrollPanel(-3)`: the same screen row is a different skill. |
| B | **chat** | `renderChatPanel()` itself, into a real headless GLES2 framebuffer with the real `UIAtlas` (test_ui_atlas_pixels.cpp's harness) — 120 messages, the `chatMessages` rect read back at offset 0 and one page up, asserted **different**. Chat is the only panel whose window cannot be asserted otherwise: the wrapped-line count exists nowhere but inside that function. Self-skips without `willeq.eqe1` or a device. |

The headline pair, `LootScrollTest`:

- `TheDefect_CorpseSlot13IsUnreachableBeforeAnyScroll` — 14 items on the
  corpse, 8 visible rows. Clicking **every** visible row unscrolled produces 8
  `LootItemIntent`s, all with `slot < 8`. Rows 8..13 could not be drawn, could
  not be hit-tested, and were takeable only via Loot All.
- `TheFix_ScrollingBringsSlot13OntoARowAndLootsIt` — two wheel notches down
  (clamped at `maxScrollOffset() == 6`), click the last visible row, get
  `LootItemIntent{corpse 4242, slot 13}`.

Also pinned: the wheel is not consumed over empty screen; a zero wheel is
never consumed; short lists do not scroll; both clamps saturate rather than
run past; `applyLootWindowOpened()`'s reset-to-top still wins over a scrolled
state; scrolling clears the now-stale `hoveredRow`.

### Found on the way, fixed (small, same file, same feature)

`StaticUIInput`'s skills-row hit test looped over a **hard-coded 20 rows**
while `renderSkillsPopup()` only ever draws `contentH / 20` of them (15 at
800x600), and carried its own duplicate copies of the render body's `LINE_H`/
`PAD` constants. A click low in the popup could therefore activate a skill
that was not on screen. Both the constants and the row count now come from one
source (`SkillsPopupState::LINE_H`/`ROW_PAD` and the new
`skillsVisibleRows(layout)`), read by the render loop, the hit test and the
scroll clamp — the buff bar's T277 rule. Pinned by
`SkillsScrollTest.TheRowHitTestNeverReachesPastTheDRAWNWindow`.

### Found and deliberately NOT fixed

- **Chat's scrollOffset is not reset when a new message arrives.** Scrolled
  back in the buffer, an incoming line shifts the whole view by one wrapped
  line because the offset counts lines, not messages. Real clients pin the
  view instead. That is a rendering-policy change inside `renderChatPanel()`,
  not a missing input path, and it needs a golden opinion — out of scope here,
  reported.
- **No scrollbar thumb is draggable.** Chat draws a thumb
  (`static_panels.cpp`); it is decoration, and the loot/skills panels draw a
  "N-M of T" line instead because the static UI has no thumb sprite. Adding
  drag is a new interaction, not the missing writer.
- **The spellbook paginates rather than scrolls** and is deliberately not a
  scroll target: it has `currentPage` and Prev/Next hit zones, which are a
  different, already-working model.
- **`irrlicht_renderer.cpp` remains a file-size FAIL** (3263 lines vs 3237 at
  `fable`; the ceiling is 3000). Pre-existing. The `/scroll` body was moved
  into `static_ui_input.cpp` to keep the growth to 26 lines (the wheel case,
  two hotkey arms, a 7-line command call).

### Sibling territory — read before merging

**Slot2 (T312) is editing four of the same files.** Every edit of mine in them
is small and localised:

| file | my hunks |
|---|---|
| `include/client/graphics/irrlicht_renderer.h` | 2: two `RendererAction` enumerators on the `Screenshot, ToggleAllUI,` line; and **one** hunk carrying `getMouseWheelDelta()` + `mouseWheelDelta_` together (deliberately kept as a single hunk, with a `private:`/`public:` sandwich, rather than putting the member 70 lines away beside `bridgeQueue_` — which is precisely what T312 is rewriting) |
| `src/client/graphics/irrlicht_renderer.cpp` | 3: the `EMIE_MOUSE_WHEEL` case; two `HA::ScrollUI*` arms in the hotkey switch; the 7-line `/scroll` arm |
| `src/client/graphics/renderer_frame.cpp` | 2: the wheel read inside the existing chat-hover block in `processFrameInput()`; the two `RA::ScrollUI*` arms in `processCommonInput()` |
| `src/client/input/graphics_input_handler.cpp`, `src/client/graphics/renderer_player_physics.cpp` | **untouched** |

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **PASS** (EXIT=0) |
| `ctest --test-dir build -j6` | **PASS** — 3361/3362; the sole failure is the documented ALSA host quirk `AudioManagerTest.PreloadCommonSounds`. Zero new failures vs `refactor/test_baseline.txt`. (Suite was 3324 before this unit's 38 tests.) |
| `scripts/build-gles2-x86.sh` | **PASS** (EXIT=0) |
| `scripts/ci/visual_gate.sh` | **PASS**, goldens untouched (`git status` shows no golden path modified) |
| `scripts/build-arm-noble.sh` | **PASS** (EXIT=0) |
| `scripts/refactor/check_file_sizes.sh fable` | 7 ok, 2 warn, 1 fail — **all pre-existing**; no new FAIL, and the failing file grew by 26 lines |

### Live acceptance — PASS, both halves, on a real corpse

eqemu lock taken atomically after slot1's T311 and slot2's T312 released it;
stack started with `scripts/eqemu/up.sh`, shut down with `down.sh`, lock file
deleted. Testchar (GM) in qeynos2, `--eqe1-path /mnt/projects/EverQuest_ETC1`,
daylight forced with `#set time 11 0` first. Media + a README in
**`/mnt/projects/fable/review/T313/`**.

A rat spawned with `#npctypespawn 351043`, given a **28-item** loot list with
`#npcloot add`, and killed. The loot panel shows 8 rows, so **twenty of those
twenty-eight items were unreachable by any click before this unit**.

`live/` — the scripted half (`/scroll`):

| capture | the panel's own "N-M of T" line |
|---|---|
| `live_01_loot_page1_unscrolled.png` | **1-8 of 28** (Cloth Cap … Cloth Sleeves) |
| `live_02_loot_page2_after_scroll.png` | **21-28 of 28** after `/scroll loot bottom`, ending on *Small Cloth Veil* |
| `live_03_after_looting_last_row.png` | chat: **"Looting Small Cloth Veil (slot 35)… --You have looted a Small Cloth Veil--"**, list drops to **20-27 of 27** |

**Corpse slot 35 — the 28th and last row — was looted.** That is the
user-visible proof the plan asked for.

`live/` — the real wheel (Xvfb `:95` + `--egl-x11` + `xdotool click 5` x10,
pointer parked at 400,300 inside the popup, no slash command anywhere):

| capture | |
|---|---|
| `wheel_01_before_wheel.png` | **1-8 of 27** |
| `wheel_02_after_real_mouse_wheel.png` | **20-27 of 27**, with the row under the cursor highlighted — the pointer demonstrably over the panel |

That closes the one hop the unit tests cannot reach: Irrlicht really does
deliver `EMIE_MOUSE_WHEEL` to `OnEvent()` under the X11 EGL device, and the
accumulator really is read and routed on the render thread.

`synthetic/` — the same production render functions driven by the test binary
(`WILLEQ_T313_CAPTURE_DIR=<dir> ./bin/test_static_ui_scroll`), so the reviewed
pixels come from the calls the assertions run on: the loot popup at 1-8 of 14
and 7-14 of 14, and the chat box at offset 0, one page up, and a deliberate
over-scroll showing `renderChatPanel()`'s clamp.

**DB restored to an EMPTY diff** (`db_before.txt` vs `db_after.txt`): general
slots 23-30 and the cursor slot 33 back to their original contents, character
back to zone 2 / (-74, 428, 3.751) / heading 160.

### Live-session traps worth carrying forward

1. **`#gm on` is required before `#kill`, `#damage` and `#npcloot`.** Three runs
   were lost to this. `#npctypespawn` and `#set time` work on account status
   alone, so GM commands *appear* to be working; the target-taking ones
   silently do nothing until the GM flag is on. T290's memory note has the
   `#set gm on` recipe for reaching another player — this is the same switch,
   needed for one's own harmful commands too. Neither the client nor the
   server log says a word when it is off.
2. **T290's cursor trap is still live, and there is a second one.** Testchar
   had `Wooden Fishing Tackle` on the cursor (`inventory.slot_id = 33`) *and*
   all eight general slots (23-30) full of `Scaled Curskin` — with either in
   place, every loot is refused. Both cleared for the session and restored
   after.
3. **An inventory with ZERO rows makes the zone log "Error loading inventory
   for char_id 65 from the database" on every connect.** Clearing slots 23-33
   left the character with no inventory rows at all; putting one row back
   silenced it. Leave at least one item on a test character.
4. `/target` takes a name (`/target a_rat`); bare `/target` is a usage error,
   not target-nearest.

