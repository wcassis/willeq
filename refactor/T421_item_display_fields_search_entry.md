# T421 — the item display shows what it parses (B298, item half); the search box types (B365)

Class B ×2, slot3, no server. Read B298 (21 `ItemInstance` fields parsed
and never shown; the `SpellData`/`SkillData`/entity halves are separate —
board them as their own follow-ups with the census rows), B365, the
item display popup in `static_panels.cpp` (what it shows today), the
oracle `EQUI_ItemDisplay.xml` (+ `EQUI_Inspect.xml` if relevant), T413/T415's
XML-placement method, and the chat text-entry code (`ChatPanelState`,
the key routing T357/T363 documented) for Leg B.

## Leg A — B298 item fields

The official item display window lists the stats an item carries; fable
parses 21 of them and shows none. Table: field → the XML element/label
that shows it on the oracle (quote `EQUI_ItemDisplay.xml`; where the
window is free text, quote the official client's item-display text
format from an oracle screenshot under `review/T3xx/oracle/` if one
shows an item, else say "text layout unquotable" and follow the XML
element order). Draw every field the oracle shows, in its place;
fields the oracle does NOT show stay hidden (say which). Tests pre-fix
failing per field (static-panel seam: the text entries). Strike the
item half of B298; annotate the rest.

## Leg B — B365

The tradeskill search box needs text entry. Reuse the chat panel's
entry mechanism (focus + key routing + caret; T357/T363 documented the
focus gate) as a shared `TextEntryState` the chat AND the search box
use — do not write a second entry widget. Enter or the Search button
pushes the existing `SendRecipesSearch` intent with the text and the
trivial bounds; the favourites buttons push their existing senders.
`TradeskillButton` gains the enumerators (the hit test returns them).
Tests pre-fix failing: typing into the focused search box changes its
text; Enter publishes the search intent with that text; the chat entry
is unchanged (its existing tests stay green). Strike B365.

## Proof

Build (background), `ctest -j6` (expected red only if T420 (slot1) has
replaced the pack: `PackReproducible`/`eqe1MappedBytes`), pre-fix/post-fix,
visual gate standalone + ARM (no image golden should move), file sizes;
`ui_panel_capture` for the item display and the tradeskill window into
`review/T421/`.

STOP: none expected.

## Review

Branch `task/T421-item-display-search-entry`, base `fable` at **`8f7e718c`**
(verified at checkout). Slot 3, `build/` RelWithDebInfo + GLES2, no server —
T418 held the eqemu lock and nothing here opened a socket (B306).

### Leg A — B298, the item half

**There was no item display window in the tree at all.** The plan's phrasing
("the item display popup in `static_panels.cpp` (what it shows today)")
assumed one existed; `grep -rn 'ItemInstance' src/client/graphics
include/client/graphics` returns **nothing**, and no panel, no event and no
intent named an item's stats. So Leg A is a new window rather than fields
added to an old one.

**The oracle, and what it does and does not pin.**
`/mnt/projects/EverQuestP1999/uifiles/default/EQUI_ItemDisplay.xml` is 81
lines: a 400x190 `<Screen item="ItemDisplayWindow">` with
`RelativePosition false`, `<Location>(0,80)</Location>`, `Style_Titlebar`,
`Style_Closebox`, holding exactly two children —

    <STMLbox item="IDW_ItemDescription">   0 / 0 / 0(right) / 3(bottom)
        <Style_VScroll>true</Style_VScroll>
    <Button item="IDW_IconButton">         70 / 12 / 26 / 56, anchored RIGHT
        <DecalOffset>(2,2)</DecalOffset>  <DecalSize>40x40</DecalSize>

So the XML pins the window's SHAPE and says nothing whatever about which stats
go in it: the body is one free-text box the client composes. The plan
anticipated this ("where the window is free text ... else say text layout
unquotable"). **It is not unquotable.** The line FORMATS are shipped, in the
official client's own string table
`/mnt/projects/EverQuestP1999/eqstr_us.txt`, and every format the composer
emits is quoted from it with its id.

**What is NOT the oracle's**, flagged rather than dressed up (the T419
`ORACLE_ROW_H` rule): the ORDER of the lines and which share a row (the string
table is a flat id→format map; the order used is the table's own id order
within each group), and the TITLE BAR HEIGHT of 18 (the XML says
`Style_Titlebar true` and nothing more).

#### The field → oracle-element table

Every row is `ItemInstance` → the eqstr id and its verbatim format. The
window element is `IDW_ItemDescription` for all of them except the name and
the icon; B298's own item census is the first block.

| field | eqstr id | format, verbatim | element |
|---|---|---|---|
| `accuracy` | 9043 | `Accuracy: %1` | IDW_ItemDescription |
| `damageShield` | 5847 | `Damage Shield: %1` | IDW_ItemDescription |
| `dotShield` | 5836 | `DoT Shielding: %1%` | IDW_ItemDescription |
| `spellShield` | 9041 | `Spell Shield: %1%` | IDW_ItemDescription |
| `strikethrough` | 9045 | `Strikethrough: %1%` | IDW_ItemDescription |
| `stunResist` | 9044 | `Stun Resist: %1%` | IDW_ItemDescription |
| `shielding` | 9040 | `Shielding: %1%` | IDW_ItemDescription |
| `enduranceRegen` | 6184 | `Endurance Regeneration: %1` | IDW_ItemDescription |
| `hpRegen` | 5845 | `Regeneration: %1` | IDW_ItemDescription |
| `manaRegen` | 5846 | `Mana Regeneration: %1` | IDW_ItemDescription |
| `delay` (+`skillType`) | 3008 | `Skill: %1   Atk Delay: %2` | IDW_ItemDescription |
| `recLevel` | 3028 | `Recommended level of %1.` | IDW_ItemDescription |
| `maxCharges` (+`charges`) | 3003 / 3002 | `Charges: %1` / `Charges: None` | IDW_ItemDescription |
| `lore` | 3048 | `LORE: ` | IDW_ItemDescription |
| `expendable` | 3001 | `EXPENDABLE     Charges: %1` | IDW_ItemDescription |

Drawn in the same pass because they share those lines and were equally
undrawn: `name` → the `Style_Titlebar` row; `icon` → `IDW_IconButton`'s decal;
`slots` → 3000 `Slot: %1` (names from eqstr 558..576, via the previously
callerless `buildSlotsString()`); `damage` → 12455; `ac` → 3016;
`str`/`dex`/`sta`/`cha`/`wis`/`int_`/`agi`/`hp`/`mana` and the five resists →
12457..12470, the trailing-space formats that run together on a row;
`endurance` → 5809; `attack` → 5844; `haste` → 5848; `avoidance` → 9042;
`reqLevel` → 3004; `weight`/`range`/`size` → 3037/3038;
`bagWR`/`bagSlots`/`bagSize` → 3042/3043; the four `ItemEffect`s → 3036 with
the 3029..3034 qualifiers and 5530; `classes`/`races` → 3064/3049 (via the
previously callerless `buildClassString()`/`buildRaceString()`); the flag
banner → 3094..3100, via `ItemInstance::getFlagsString()`, which also had no
production caller.

#### Fields the oracle does NOT show — hidden, and why

* **`currentSlot`.** `Slot:` (3000) is the item's EQUIPPABLE slot list, drawn
  from `slots`; the window has no element and the string table no format for
  where an item currently sits. Its `WriteOnlyFields` exemption now says that
  instead of "lands with item tooltips".
* **the seven `heroic*`.** `grep -i heroic` over `eqstr_us.txt` returns
  **nothing** — heroic stats postdate this client. There is no oracle line to
  put them on.
* **`bardEffect`.** No instrument-modifier item-display format exists in the
  table either (5750 "All Instrument Types" is a spell-requirement line).
* `loreGroup`, `flags`, `material`, `color`, `bookFilename` and the augment
  `type` bitmasks: structural, never printed.

#### Two corrections to B298's own census, both measured

1. **The count "21" over-reads.** The names the entry lists are 24.
2. **`expendable`, the seven `heroic*` and `bardEffect` have NO WRITER.**
   `TitaniumItemParser::parse()` never assigns them (fields 41-42 `BardType`/
   `BardValue` are read past with a comment) and a comment-filtered grep over
   `src/ include/ tests/ tools/ benchmarks/` finds only the declarations. They
   are dead struct members, not parsed-and-unshown — split out as **B375**.
   The seven `heroic*` surfaced as `WriteOnlyFields` **NEW** rows the moment
   the new test wrote them, which is the checker doing exactly its job.

#### The path

    click an occupied inventory slot   (StaticUIInput::processInput)
        -> ItemDisplayRequestIntent{slot}
        -> BridgeComponent::onItemDisplayRequestIntent   [GAME thread]
             ctx_.inventory_manager->getItem(slot)
             eqt::inventory::BuildItemDisplayText(*item)     <- the composer
        -> ItemDisplayShown{itemId, name, iconId, lines}
        -> IrrlichtBridge arm -> applyItemDisplayShown()
        -> ItemDisplayPanelState -> renderItemDisplayPopup()

The text is composed GAME-SIDE and shipped as finished lines rather than as
fifty numbered fields, because that is what the official client does: the
window's body is one STMLbox and the formats live in a shipped string table.
A renderer-side formatter would put the string ids on the wrong side of the
bridge.

**DEVIATION, flagged: the trigger is a LEFT click, not the oracle's right
click.** fable binds the right mouse button to mouselook and its static UI has
no right-click routing at all — `RendererEventReceiver` tracks
`rightButtonDown_` for the camera and latches no click. Left was the button
that was free: a click on an inventory slot did literally nothing before this
(the hit-test loop set `hoveredSlot` and broke).

**Scrolling was added rather than left as an unwritten field.** The first
capture showed the description overflowing the 190px box with nothing able to
move it — `Style_VScroll true` in the XML, and a `scrollOffset` no code
writes is the very defect class this board hunts. The window is now a fifth
`ScrollTarget` in T313's ONE scroll router (wheel, PageUp/PageDown,
`/scroll item`), not a private scroll writer.

### Leg B — B365, the search box types

**One widget, and there is exactly one.**
`include/client/graphics/ui/text_entry.h` declares `TextEntryState`
(`inputText`, `cursorPos`, `inputActive` — chat's own member names) and
`applyTextEntryKey()` / `focusTextEntry()` / `clearTextEntry()`.
**`ChatPanelState` DERIVES from `TextEntryState`**, which is what makes this a
share rather than a second widget: every existing chat call site and every
existing chat test compiles and passes untouched, and
`FrameComponent::processChatInput()`'s inline key drain became a call to that
one body plus chat's policy on the four actions it returns (Submit publishes
`ChatSubmitIntent` and drops focus, Cancel drops focus, Complete is T357's
slash completion, Changed needs nothing).

The behaviour was moved **verbatim**: Return/Escape/Tab/Backspace and the
printable 32..126 filter, nothing added — caret arrows included. "The chat
entry is unchanged" is a condition of this unit and a shared widget that
quietly changes one of its two users is the opposite of sharing.

`TradeskillPanelState` gains three `TextEntryState`s and a
`TradeskillField focusedField`; `hitTestTradeskillField()` focuses one and a
click elsewhere in the window blurs. `TradeskillButton` gains the four
enumerators T419 deliberately withheld (`Search`, `AddFavorite`,
`RemoveFavorite`, `ShowFavorites`) and `hitTestTradeskillButton()` returns
them.

**The keyboard GATE stays in one place.** `processChatInput()` is still the
only caller of `setChatInputFocused()`; it early-returns into
`FrameComponent::processTradeskillSearchInput()` when a search box holds the
caret. That is why `popKeyEvent`'s row in
`tests/test_read_and_reset_ownership.cpp` went 1 → 2 call sites, taking the
`getMouseWheelDelta` "two textual sites, ONE consumer, mutually exclusive"
exception — the guard caught this on the first suite run, which is the guard
working.

Enter in any of the three boxes, or `COMBW_SearchButton`, pushes the new
`RecipeSearchIntent{query, minTrivial, maxTrivial}` into the EXISTING
`MerchantComponent::SendRecipesSearch()`; the favourites row pushes
`RecipeFavoriteIntent{op, recipeId}` into the EXISTING
`SendRecipesFavorite()`. `object_type` is resolved bridge-side from the open
container's ground-spawn record, exactly the way
`EqActionHandler::resolveOpenContainerObjectType()` does for `/recipesearch` —
it is not the UI's to know.

Two deliberate refusals: a trivial bound that will not parse is **0** (= no
bound, what `/recipesearch` sends when the player omits it), never a silent
filter; and Tab is a no-op in a search box, because a recipe name is the
SERVER's vocabulary and T357's rule is that Tab with no vocabulary leaves the
text alone. The list-clear applier seeds the three boxes from the
server-acknowledged query but **never the box the player is holding the caret
in**.

**New board item B377**: fable keeps no favourites LIST, and eqemu's
`OP_RecipesFavorite` carries the whole set rather than a delta, so Add sends a
one-element list and Remove an empty one — correct only with exactly one
favourite. Wired anyway rather than leaving four named enumerators no hit test
could return, which is the defect T419 refused to create.

### Proof

| step | result |
|---|---|
| **checkout** | `task/T421-item-display-search-entry` off `fable` at **`8f7e718c`**, verified |
| **slot config** | `CMAKE_BUILD_TYPE=RelWithDebInfo`, `EQT_GLES2_IRRLICHT_DIR` = slot3's own — both checked at checkout |
| **pre-fix red, Leg A (compile)** | production reverse-applied via exported patch (`git apply -R`, never `git stash`) + the seven new production files held aside; `g++ -std=c++17 -fsyntax-only tests/test_item_display.cpp` → **exit 1**, `fatal error: client/graphics/ui/item_display_layout.h: No such file or directory`. There was no window, no composer, no layout and no event. |
| **pre-fix red, Leg A (behavioural)** | production restored, then ONLY the `ItemDisplayRequestIntent` push reverted in `static_ui_input.cpp`; rebuilt → **1 of 15 FAILED**, `ClickingAnOccupiedInventorySlotAsksForTheDisplay` (the click produced no intent — an inventory slot click did nothing at all). |
| **pre-fix red, Leg B (compile)** | same revert; `g++ -fsyntax-only tests/test_search_entry.cpp` → **exit 1**, `fatal error: client/graphics/ui/text_entry.h: No such file or directory`. |
| **pre-fix red, Leg B (behavioural)** | production restored, then ONLY `hitTestTradeskillButton`'s four new arms removed; rebuilt → **3 of 9 FAILED**: `TheSearchButtonPublishesWhatWasTyped`, `TheFavouriteButtonsCarryTheirOwnOperation`, `TheFourWithheldEnumeratorsAreNowReturnable`. That is B365 ("drawn and INERT") stated as an assertion. |
| **build** (`build`, RelWithDebInfo, GLES2) | exit **0** (background + exit file, `wait_for.sh --exit-file`, unit-private scratch dir) |
| **post-fix tests** | `test_item_display` **16/16 PASSED**; `test_search_entry` **9/9 PASSED** |
| **`ctest -j6`** | **4,349 of 4,351.** The two failures are exactly the two the dispatch named as expected while T420 (slot1) replaces the shared pack: `PackReproducible`, and `visual_gate` on `eqe1MappedBytes` only. |
| **visual gate** (standalone, after `build-gles2-x86.sh` exit 0) | **FAIL, and only on those three `eqe1MappedBytes` lines** — qeynos2 123015486→124064062, gfaydark 121681578→122730154, befallen 116166074→117214650, **+1,048,576 in all three**, which is exactly the installed pack's size delta (6,930,494 on disk vs the 5,881,918 this branch pins). CPU p95 well under ceiling everywhere: qeynos2 7.093 (10.108), gfaydark 6.076 (10.672), befallen 7.810 (10.948). **No IMAGE golden moved** (`git status tests/goldens/ benchmarks/` names only the statement-text golden below). |
| **ARM** (`build-arm-noble.sh`) | exit **0**; `build-arm-noble/bin/willeq` = ELF 32-bit LSB pie, ARM EABI5 |
| **file sizes** | `check_file_sizes.sh fable`: 32 changed files, **32 ok / 0 warn / 0 fail**; the nine new files checked explicitly, **9 ok** (largest: `tests/test_item_display.cpp` 437, `src/client/inventory/item_display_text.cpp` 226). Substantial new code went in NEW TUs. |
| **captures** | `/mnt/projects/fable/review/T421/after/` — all 18 panels including the NEW `item_display_800x600.png` and the CHANGED `tradeskill_800x600.png`, plus a README saying what to look at |

### Four suite reds that were REAL and are fixed here

1. **`ReadAndResetSweepGuard.NoAccessorGrowsASecondCaller`** — `popKeyEvent`
   went to two call sites. Updated with the mutual-exclusion reason (the
   `getMouseWheelDelta` precedent), not nudged.
2. **`WriteOnlyFields`** — twelve `ItemInstance` exemptions went STALE (the
   fields have readers now) and the seven `heroic*` came up NEW. Twelve rows
   deleted, seven added under B375, `currentSlot`'s reason rewritten.
3. **`FrameRenderOrder.ComposedDrawOrderMatchesThePreG05Golden`** — 309 → 313.
   Taught IN PLACE at the two positions (entries 260-262, the item display's
   input hop; entry 285, its draw), never regenerated: the composed list was
   diffed against the pre-edit golden and **the only hunks are those two
   insertions** — everything else is byte-identical and nothing was reordered.
   `kGoldenSize` and the comment above it name both positions.
4. **`DocCounts` / `DocCitations`** — 99 → 100 events and 37 → 40 intents in
   three docs; and two BOARD.md citations that were already broken at
   `8f7e718c` (T419's B365 entry cited `common/eq_packet_structs.h:4313` and
   `zone/tradeskills.cpp:866` without the `/mnt/projects/akk-stack/code`
   prefix) fixed while striking that entry. Both green.

`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` also failed
in the first `-j6` run with **empty** process output — the load artefact the
protocol names. Passes standalone.

**Commits.** Leg B `f35a4517`, Leg A `f7b0fc22` (which also carries the shared
TUs and this Review). The board hashes were filled in a third, no-code commit
because each strike names its own commit.

### Deviations

* **Leg A is a new window, not fields added to an existing popup** — the plan
  assumed an item display existed in `static_panels.cpp`; none did.
* **The text layout IS quotable**, from `eqstr_us.txt`, so the plan's
  "text layout unquotable" fallback was not used. Only the line ORDER and the
  title-bar height are fable's, and both are flagged in the source.
* **Left click, not right click**, to open the window (reason above).
* **The two commits are split by LEG but not cleanly by file.** Both legs edit
  `static_ui_input.{h,cpp}`, `renderer_frame_input.cpp`, `frame_component.h`,
  `renderer_intents.h`, `bridge_component.{h,cpp}` and the two CMake lists; a
  hunk-level split of those would be a fiction, so the shared TUs ride with
  the Leg A commit and neither commit builds standalone. The branch does.

### Surprises

* **`eqstr_us.txt` is an oracle nobody had used.** The official client ships
  its entire item-display vocabulary as numbered format strings — 3000..3105
  plus 12454..12470, 5809/5836/5844..5848, 6184, 9039..9045 — so a window the
  XML describes as "one free-text box" is in fact almost completely pinned.
  Worth remembering for the `SpellData` and `SkillData` halves of B298.
* **Three ready-made formatters had ZERO callers**: `buildSlotsString()`,
  `buildClassString()` and `buildRaceString()`
  (`src/client/inventory/inventory_constants.cpp`), plus
  `ItemInstance::getFlagsString()`. Their abbreviation tables are eqstr
  3050..3079's and 558..576's, in the same order. This unit is their first
  caller.
* **Nine of the fields B298 called "parsed and never shown" are not parsed
  either** — see B375. The board entry was written from the census summary
  rather than the census output, the same shape as B299's
  `m_rest_mana_threshold` correction.
* **`WriteOnlyFields` flags a field the moment a TEST writes it.** The seven
  `heroic*` had been invisible to the checker because nothing anywhere touched
  them; asserting that they stay hidden is what made them "write-only". The
  checker is right — they are dead — but it is worth knowing that a test can
  promote a completely-unused member into a red.
* **The first capture showed the defect the code review would not have.**
  `scrollOffset` on the new panel had no writer until the picture showed the
  description running off the bottom of the box.
