# T415 — the tradeskill window (B321) and the combat abilities window (B320)

Class B (two panels the official client has and fable lacks; their
payload fields are already published), slot1, no server. Read B321/B320,
T413's §Review (the XML-placement method: `spellbookMemorizeGaugeRect()`),
T393/T409 (`inventory_layout.h`'s scaling from the oracle window size),
the static-panel seam (T54–T56), and the two XMLs:
`/mnt/projects/EverQuestP1999/uifiles/default/EQUI_TradeskillWnd.xml` and
`EQUI_CombatAbilityWnd.xml`.

## Leg A — B321 tradeskill window

On `TradeskillContainerOpenedEvent` draw the window: title from
`containerName`, `slotCount` slots at the XML's slot positions, the
Combine button rect, a Close; `TradeskillContainerClosedEvent` hides it.
Placement scaled from the XML's screen size the way `inventory_layout.h`
does. Slot contents come from the existing container state (T342 wired
combines — read what the container holds and where). Tests pre-fix
failing: open event → the window's rects; the slot rects match the XML
scaled; close hides. Strike B321.

## Leg B — B320 combat abilities window

`EQUI_CombatAbilityWnd.xml`: the ability list and
`CAW_CombatEffectTimeRemainingGauge` (EQType 26). Draw the gauge from
`SkillActivationFeedbackData::cooldownMs` (the field nothing consumed)
and the ability rows from the skills the character has (the skill list
already drives `/skills`, T363). A toggle key: the oracle's default is in
Options > Keys (T377's screenshots under `review/T377/oracle/` — read the
UI page; if the combat abilities key is not on it, `Alt+C` is the Titanium
default per the same table — quote your source; if unquotable, bind
nothing and board the key). Tests pre-fix failing. Strike B320.

## Proof

Build (background + exit file), `ctest -j6`, pre-fix/post-fix, visual
gate standalone + ARM (no golden should move — the gate opens no
windows), file sizes. Live screenshot pairs OWED to the next lock unit.

STOP: the container state for Leg A has no slot contents reachable from
the renderer (report the shape).

## Review

Branch `task/T415-tradeskill-combat-windows`, base `ec4918cb`. Two commits,
one per leg: Leg A is `913455bf`, Leg B is the commit carrying this Review.
Leg A was built and tested on its own (8 tests + the render-order golden,
both green) before Leg B was applied, so the intermediate commit is
buildable, not a half-state. No server was used (T408 holds the eqemu lock);
live screenshot pairs are OWED to the next lock unit.

### Leg A — B321, the tradeskill container window

**The STOP condition did not fire, and the reason is the finding.** The plan
said to stop and report the shape if "the container state for Leg A has no
slot contents reachable from the renderer". They ARE reachable, and no new
event was needed:

| hop | where |
|---|---|
| eqemu sends one `ITEM_PACKET_WORLD_CONTAINER` per occupied cell | `Object::HandleClick` |
| the packet's bag index `i` is mapped to `WORLD_BEGIN + i` (4000..4009) | `src/client/inventory/inventory_packets.cpp:108` (T282) |
| an ENRICHED `InventorySlotChanged` is published for that slot — name, icon, quantity | `src/client/zone/zone_dispatch.cpp:249` (the generic ItemPacket fallback) |
| **the bridge arm dropped it** | `src/client/bridge/irrlicht_bridge.cpp` — the `if/else` covered slot ids 0..29 and had no `else` |

So the contents were crossing the bridge every time a forge opened and going
nowhere. The fix is one `else` branch calling
`applyTradeskillSlotChanged()`, which ignores any id outside the world range.
`ZoneProcessClearObject` publishes the same event with `hasItem = false` for
all ten slots, so the clear path needed nothing either.

**Placement — the oracle's.** `EQUI_TradeskillWnd.xml`'s container half is
ANCHOR-positioned, not absolute, so the absolute rects are derived; the full
derivation is written out in `include/client/graphics/ui/tradeskill_layout.h`
so it can be checked against the XML without redoing it. `AnchorToLeft/ToTop
== false` means the offset is measured from the parent's right/bottom edge.

| XML element | XML Location / Size (or anchors) | absolute in the 400x525 `TradeskillWnd` | fable rect at 800x600 |
|---|---|---|---|
| `<Screen item="TradeskillWnd">` | Location (20,25), **Size 400x525** | — | `centerPopup` = {200,125,600,475} (400x350) |
| `<Screen item="COMBW_RightSideArea">` | L160(from right)/T0/R0(from right)/B0(from bottom) | (240,0)-(400,525) | — |
| `<Screen item="COMBW_ContainerArea">` | L0/T0/R0(from right)/B330 | (240,0)-(400,330) | — |
| `<Screen item="COMBW_ComponentArea">` | L39/T120/R123/B330 | (279,120)-(363,330) = 84x210 | — |
| `<Button item="COMBW_RequiredIcon0">` | Location (0,0), Size 42x42, Decal (2,2) 40x40 | (279,120) 42x42 | {479,205,521,233} |
| `<Button item="COMBW_RequiredIcon1">` | Location (42,0), Size 42x42 | (321,120) 42x42 | {521,205,563,233} |
| `<Button item="COMBW_RequiredIcon2">` | Location (0,42), Size 42x42 | (279,162) 42x42 | {479,233,521,261} |
| `<Button item="COMBW_RequiredIcon8">` | Location (0,168), Size 42x42 | (279,288) 42x42 | {479,317,521,345} |
| `<Button item="COMBW_RequiredIcon9">` | Location (42,168), Size 42x42 | (321,288) 42x42 | {521,317,563,345} |
| `<Label item="COMBW_ContainerName">` | L2/T70/R2(from right)/B90, `<Text>Tradeskill Container</Text>` | (242,70)-(398,90) = 156x20 | {442,171,598,184} |
| `<Button item="COMBW_CombineButton">` | in `COMBW_CombineArea` (240,334)-(400,388): L20/T0/R20/B30, `<Text>Combine</Text>` | (260,334)-(380,364) = 120x30 | {460,347,580,367} |
| `<Button item="COMBW_CloseButton">` | in `COMBW_FlagsArea` (240,392)-(400,525): L20/T24(from bottom)/R20/B2(from bottom), `<Text>Close</Text>` | (260,501)-(380,523) = 120x22 | {460,459,580,473} |

The scale rule is `InventoryLayout::coinRect()`'s exactly: the oracle rect
mapped proportionally from the oracle window onto `centerPopup`, integer
division, in ONE body (`TradeskillLayout::scaled()`), so the draw loop and the
two hit tests cannot drift (T57's rule). Because the oracle window is 525 tall
and `centerPopup` is 350, the 42x42 cells become 42x28 — non-square. That is
what the established rule produces and it is the same compression the purse's
70x24 takes; it is called out here rather than silently "corrected".

**Slot contents / state → panel.** `TradeskillContainerOpened` →
`applyTradeskillContainerOpened()` fills `containerName` (the title),
`isWorldObject` (which combine the button sends), `slotCount` (bounds the
drawn grid, clamped to the oracle's ten) and `objectId`, and clears all ten
cells so a second container cannot show the first one's items. Opening it
closes the other three centre popups, the rule `LootWindowOpened` already
applies. `TradeskillContainerClosed` resets the whole struct.

**The buttons are real, not decorative.** Combine pushes the EXISTING
`TradeskillCombineIntent` (`SLOT_TRADESKILL_EXPERIMENT_COMBINE` for a world
container, the container's own slot otherwise — the same value `/combine`
sends), Close pushes the existing `TradeskillCloseIntent`. Nothing new crosses
the bridge. **The UI never closes itself** (T290's rule): the click sends the
intent and the panel stays up until `TradeskillContainerClosed` comes back, so
an optimistic local close cannot desync `ctx_.active_tradeskill_object_id`
(which also gates `InventoryManager::setWorldContainerOpen`). ESC deliberately
does NOT dismiss this window for the same reason — boarded as **B336**.

### Leg B — B320, the combat abilities window

**The cooldown's path to the gauge.** `EverQuest`'s `setOnSkillActivated`
handler (`src/client/core/everquest.cpp:223`) applies equipment + buff haste
to the skill's base recast and publishes `SkillActivationFeedback` on every
successful activation. The bridge arm discarded the payload; it now calls
`applyCombatAbilityFeedback()`, which stamps `cooldownMs` and a
`steady_clock` start onto `CombatAbilityPanelState`. `gaugeProgress()` is a
TIME-REMAINING fill — 1.0 at activation falling to 0.0 — because that is what
the element is called. A failed activation or a zero cooldown is not an
effect and leaves the gauge alone.

**The ability rows.** A VIEW of `skillsPopupState_.skills`, the list `/skills`
already draws (T363) — no second source of skill data. Filter:
`EQ::getSkillCategory() == Combat` AND `EQ::isActivatableSkill()` AND
`value != 0` (a skill at 0 is one the character does not have), sorted by
skill id, capped at the oracle's eight buttons. Rebuilt from the existing
`SkillsSnapshot` and `SkillValueChanged` arms, so a skill going 0 → non-zero
adds its row. A click pushes the existing `SkillActivateIntent`.

**Placement — the oracle's, UNSCALED.** `CombatAbilityWnd` is
`Style_Sizable false` at 174x150, so unlike the tradeskill window nothing is
proportional and no rect can round.

| XML element | XML Location / Size | fable rect at 800x600 |
|---|---|---|
| `<Screen item="CombatAbilityWnd">` | Location (50,50), **Size 174x150**, `<Text>Combat Abilities</Text>`, Titlebar/Closebox true, Sizable false | {586,42,760,192} |
| `<Button item="CAW_CombatEffectButton">` | (5,4) 20x20, NormalDecal `BuffIcons` | {591,66,611,86} |
| `<Label item="CAW_CombatEffectLabel">` | (28,5) 129x20, `<Text>No Effect</Text>` | {614,67,743,87} |
| `<Gauge item="CAW_CombatEffectTimeRemainingGauge">` | (11,30) 116x10, **`<EQType>26</EQType>`** | {597,92,713,102} |
| `<Button item="CAW_OpenCombatSkillSelectWnd">` | (124,28) 37x14, `<Text>S</Text>` | {710,90,747,104} |
| `<Button item="CAW_Button1">` | (2,45) 40x40 | {588,107,628,147} |
| `<Button item="CAW_Button4">` | (122,45) 40x40 | {708,107,748,147} |
| `<Button item="CAW_Button5">` | (2,85) 40x40 | {588,147,628,187} |
| `<Button item="CAW_Button8">` | (122,85) 40x40 | {708,147,748,187} |

The piece offsets are relative to the window's CLIENT area under its titlebar,
and the arithmetic confirms it: the lowest piece ends at 85+40 = 125 in a
150-tall window, leaving 25px of titlebar. fable's static panels draw their
title inside the panel rect, so `TITLE_H = 20` is that band and the piece
origin is the window origin + (0,20); 20+125 = 145 ≤ 150, the same 5px bottom
margin the official window has.

**DEVIATION, flagged.** The oracle's own `<Location>` is (50,50) — an absolute
top-left screen position. fable's top-left is `UILayout::playerStatus`, so the
window is ANCHORED instead: top-right, under `layout.targetInfo` and **left of
the spell-gem column**. Size and every interior rect are the oracle's; only
the screen anchor is fable's. The gem column, not the screen edge, is the
right anchor because the first attempt (screen edge) put the window's bottom
44px inside the gems at 640x480 — the test caught it at that resolution and
nowhere else, which is why the case checks all five.

**The toggle key: Alt+C, MEASURED, not inferred.** The plan allowed for a
Titanium-default-table fallback; it was not needed.
`review/T377/oracle/T377_oracle_options_keys_UI_page3.png` (and `page4`),
Options > Keys, category UI, reads:

> `Toggle Combat Ability Window        Alt C        -------`

Alternate column empty. Alt+C was free — `ToggleCollision` is Ctrl+Alt+C. The
binding runs `HotkeyAction::ToggleCombatAbilities` →
`RendererAction::ToggleCombatAbilities` →
`FrameComponent::toggleCombatAbilityWindow()`, the same four-hop shape T382's
Alt+P/Alt+B use.

**Default CLOSED, and the evidence for it is an absence.** Unlike the buff and
group windows (T368/B101) there is no shipped default to read:
`/mnt/projects/EverQuestP1999/defaults.ini` has NO `[CombatAbilityWnd]`
section at all (grepped; the file has `[BuffWindow]`, `[GroupWindow]`,
`[PetInfoWindow]`, `[SelectorWindow]` and 35 others), so there is no `Show=`
either way, and T377's in-world captures show no such window open. A window
fable cannot prove ships open, ships closed.

### Verification

| step | result |
|---|---|
| **pre-fix red** | production reverse-applied via exported patch (`git apply -R`, never `git stash`) + the six new production files held aside; `cmake --build --target test_tradeskill_combat_panels` → **exit 2**, `test_tradeskill_combat_panels.cpp:24:10: fatal error: client/graphics/ui/tradeskill_layout.h: No such file or directory` |
| **build** (`build`, RelWithDebInfo, GLES2 config) | exit **0** (background + exit file) |
| **post-fix test** | `test_tradeskill_combat_panels` **16/16 PASSED** (8 tradeskill, 8 combat abilities) |
| **render-order golden** | `test_frame_render_order` **4/4 PASSED** after teaching the golden in place |
| **ctest -j6** (first run) | **4278 / 4279**, one failure: `DocCounts` — `src/client/input/CLAUDE.md` stated 94 `HotkeyAction` enumerators and the tree now has 95. That is the doc-count net doing its job on the new `ToggleCombatAbilities` enumerator. No other red. |
| **ctest -j6** (final state, after the doc fix and the 640x480 anchor fix) | **4279 / 4279, exit 0.** Zero failures, no waivers. |
| **visual gate** (standalone, after `build-gles2-x86.sh` exit 0) | **PASS** — qeynos2 p95 4.492 (ceiling 10.108), gfaydark 4.904 (10.672), befallen 5.978 (10.948). **No image golden moved** (`git status tests/goldens/` shows only `frame_render_order_g05.txt`, the learned text golden). |
| **ARM** (`build-arm-noble.sh`) | exit **0**, `build-arm-noble/bin/willeq` = ELF 32-bit ARM EABI5 |
| **file sizes** | `check_file_sizes.sh fable`: 11 changed files, **11 ok / 0 warn / 0 fail**. The six new production files are 125–174 lines, the new test 497 — all far under the 1,500 ceiling. New code went in NEW TUs: `static_panels.cpp` was already 1,099 lines. |

**The render-order golden was taught IN PLACE, not regenerated** (T337's rule,
batch-G correction 32). Five entries added, 304 → 309, at two named insertion
points: entries 257-259 are the three lines of
`staticUIInput_->processPanelInput(...)`, inserted directly after entry 256
(the last line of the existing `processInput()` call) and before
`pendingUIClick_ = false;`; entries 278 and 279 are `renderTradeskillPopup`
and `renderCombatAbilityPanel`, inserted directly after
`renderLootPopup(...)`. Nothing was removed, replaced or reordered, and
`test_frame_render_order.cpp`'s size comment gained the paragraph that says so.

### New board items

- **B335** — the tradeskill window's whole LEFT side (recipe search, recipe
  list, favourites) is not drawn, so the left 60% of the popup is blank.
  **Checked, not assumed:** the four opcodes already exist and are already
  sent (`HC_OP_RecipesFavorite` / `RecipesSearch` / `RecipeDetails` /
  `RecipeAutoCombine`, `merchant_vendor_bank.cpp:547/559/581/593`). What is
  missing is the CONSUMER: `ZoneProcessRecipeReply` parses the recipe name, id
  and trivial and its whole body is a single `LOG_DEBUG` — the T413 shape one
  layer up, a server list arriving and being thrown away. Also drawn-but-inert
  and named there: `COMBW_ExperimentButton`, `COMBW_AutoInvButton`,
  `COMBW_UseCtrlCheckbox`, `COMBW_ContainerIcon` (no item icon sheet in
  fable's atlas), `CAW_OpenCombatSkillSelectWnd` (no combat skill select
  window).
- **B336** — ESC does not dismiss the tradeskill container window, and must
  not be made to naively: it has to push `TradeskillCloseIntent` and wait, the
  way the Close button does. `processKey()` has no pattern for a
  bridge-routed dismissal, and the loot popup has the same latent question.

### Surprises

1. **The container contents were already on the renderer's doorstep.** The
   STOP condition assumed a missing producer; the producer was the generic
   ItemPacket fallback, fully enriched, and the consumer's `if/else` simply
   had no `else`. One branch closed the half of B321 that looked like the
   hard half.
2. **B335's first draft was WRONG and measuring it caught that.** The entry
   originally said the recipe opcodes were not in `TitaniumZoneOpcodes`; a
   grep found all four already sent, with a `LOG_DEBUG`-only reply handler.
   The board entry now says what is actually missing.
3. **The 640x480 collision.** The screen-edge anchor for the combat abilities
   window looked obviously fine at 800x600 and was wrong at 640x480 — the
   spell-gem column climbs as the screen shrinks. Only the all-five-resolutions
   case found it.
4. **The tradeskill window's aspect.** Mapping a 400x525 oracle window onto a
   400x350 popup squashes the 42x42 container cells to 42x28. That is the
   established rule's output, not a bug, but it is the first T393-style
   mapping where the oracle window is TALLER than `centerPopup` and it shows.
