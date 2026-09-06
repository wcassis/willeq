# T425 — the spell info window from the official format strings (B298 spell half); members with no writer (B375)

Class B ×2, slot3, no server. Read B298 (its spell/skill/entity halves —
`SpellData` carries `resist_diff`, `zone_type`, `teleport_zone`,
`spell_fades`, `player_tag`, `environment_type`, `time_of_day`,
`aoe_duration`, `push_up`, `allow_rest`, `is_dispellable`, … parsed and
never shown), B375, and T421's §Review — the KEY finding: the official
client ships its item/spell/skill display vocabulary as numbered format
strings in `/mnt/projects/EverQuestP1999/eqstr_us.txt`; T421 quoted the
item lines; the spell-info lines are their siblings (grep the file for
the spell description strings: mana, casting time, recast, duration,
target type, range, resist, "Cast on you:", "Cast on other:",
"Effect Fades:" etc. — quote each id). Read the spellbook/gem code
(`static_panels.cpp`, `spellbook*`), `EQUI_SpellBookWnd.xml` (the spell
info/description area) and `EQUI_SpellDisplay.xml` if present.

## Leg A — spell info

1. Table: each `SpellData` field the census names → the eqstr line that
   shows it (id quoted) → the window element (XML quoted). Fields with
   no eqstr line stay hidden (say which — e.g. `player_tag`, `push_up` may
   be mechanics only).
2. Draw the spell display window (the oracle opens it on a gem/spellbook
   click — fable's left click, as T421 did for items) with the composed
   lines in eqstr order, using T421's composer/text-entry/scroll
   machinery (no second composer). Tests pre-fix failing per line;
   `ui_panel_capture` gains the window; capture into `review/T425/`.
3. Strike the spell half of B298; annotate the skill/entity halves with
   the eqstr ids you find for them.

## Leg B — B375

Nine `ItemInstance` members with no writer (`expendable`, the seven
`heroic*`, `bardEffect`): read the wire struct (`Item_Struct` in eqemu's
`titanium_structs.h`) — if the field is on the wire, parse it (writer
added, then T421's display shows the ones eqstr has a line for); if not
on Titanium's wire, delete the member (compiler proof) and the exemption
row. Strike B375.

## Proof

Build (background), `ctest -j6` (no expected failures unless T424 (slot2)
replaces the pack: `PackReproducible`/`eqe1MappedBytes` only), pre-fix/
post-fix, G05 in place if a pass is added, visual gate standalone + ARM
(no image golden should move), file sizes.

STOP: none expected.

## Review

Branch `task/T425-spell-info-window`, base `fable` at **`ff69ef59`** (verified
at checkout). Slot 3, `build/` RelWithDebInfo + `EQT_GLES2_IRRLICHT_DIR`
pointing at slot3's own Irrlicht (both checked before any edit). No server:
T422 holds the eqemu lock and nothing here opened a socket (B306).

### The finding Leg A turns on: THE ORACLE HAS NO SPELL DISPLAY WINDOW

The plan says to read "`EQUI_SpellDisplay.xml` (if present)". It is not
present, and neither is any equivalent. `grep -n 'Screen item=' uifiles/default/*.xml`
lists **79 windows** and not one of them is a spell display;
`grep -rl 'SpellDisplay\|SpellInfo' uifiles/default/` returns **nothing**.

The official Titanium client shows a spell in the window it shows an item in.
`EQUI_ItemDisplay.xml` says so itself — the icon button it hangs on
`ItemDisplayWindow` decals the **spell** sheet:

    <Button item="IDW_IconButton">
        <ButtonDrawTemplate>
            <Normal>RedIconBackground</Normal>
            <NormalDecal>BuffIcons</NormalDecal>

and the spell vocabulary sits **inside the item-display block of the string
table**: 3006 `Mana Cost: %1` is nine ids below 3016 `AC: %1`, and 3101-3107
(`Spell:`, `Spell Duration: %1.`, `Spell Duration: Permanent.`, `CANNOT USE`,
`Skill: %1`, `Normal Cast Time…`, `Cast Time: %1`) sit directly after the
3094-3100 item flag banner T421 quoted.

So this unit adds **no window, no panel, no layout, no event, no scroll
target and no second composer pipeline**. It adds one composer FUNCTION —
whose input type is a `SpellData` rather than an `ItemInstance`, which is the
only reason it cannot literally be `BuildItemDisplayText()` — and reuses
`ItemDisplayShown`, `ItemDisplayPanelState`, `applyItemDisplayShown()`,
`renderItemDisplayPopup()`, `ItemDisplayLayout` and `ScrollTarget::ItemDisplay`
exactly as T421 left them. The window's SHAPE, its scrolling and its close box
are therefore untested-by-me only in the sense that T421's sixteen cases
already pin them; `SpellDisplayT425.TheSpellReusesTheOraclesOneDisplayWindow`
asserts the reuse rather than re-asserting the geometry.

### Leg A — the field → eqstr id → element table

Every drawn line, with its id and its verbatim shipped format. The element is
`IDW_ItemDescription` for all of them except the name and the icon.

| `SpellData` field | eqstr id | format, verbatim | element |
|---|---|---|---|
| `class_levels[class-1]` | 3004 | `Level Needed: %1` | IDW_ItemDescription |
| `class_levels[class-1] == 255` | 3005 | `Level Needed: CANNOT USE` | IDW_ItemDescription |
| `mana_cost` | 3006 | `Mana Cost: %1` | IDW_ItemDescription |
| `calculateDuration(level)` | 3102 | `Spell Duration: %1.` | IDW_ItemDescription |
| `calculateDuration(level) == 0xFFFFFFFF` | 3103 | `Spell Duration: Permanent.` | IDW_ItemDescription |
| `casting_skill` | 3105 | `Skill: %1` | IDW_ItemDescription |
| `cast_time_ms` | 3107 | `Cast Time: %1` | IDW_ItemDescription |
| `range` | 6730 | `Range = %1` | IDW_ItemDescription |
| `recast_time_ms` | 9234 | `Recast Delay: %1` | IDW_ItemDescription |
| `name` | — | the `Style_Titlebar` row | Screen ItemDisplayWindow |
| `spell_icon` | — | `<NormalDecal>BuffIcons</NormalDecal>` | IDW_IconButton |

#### Fields with NO line, and the measured absence for each

Every one of these was decided by grepping `eqstr_us.txt`, not by argument.
The greps are reproducible: `grep -nE "^[0-9]+ .{0,40}(Resist|resist)"`,
`grep -nE "^[0-9]+ (Resist|Target|Range|Duration|Recast|Recovery|Endurance Cost|Location)"`,
`grep -nE "^[0-9]+ .{0,30}(Destination|Location|Teleport|Indoor|Outdoor|Night|Daytime|Dispel|Beneficial|Detrimental|Target Type|Area of Effect|Self Only)"`.

| field | why it is not drawn |
|---|---|
| `resist_type`, `resist_diff` | **the table has no spell-resist line at all.** Every "Resist" hit is a combat message, `Sense NPC Resists` (an AA name), or 9044 `Stun Resist: %1%` — the ITEM stat |
| `zone_type` | no Indoor/Outdoor format string |
| `environment_type` | no environment format string |
| `time_of_day` | no time-of-day format string |
| `teleport_zone` | no Destination/Location line |
| `player_tag` | a spells_us.txt internal tag (`PLAYER_1`); names nothing player-visible |
| `target_type`, `aoe_range`, `aoe_duration` | no Target Type / Area of Effect line |
| `push_back`, `push_up` | no knockback line |
| `allow_rest`, `is_dispellable`, `no_partial_resist` | no med-while-active, dispel or partial-resist line |
| `endurance_cost` | 5809 `Endurance: %1` is the ITEM stat, not a spell cost |
| `recovery_time_ms` | no Recovery line (236/237 are refusal messages) |
| `reagent_id`, `reagent_count`, `noexpend_reagent_id`, `no_expend_reagent` | **the one line the oracle HAS and fable cannot draw** — 6732 `Reagents Needed: %1` wants an item NAME, and the client has no item-name-by-id source: `data/willeq.db` has no items table and the wire only names an item inside an item packet. Boarded as **B391** rather than faked with ids |
| `cast_anim`, `target_anim`, `travel_type`, `spell_affect_index`, `spell_group`, `spell_rank`, `effects[]`, `duration_formula`, `duration_cap` | mechanics/visuals; never printed |
| eqstr **3101** `Spell:` | deliberately UNUSED. It is the ITEM window's label for an item's spell effect; a spell's own name goes where an item's does, on the title bar |

#### `spell_fades` is not a window field, and it is now READ

It is the sibling of `cast_on_you` / `cast_on_other`, which
`spell_packet_handlers.cpp` already prints to chat on a landing.
`spell_fades` is the message the official client prints when the buff wears
off, and it had **no reader anywhere** — a loader writing a string nobody
read, which is B298's own class. It is wired at `BuffManager`'s EXISTING
`setBuffFadeCallback` in `src/client/core/everquest.cpp` (the T339 vision
hook), player buffs only (`entity_id == 0`): the text is second-person
("Your strength fades") and the client has no third-person form of it. No new
callback, no new hook.

#### What is NOT the oracle's, flagged

* **The ORDER of the lines** — the string table is a flat id→format map. The
  order used is ascending id, which is T421's rule unchanged and the only
  ordering evidence the shipped data carries. `LinesComeOutInAscendingStringIdOrder`
  pins it.
* **The UNIT in 3102's `%1`** — ticks, from `SpellData::calculateDuration()`
  at the caster's level. The format says nothing about what fills it.
* **The title-bar height (18)** — T421's, unchanged.
* **The TRIGGER.**

#### DEVIATION, flagged: shift+left click, not right click

The oracle opens the spell display on a **right** click on a gem or a
spellbook entry. fable binds the right mouse button to mouselook and its
static UI latches no right click at all (`RendererEventReceiver` tracks
`rightButtonDown_` for the camera) — T421 recorded exactly this wall and took
left, because a click on an inventory slot did **nothing** before it. Here
left is not free: T272 made a gem click cast, a gem click with the spellbook
open pick the memorise target, and a spellbook row click memorise. So the free
gesture is **shift+left**, and two tests assert the plain clicks still do what
they did (`APlainGemClickStillCasts`, and the second half of
`ShiftClickingASpellbookRowAsksForTheDisplayAndPlainStillMemorises`).

`shift` had to be an ARGUMENT to the existing `processInput()` rather than its
own entry point (the T415/T421 shape) precisely because it MODIFIES clicks that
call already handles: a later separate pass would fire after the cast had
already been published. It is appended with a `= false` default, so all four
existing callers — `renderStaticUI()` and three test files — compile and assert
exactly what they did before.

#### The path

    shift + click a memorised gem / a spellbook row   (StaticUIInput::processInput)
        -> SpellDisplayRequestIntent{spellId}
        -> BridgeComponent::onSpellDisplayRequestIntent      [GAME thread]
             ctx_.spell_manager->getSpell(spellId)
             eqt::spell::BuildSpellDisplayText(*spell,
                 player().classId(), player().level())        <- the composer
        -> ItemDisplayShown{itemId, name, iconId, lines}       <- T421's event
        -> IrrlichtBridge arm -> applyItemDisplayShown()       <- T421's applier
        -> ItemDisplayPanelState -> renderItemDisplayPopup()   <- T421's draw

### Leg B — B375, the per-member wire verdict

Decided against eqemu's `titanium.cpp` `SerializeItem()`, the wire this client
actually parses, one member at a time.

| member(s) | on Titanium's wire? | eqstr line? | verdict |
|---|---|---|---|
| `heroicStr`/`Sta`/`Agi`/`Dex`/`Wis`/`Int`/`Cha` | **NO** — `grep -n "Heroic" common/patches/titanium.cpp` returns **zero hits**; `ItemData` declares them (item_data.h:513) but `SerializeItem()` never emits one | **no** — `grep -i heroic eqstr_us.txt` is empty; heroic stats postdate this client | **DELETED**, compiler proof |
| `bardEffect` (an `ItemEffect`) | **NO** — the wire carries exactly FIVE effect blocks, `Click`/`Proc`/`Worn`/`Focus`/`Scroll` at fields 134-158, and no sixth. The bard data it carries is the SCALAR pair `BardType`/`BardValue` at fields 41-42, a skill-modifier pair, not an effect | **no** instrument-modifier item line (5750 is a spell requirement) | **DELETED**; the parser's existing `// fields[41-42]` comment stays |
| `expendable` | **YES, derived.** eqemu's own `ItemInstance::IsExpendable()` is `Click.Type == ItemEffectExpendable \|\| ItemType == ItemTypePotion`, and BOTH inputs are serialized: `Click.Type` is field 135 (already read as `clickEffect.type`), `ItemType` is field 0 (already read as `itemType`). Enumerator values from `item_data.h`: Expendable = 3, Potion = 21 | **yes**, 3001 `EXPENDABLE     Charges: %1`, printed by `BuildItemDisplayText()` since T421 and unreachable until now | **WIRED** |

T421's `TheOraclesOmissionsStayOmitted` no longer searches the composed text
for heroic values or instrument wording: the members are gone, so a deleted
member cannot leak and the compiler enforces it better than a string search.

### Proof

| step | result |
|---|---|
| **checkout** | `task/T425-spell-info-window` off `fable` at **`ff69ef59`**, verified |
| **slot config** | `CMAKE_BUILD_TYPE=RelWithDebInfo`, `EQT_GLES2_IRRLICHT_DIR=/mnt/projects/fable/wt/slot3/third_party/irrlicht-gles2-x86` — both checked at checkout |
| **pre-fix red, Leg A (compile)** | new header moved aside (an `mv`, never `git stash`); `g++ -std=c++17 -fsyntax-only tests/test_spell_display.cpp` → **exit 1**, `fatal error: client/spell/spell_display_text.h: No such file or directory` |
| **pre-fix red, Leg A (behavioural)** | production restored, then ONLY the two shift arms in `static_ui_input.cpp` reverted via an exported patch (`git apply -R`), the signature kept; rebuilt → **2 of 10 FAILED**: `ShiftClickingAGemAsksForTheSpellDisplay`, `ShiftClickingASpellbookRowAsksForTheDisplayAndPlainStillMemorises` |
| **pre-fix red, Leg B** | ONLY the two-line `expendable` derivation reverted (exported patch); rebuilt → **2 of 19 FAILED**: `ItemDisplayT425.ExpendableIsParsedFromTheClickEffectType`, `ItemDisplayT425.ExpendableIsParsedFromThePotionItemType`. That is B375's `expendable` row stated as an assertion |
| **build** (`build`, RelWithDebInfo, GLES2) | exit **0** (background + exit file, `wait_for.sh --exit-file`, unit-private names, `rm -f` before each launch) |
| **post-fix tests** | `test_spell_display` **10/10 PASSED**; `test_item_display` **19/19 PASSED** (16 T421 + 3 new) |
| **G05 golden, taught IN PLACE** | entry **257** only — the last argument line of the existing `processInput(...)` call gained `ctx_.eventReceiver->isKeyDown(irr::KEY_SHIFT)`. `kGoldenSize` **UNCHANGED at 314**: one entry's TEXT changed, nothing added, removed or reordered. The comment above `kGoldenSize` gained a paragraph naming the position and the reason |
| **G05, both faults re-planted** | (a) swapped golden entries 257/258 → `draw order diverges at statement 257`, 1 FAILED. (b) deleted golden entry 257 → `the render-order golden changed size` **and** `draw statement count changed: 313 -> 314`, 2 FAILED. Golden restored from a saved copy; all 4 cases green |
| **`ctest -j6`** | **4,391 of 4,394.** `DocCitations` was a REAL red and is FIXED (see below); the other two are exactly the pair the dispatch named as expected while T424 (slot2) replaces the shared pack |
| **`PackReproducible`, standalone** | FAIL, and the reason is not this branch: `built: 14a6bd45… (6930494 bytes)` == `pinned: 14a6bd45…`, while `shipped: 19e78745…` at `/mnt/projects/EverQuest_ETC1/willeq.eqe1` differs. This tree's own rebuild matches its own pin exactly; the INSTALLED pack was replaced by another slot |
| **visual gate, standalone** (after `build-gles2-x86.sh` exit 0) | **FAIL, and only on the three `eqe1MappedBytes` lines** — qeynos2 124064062→124063684, gfaydark 122730154→122729776, befallen 117214650→117214272, **−378 in all three**, the same installed-pack delta. Every image comparison PASSED. CPU p95 comfortably under ceiling: qeynos2 4.824 (10.108), gfaydark 5.419 (10.672), befallen 5.994 (10.948) |
| **image goldens** | **NONE moved.** `git status tests/goldens/ benchmarks/ data/` is empty |
| **ARM** (`build-arm-noble.sh`) | see the final report line |
| **file sizes** | `check_file_sizes.sh fable`: **19 changed files, 19 ok / 0 warn / 0 fail**. The two new TUs are small — `src/client/spell/spell_display_text.cpp` 92 lines, `tests/test_spell_display.cpp` 361 |
| **captures** | `/mnt/projects/fable/review/T425/after/` — 18 panels including the NEW `spell_display_800x600.png`, plus a `README.md` naming the eqstr id behind every line on screen |

### The one REAL suite red, and it was mine

`DocCitations` failed on `refactor/BOARD.md:5994 cites common/item_instance.h
— file does not exist`. The B375 strike quoted eqemu's `IsExpendable()` by its
akk-stack-relative path. **This is the identical trap T421 hit** (its B365
strike cited `common/eq_packet_structs.h:4313` the same way), so it is now
twice: a citation into `/mnt/projects/akk-stack/code/` must carry the full
prefix or the checker reads it as a repo path. Fixed in three places — the
board entry and the two source comments that quoted the same file — and
`DocCitations` is green.

### Deviations

* **Leg A adds no window.** The plan's phrasing ("Draw the spell display
  window", "`EQUI_SpellDisplay.xml` if present") assumed a second window
  existed in the oracle. None does, and the evidence that the ONE window is
  shared is positive rather than merely absent (`<NormalDecal>BuffIcons</NormalDecal>`
  plus the spell strings living inside the item-display id block). Reusing
  T421's window is therefore the oracle-matching answer, not a shortcut.
* **`BuildSpellDisplayText()` is a second FUNCTION, not a second composer.**
  "One composer (T421's), never a second" is read here as: one composed-text
  pipeline, one event, one panel, one scroll router — which is what shipped.
  A `SpellData` cannot be fed to a function taking an `ItemInstance`, so a
  sibling free function in the spell domain TU is the minimum, and it follows
  T421's discipline line for line (quoted formats with ids, ascending id
  order, zero-omitted-not-printed, free function with no bridge/DB/device).
* **Shift+left click, not right click** — reason above, and it is why
  `processInput()` grew a defaulted parameter instead of gaining an entry
  point.
* **The trigger is not reachable from the spellbook while a gem is hovered**
  in one edge case: with the spellbook open, shift+click on a GEM opens that
  gem's spell display rather than picking the memorise target. That is
  deliberate (the gem's own spell is what you asked about) and the plain click
  still picks the target.
* **Three commits, not two.** Each board strike names the commit that made it,
  and a commit cannot contain its own hash, so the hashes are filled in a
  third no-code commit — exactly T421's shape.
* **`spell_fades` was wired outside the window.** It is in B298's spell
  census, so leaving it would have blocked the strike; but it is a chat
  message, not a display line, so it went to the existing fade callback. That
  is one extra file (`src/client/core/everquest.cpp`) beyond a pure window
  unit, flagged rather than absorbed.

### Surprises

* **The oracle's spell window does not exist — and the proof it is shared is
  in the XML fable already quoted.** T421 read `EQUI_ItemDisplay.xml` closely
  enough to resolve every anchor offset and did not notice that its icon
  button decals `BuffIcons`, the spell sheet. The window was always the spell
  display too.
* **The whole `eqstr_us.txt` table is ALREADY SHIPPED IN `willeq.db`** — table
  `eqstrings`, 5,901 rows, loaded by `tools/willeq_db_builder.cpp` — and
  ALREADY reachable at runtime through `EQT::StringDatabase::getString()` /
  `formatString()`, which `EverQuest::m_string_db` owns and `chat_component.cpp`
  and `eq_strings.cpp` already call. Both display composers transcribe by hand
  what one `getString(3006)` would fetch. Boarded as **B390**; not fixed here
  because making a free, database-free composer take a string table is a
  design call, not a typo fix.
* **`WriteOnlyFields` flagged SEVEN MORE fields the moment the new test wrote
  them** — `allow_rest`, `aoe_duration`, `environment_type`, `is_dispellable`,
  `no_partial_resist`, `push_up`, `time_of_day`. This is precisely T421's
  `heroic*` experience repeating: asserting that a field stays hidden is
  itself a write. The checker is right (they are unread), the seven are the
  remainder of B298's spell census, and each now carries a measured reason.
* **`expendable` is not a wire field and B375 said it would close "the moment
  the parser fills it" — but there is nothing to fill it FROM.** It is
  derived, and the derivation is eqemu's own `IsExpendable()`, using two
  fields the parser was already reading. The entry's framing was slightly
  off; the verdict it predicted was right.
* **eqstr's spell vocabulary is much thinner than the item's.** The item
  window has ~50 usable formats; the spell display has nine. Half of B298's
  spell census exists only because the loader is faithful to spells_us.txt,
  not because the client ever showed it.
