# B1025-H4: hotbar page keys and the window toggle, per the official defaults

Source: Will 2026-10-13; chain H-4 of the SCOPE note on #345. Consumes H-O
(the measured defaults, `pages.tsv`) and H-1/H-2 (the model and the window).
Scope amendment per the 2026-10-13 orchestrator note on #345: the stock
client cannot show Hotbars 2-4, so the model is ONE window x 10 pages x 10
buttons -- this unit adds that one window's page keys and its toggle, not a
4-window key matrix.

## The bindings (ONE table: setupDefaults + the shipped file)

H-O measured the official defaults on the stock client (kb-h0-hotbar-oracle
`pages.tsv:2`, still `27_keys_hb1.png`; eqstr 6562-6571 "Hot button page
1-10", eqstr 6587 "Toggle Hotbar 1 window"):

| Action (enum name, Options Keys row) | Default | Oracle source |
|---|---|---|
| HotbarSlot1..10 (unchanged, "Hotbar Slot 1-10") | 1..9, 0 | pages.tsv:2 |
| HotbarPage1..10 ("Hotbar Page 1-10") | Shift+1 .. Shift+9, Shift+0 | pages.tsv:2, 27_keys_hb1.png |
| ToggleHotbar1Window ("Toggle Hotbar 1 Window") | Alt+H | pages.tsv:2, 27_keys_hb1.png |

Hotbars 2-4 are UNBOUND in the official client (`pages.tsv:3-6`, stills
`28_keys_hb2.png` / `26_keys_hb1.png` / `29_keys_hb4.png`), and the stock
client shows no second window (the XML gives HotButtonWnd2..4 zero size;
H-O measured them invisible even forced on, `pages.tsv:11`), so there are no
`HotbarPage2..4`/`ToggleHotbar2..4` family actions: one toggle for the one
window, no dead code for bars 2-4.

The bindings live in the ONE place the rule already demands
(`src/client/input/CLAUDE.md`: a default binding must be in `setupDefaults()`
AND `config/hotkeys.json`, T464 pins the two tables equal binding by
binding):

- `setupDefaults()` -- `src/client/input/hotkey_manager.cpp:615-630`
- name table -- `src/client/input/hotkey_manager.cpp:222-232`
- shipped file -- `config/hotkeys.json` ("player" section, after
  `HotbarSlot10`)
- `HotkeyAction` enumerators -- `include/client/input/hotkey_manager.h:155-175`
  (the enum count is 95 -> 106, re-counted here; DocCounts row, see the
  CLAUDE.md EDITS below)

No second table exists: the Options Keys page lists these rows off the
`HotkeyAction` table (below), the receiver switch resolves them through the
same singleton, and the census
(`tests/test_hotkey_dispatch_census.cpp`) presses them over both the
defaults and the shipped file.

## Routing

Keyboard activation is ADDRESSED by the game thread's one `HotbarModel`
(acceptance C):

- **Slot keys (1-0)**: the receiver latches the 0-based slot index on
  `hotbarActivationRequest_` as before (`renderer_event_receiver.cpp:287-296`);
  the ONE consumer, `GraphicsInputHandler::consumeHotbarRequest()`, feeds
  `InputActionBridge::processHotbarRequests()`
  (`src/client/action/input_action_bridge.cpp:292-348`), which reads the
  slot from `m_hotbarModel->getSlot({m_hotbarModel->currentPage(), slot})`
  -- i.e. slot N of the MODEL'S current page. The page is not carried in
  the request: there is one source of truth, and the panel's
  `currentPage` follows the model's own republish
  (`applyHotbarSlotAssigned`), so slot and page cannot disagree.
- **Page keys (Shift+1..0)**: `renderer_event_receiver.cpp:301-320` queues
  `RendererAction::HotbarPageChange` with the 0-9 page in `intData` on the
  RENDERER action queue; its consumer,
  `FrameComponent::processPlayerInput()` (`renderer_frame_input.cpp:418-430`),
  clamps to `HotbarModel::PAGE_COUNT` and pushes the SAME
  `HotbarPageChangedIntent` the window's page buttons push
  (`static_ui_input.cpp:101-111`) -- one consumer,
  `BridgeComponent::onHotbarPageChangedIntent()`
  (`bridge_component_intents.cpp:806`) -> `HotbarModel::setCurrentPage()`,
  which republishes the page and the panel redraws.
- **The toggle (Alt+H)**: `RendererAction::ToggleHotbar1Window`
  (`renderer_event_receiver.cpp:322-325`), consumed by
  `FrameComponent::processPlayerInput()`
  (`renderer_frame_input.cpp:415-416`) via
  `FrameComponent::toggleHotbar1Window()`
  (`renderer_frame_input.cpp:341-350`) -- the same flip the panel's close
  box does on click (`static_ui_input.cpp`'s `HotbarHit::Close`), on
  `hotbarState_.isOpen`. No new intent: the window state is renderer-side
  (T432: nothing user-changeable is persisted, and the oracle offers no
  other UI to hide this window).

Both new `RendererAction`s are in `renderer_types.h:136-137`; the two
switches over `RendererAction` (`processCommonInput`,
`processPlayerInput`) have the arms, and the census's "every default and
shipped binding reaches a dispatch consumer" now covers the eleven
presses (the page keys read as a renderer action, Alt+H as a renderer
action too -- no bridge-queue entry, so no game-thread shadowing).

## Options Keys rows

The eleven actions join the existing "Hot Buttons" category in
`options_keys.cpp` (`table()`, after `HotbarSlot10`): that category is
21 rows (10 buttons + 10 pages + 1 toggle = the oracle's "Hotbar 1" page),
which fits the list's 22 visible rows at 800x600
(`OKP_KeyboardAssignmentList` 377x337, `LIST_ROW_H` 14, `LIST_HEADER_H` 16
-- options_layout.h:290,439-446). `OptionsKeys.EveryActionIsInExactlyOne-
CategoryAndEveryCategoryFits` keeps the whole enum covered exactly once, so
a future row added to the enum without a category is a red test;
`TheCategoryComboCyclesAndTheListShowsTheBindings` and the new
`HotbarKeyRoutingTest.TheOptionsKeysPageListsThePageAndToggleRows` pin the
rows and their shipped binding text.

## Tests (acceptance C + B + D)

New TU `tests/test_hotbar_key_routing.cpp` (willeq_core, device-free):

- `PageKeysCarryTheirPageOnTheRendererActionQueue` -- a real
  `RendererEventReceiver` with the SHIPPED `config/hotkeys.json` (the exact
  load sequence the client runs): each of Shift+1..Shift+0 leaves exactly
  one `HotbarPageChange` with the right 0-9 page, and no slot request.
- `AltHTogglesTheHotbarWindowOnTheRendererActionQueue` -- LMENU down + H
  leaves exactly `ToggleHotbar1Window` on the renderer action queue, the
  bridge queue stays empty.
- `PlainDigitKeysStillActivateSlotsOfTheCurrentPage` -- the unchanged slot
  latch (0-based request, no renderer action).
- `ThePageActionPushesTheSameIntentThePageButtonsPush` -- through the
  production consumer `FrameComponent::processPlayerInput` on a bare
  renderer: page 2 and page 9 go out as `HotbarPageChangedIntent`; 10 and
  -1 are clamped away. (Page-3 keyboard activation firing the page-3 slot:
  the model's page read in `processHotbarRequests` is exactly what
  `test_skill_reachability`'s `PageAddressPublishesActivePage` and
  `test_input_action_bridge`'s T174 route tests already pin at the model
  and the bridge respectively; this test closes the key -> model half.)
- `TheToggleActionFlipsTheHotbarWindowOnAndOff` -- the flip, through the
  same consumer, with nothing beside the flag disturbed.
- `TheOptionsKeysPageListsThePageAndToggleRows` -- all eleven rows in the
  the "Hot Buttons" category with their shipped binding text, and the row
  wording derived from the enum names by the standing camelCase-split rule.

The binding-table round-trip (acceptance B) is the standing
`HotkeyShippedFileTest.T464_TheShippedFileCarriesEveryDefaultBinding` over
the now-larger tables, plus the census over both tables. No golden changed:
the touched TUs are not named by `tests/goldens/*` (frame_render_order is
the renderer's `renderHotbar`, untouched; the page/toggle arms live in
`processPlayerInput`'s action switch, whose order is not goldened).

## Not verified / left open

- No live in-world run of the new keys (that leg is H-5, the CI feature
  scenario, against a lane). Everything here is the offline chain: key ->
  receiver -> consumer -> intent/model/panel, each half behaviourally
  tested on real objects.
- The oracle stills (`27_keys_hb1.png` etc.) live in
  `/home/user/kb-evidence/kb-h0-hotbar-oracle/` on the hermes-ext host (the
  lane-side copy MR !165 carries is the H-O unit's evidence); `pages.tsv`
  is the cited record. The stills themselves were not re-opened for this
  unit -- the binding table quotes `pages.tsv`'s recorded observations.
- `config/hotkeys.json` is now 11 rows larger; a pre-H-4 user copy in the
  cache dir simply lacks them (it loads fine -- `loadFromFile` replaces the
  table with what the file has, so a user copy predating this loses the new
  rows until Reload defaults; same behaviour as every earlier row addition,
  e.g. B582's volume deletion, `B582_APreB582UserFileStillLoads`).

## Acceptance

| Criterion | Status | Evidence |
|---|---|---|
| (A) Official defaults, still cited | Met | Table above: `pages.tsv:2` + `27_keys_hb1.png` (H-O), eqstr 6562-6571/6587; Hotbars 2-4 unbound per `pages.tsv:3-6` |
| (B) One bindings table | Met | `setupDefaults()` + `config/hotkeys.json` only (the standing T464 rule); no second table anywhere -- the Options page, the receiver and the census all resolve off the same `HotkeyAction` singleton |
| (C) Addressed activation tested | Met | `test_hotbar_key_routing.cpp` (key -> receiver -> consumer -> intent/model/panel) + the standing model/bridge tests; slot keys read the model's current page (`input_action_bridge.cpp:310`) |
| (D) DocCounts green | Met | `scripts/refactor/check_doc_counts.py` at 0 failures after the HotkeyAction count update below |
| (E) CI green | Met by the MR pipeline | See the completion summary (pushed with `-o ci.skip`) |

## CLAUDE.md EDITS

The DocCounts "HotkeyAction enumerators" row reads the bold count in
`src/client/input/CLAUDE.md` and re-measures the enum. The enum went from
95 to 106 enumerators (ten `HotbarPage1..10` + `ToggleHotbar1Window`).
Exact replacement (one line, `src/client/input/CLAUDE.md` line 24):

OLD:

    - `HotkeyAction` enum (**95** actions plus a `Count` sentinel, re-counted UIGAP-small -- T471 read 102, T461 99, T415 95, T371 94; B905 (#225) DELETED eight Global actions that were bound and had no consumer ...

NEW (the whole sentence, with the count and its re-count history updated):

    - `HotkeyAction` enum (**106** actions plus a `Count` sentinel, re-counted B1025-H4 -- B1025-H4 read 106 (ten `HotbarPage1..10` + `ToggleHotbar1Window`, the official page keys and window toggle, H-O's measured defaults), T471 102, T461 99, T415 95, T371 94; B905 (#225) DELETED eight Global actions that were bound and had no consumer (`ToggleUILock`, `SaveUILayout`, `ResetUIDefaults`, `ConfirmDialog`, `CancelDialog`, `SubmitInput`, `CancelInput`, `ChatAutocomplete`): none is on the oracle's Options > Keys pages and, Global, they shadowed Tab (CycleTargets), Escape and Return. `tests/test_hotkey_dispatch_census.cpp` presses every default AND shipped binding on a real `RendererEventReceiver` and fails on a shadowed binding or one with no consumer; the key table grows with every UI panel, T461 added the four camera-orbit actions, T471 `CameraRaise`/`CameraLower`, binding all six to the official Camera-page keys T466 measured, and B1025-H4 the hotbar page/toggle keys, binding all eleven to the official Hotbar-1-page keys H-O measured) covering movement, toggles, targeting, interaction

(Only the `**95**` -> `**106**` and the two re-count/append clauses change;
the rest of the sentence is verbatim. The checker's regex is
`` `HotkeyAction`\s+enum\s+\(\*(\w+)\*\` `` -- any wording works, the
number must be 106.)

## FOLLOW-UP

- H-5 (the CI feature scenario) should drive Shift+<digit> and Alt+H in the
  scenario lane to close the live leg.
- `include/client/graphics/ui/CLAUDE.md`'s "Keys" page description does not
  enumerate the category rows, so it needs no edit; if it ever starts
  quoting a category's row count, this unit's eleven rows belong to
  "Hot Buttons" (21 rows at 800x600, fits the 22 visible).
- The `RendererEvent::intData` WriteOnlyFields exemption (B346,
  `scripts/refactor/check_write_only_fields.py`) is REMOVED: this unit's
  `HotbarPageChange` arm is that field's first production consumer (page
  index in `intData`). `refactor/BOARD.md`'s B346 row is struck for the field.
