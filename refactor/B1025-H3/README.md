# B1025-H3: hotbar assignment gestures -- hold-to-pickup, place, pickup, /hotbutton

Source: Will 2026-10-13 SCOPE note on #345 (GitLab issue #345) plus the H-O
oracle (`refactor/B1025-H0/README.md`, the evidence ledger at
`/home/user/kb-evidence/kb-h0-hotbar-oracle/`). Consumes H-1 (the paged
`HotbarModel`), H-2 (the stock window's slot rects,
`include/client/graphics/ui/hotbar_layout.h`).

## What this unit is

THE GESTURE IS PRESS-AND-HOLD, THEN CLICK (H-O). There is no drag-and-drop
anywhere in fable and this adds none. A press on a holdable source that stays
down and on-source crossing `kHotbarHoldThresholdMs` (300 ms, hotbar_assignment.h:57)
clones the source as a HOT BUTTON onto the cursor; the source's normal action
is DEFERRED into `HotButtonHolder` (hotbutton_holder.h) so a release before the
threshold still fires it (the short-press cast) and a crossing cancels it. The
button is placed by a SEPARATE click on a hotbar slot
(`HotbarAssignIntent` -> `BridgeComponent::onHotbarAssignIntent` ->
`HotbarModel::assignSlot()` -- the first production caller, the zero-caller
seam row struck in `scripts/refactor/check_zero_callers.py`).

### The sources (H-O's measured clone set)

| Source | Kind | H-O row | Notes |
|---|---|---|---|
| Occupied hotbar slot | `Slot` | `gestures.tsv:2` "hotbar-pickup" | the clone ALSO clears the source slot (`HotbarClearIntent` -> `HotbarModel::clearSlot()`, its first production caller) |
| Memorised spell gem | `Gem` | `gestures.tsv:3` "spell-hold" | a 0.117 s press cast with no clone; a 3.074 s hold cloned by ~1.1 s |
| Combat-ability row | `Ability` | `gestures.tsv` "ability-hold" | the clone is the row's skill (Combat slot type) |
| Main-inventory item | `Item` | `gestures.tsv` "inventory-hold" | 10-fps-bounded onset 0.72-1.02 s; the clone carries the item id (the game thread resolves id -> slot via `findItemSlotByItemId`) |

`recomputeHoldSource()` (static_ui_input.cpp) derives the source each frame
from the PREVIOUS frame's hover, in hit-test order: hotbar window, gem,
inventory, ability rows. Bag items are NOT a source (H-O "bag-item-hold"
produced no clone in its session), and the direct Skills-window row hold is
NOT a source (H-O measured no clone on a 4.5 s row press).

### The threshold

`kHotbarHoldThresholdMs = 300` sits INSIDE H-O's measured bounds (past the
0.117 s short press, well before the 0.72 s earliest measured onset): the
middle of the bound, not a claimed millisecond -- the oracle's exact value is
still open (H-O "Not captured").

## Files

| File | What |
|---|---|
| `include/client/graphics/ui/hotbar_assignment.h` | the gesture's declarations + the H-O citations (the hold timer, the displacement, the per-frame step) |
| `src/client/graphics/ui/hotbar_assignment.cpp` | `tickHotbarHold`, `stepHotbarGesture`, `applyHotbarAssignPending`, `applyHotbarPlacementDisplacement`, `renderHotbarCursorAttachment` (the CA_SpellGem piece's producer -- B440's "no producer" is struck) |
| `include/client/graphics/ui/hotbutton_holder.h` | `HotButtonHolder` -- the deferred source action, pure and testable |
| `include/client/graphics/ui/cursor_attachment.h` | `hasHotbutton`/`hotbutton` (`HotButtonCarried`), the hold timer's fields, `carriedItemId` |
| `src/client/graphics/ui/static_ui_input.cpp` | the press arms (slot / gem deferral), `recomputeHoldSource`, `processFrameGesture`, the placement/pickup on the slot click |
| `src/client/graphics/frame/renderer_frame_cursor.cpp` | the WORLD click's dismissal (H-O "hotbar-worlddrop") |
| `src/client/graphics/ui/cursor_attachment.cpp` | the hot button's draw (above the item/coin) |
| `include/client/events/renderer_intents.h` | `HotbarAssignIntent` (the placement), `HotbarClearIntent` (the pickup) |
| `include/client/state/event_types.h` / `event_payloads_ui.h` | `HotbarAssignPending` (the /hotbutton command's button -> cursor) |
| `src/client/bridge/irrlicht_bridge.cpp` | the `HotbarAssignPending` arm; `slot.itemId` population (equip + general) |
| `include/client/bridge/bridge_component.h` / `src/client/bridge/bridge_component_intents.cpp` | `onHotbarAssignIntent`, `onHotbarClearIntent`, `PublishHotbarAssignPending` |
| `include/client/action/action_handler.h` / `action_dispatcher.h` / `action_dispatcher_social.cpp` / `eq_action_handler.h` / `eq_action_handler.cpp` | the `/hotbutton` chain: `IActionHandler::createHotbutton` -> `ActionDispatcher::createHotbutton` -> `EqActionHandler::createHotbutton` -> `PublishHotbarAssignPending` |
| `src/client/action/command_processor_registration.cpp` / `command_processor_social.cpp` | `/hotbutton [Name] <Color> [Text]` registered beside `/useskill`; the parse (eqstr 13278 usage, 13283 error, 13282 success) |
| `include/client/hotbar/hotbar_model.h` | the `assignSlot`/`clearSlot` doc (production callers now exist) |

## /hotbutton

Syntax `/hotbutton [Name] <Color> [Text]` (eqstr 13278-13283, quoted from
`/mnt/projects/EverQuestP1999/eqstr_us.txt`): the oracle's session form is
`/hotbutton H0 1 /say hi`. The macro is the button's TEXT (the Emote's
emoteText, what `ActivateHotbarCommandSlot` runs when the slot is pressed);
the NAME rides the button's name; the COLOR is accepted-and-ignored (the
model carries no colour field -- a no-op by construction, not an invented
refusal). The button lands on the cursor through the T239/T230 four-layer
shape (`CommandProcessor` -> `ActionDispatcher` -> `IActionHandler` ->
`BridgeComponent::PublishHotbarAssignPending` -> bridge event -> the
render thread's `applyHotbarAssignPending`), NOT the state EventBus.

## Tests

- `tests/test_hotbar_assignment.cpp` (NEW target): the pure state machine --
  the hold timer (threshold under/over, move-off reset, no-source,
  existing-clone, item displacement), the deferred-action holder (fire /
  cancel / move-off), the pickup clear (slot vs gem sources), the /hotbutton
  button's landing + replacement, the displacement (empty / occupied /
  out-of-range), and the end-to-end press loops through the REAL
  `StaticUIInput` (press-release-under-threshold activates; hold clones AND
  clears the source; a release KEEPS the clone; placement assigns and
  displaces).
- `tests/test_command_processor_hotbutton.cpp` (new source of the existing
  `test_command_processor` target): the command's registration, the oracle
  form, usage, colour/text refusals, the 60-char limit, the headless and
  not-zoned refusals.
- `tests/test_bridge_component.cpp`: `HotbarAssignIntent` assigns the model
  slot (the production caller's proof), `HotbarClearIntent` clears it,
  `PublishHotbarAssignPending` pushes the event / refuses headless.
- The reachability sweep (`tests/test_reachability_sweep.cpp`) covers the new
  event and both intents: `HotbarAssignPending` is published
  (`BridgeComponent::PublishHotbarAssignPending`) and consumed
  (`IrrlichtBridge`'s arm); `HotbarAssignIntent`/`HotbarClearIntent` have
  consumer arms (`onHotbarAssignIntent`/`onHotbarClearIntent`) and production
  producers (`StaticUIInput`'s placement / pickup), so neither enters
  `kBlockedIntents`.
- `tests/goldens/command_registration_order_g03.txt`: 156 -> 157, `hotbutton`
  at 029 after `useskill` (the learned-edit golden).

## Acceptance

| Criterion | Status | Evidence |
|---|---|---|
| (A) every H-O gesture row implemented or listed | Met | table above: slot/gem/ability/item implemented with the ledger rows; bag-item-hold and skills-row-direct-hold listed as NOT sources with their rows; the Skills-window Make-Hotkey route is Not implemented (below) |
| (B) assignSlot has a production caller through a user gesture | Met | `BridgeComponent::onHotbarAssignIntent` (bridge_component_intents.cpp), reached by `StaticUIInput`'s placement; `test_hotbar_assignment` `HotbarGestureEndToEndTest.PlacementAssignsAndDisplaces` + `test_bridge_component` `HotbarAssignIntentAssignsTheModelSlot` prove the path |
| (C) /hotbutton works | Met | the four-layer chain above; `test_command_processor_hotbutton` (parse + handler) and `test_bridge_component` `PublishHotbarAssignPendingPushesTheEvent` |
| (D) no drag-and-drop abstraction added | Met | the gesture is hold-then-click; `grep` for a drag type over the new files returns nothing |
| (E) CI green | In flight | pushed with `-o ci.skip`; the MR pipeline is the one that counts |
| (F) no bless-only file touched | Met | no `tests/goldens/*` except the learned-edit `command_registration_order_g03.txt` (a LEARNED-EDIT golden by its own test's design, re-taken for the new command); no budget/baseline/ratchet value, no constrained_presets.json, no third_party/ |

### Frame-render-order golden

The per-frame tick (`processFrameGesture`) lives on `StaticUIInput`
(static_ui_input.cpp), called from `renderer_frame_input.cpp` -- NOT from a
`FrameComponent` method. `tests/goldens/frame_render_order_g05.txt` parses
`renderer_frame_render.cpp` only, and `processCursorGestureFrame` (the world
dismissal's home) is at its line 298 unchanged.

## Not implemented

- **The Skills-window Make-Hotkey route.** H-O's "skills-row-direct-hold" row
  found no clone on a 4.5 s row press; the oracle's supported route there is
  the Make Hotkey control of the skills window, which fable has not built. A
  direct row hold is deliberately NOT a source (see the table), and no route
  is invented to match a gesture the oracle does not show.
- **CA_Anim2.** H-O's cursor shows CA_SpellGem for a spell and CA_Anim for an
  item; the hot button draws CA_SpellGem's own box (hotbar_assignment.cpp's
  `renderHotbarCursorAttachment`) and an item button reuses the dragitem decal
  (T430's reuse). CA_Anim2 (the second item piece) is not used by this
  gesture -- the item button is one decal, as the oracle's frames show.
- **Right-click on a hotbar slot.** H-O recorded no right-click
  de-assignment; a right click is the item display (B442's convention) and
  this unit does not add a second right-click behaviour to the slot.
- **The colour field.** Accepted-and-ignored (the model carries no colour);
  the oracle's INI colour is not persisted.

## Not verified / left open

- The exact threshold. 300 ms is the middle of H-O's bound (the oracle's
  exact value is "Not captured"); a Pi live A/B against the measured onsets
  is open, as is the live feature scenario (`--scenario` for the gesture).
- The letter-code taxonomy (the INI's `E12`/`F22` codes the oracle's
  placements produced; H-O's acceptance-table leaves it unestablished).

## CLAUDE.md EDITS

`check_doc_counts.py` (the `DocCounts` hygiene test) fails on this branch until
these land. The measured tree: **108** `GameEventType` enumerators (107 +
`HotbarAssignPending`), **52** `RendererIntent` variants (49 + H2's
`HotbarPageChangedIntent` + `HotbarAssignIntent` + `HotbarClearIntent`),
**157** `registerCommand({` calls (156 + `/hotbutton`). H2's pending
`49 → 50` intent edit is subsumed by the 52 below.

- `CLAUDE.md:14` — `EventBus, 107 event types` → `EventBus, 108 event types`
- `CLAUDE.md:735` — `(switch over the 107 \`GameEventType\` enumerators;` →
  `(switch over the 108 \`GameEventType\` enumerators;`
- `CLAUDE.md:741` — `— 107 event types covering all game state changes` →
  `— 108 event types covering all game state changes`
- `include/client/state/CLAUDE.md:54` — both `107`s on that one line
  (`**107 event types**` and `a case for all 107 in its switch`) → `108`;
  add `B1025-H3 added \`HotbarAssignPending\`` to the added-events list
- `include/client/bridge/CLAUDE.md:46` — `## Intent Types (49)` →
  `## Intent Types (52)` (note `HotbarAssignIntent` / `HotbarClearIntent`,
  B1025-H3)
- `src/client/input/CLAUDE.md:40` — both `**156**`s on that line → `**157**`
- `include/client/graphics/ui/CLAUDE.md:443` — the `**156**
  \`registerCommand({...})\` calls` (line 444's `**156**` too) → `**157**`
