# T419 — the tradeskill recipe list (B335); the pet window's button row (B352)

Class B ×2, slot3, no server. Read B335 (the four recipe opcodes are
already SENT from `merchant_vendor_bank.cpp`; `ZoneProcessRecipeReply`'s
body is a LOG_DEBUG — the consumer is missing), B352, T415's §Review
(`tradeskill_layout.h`, the left-side elements of `EQUI_TradeskillWnd.xml`
it did not draw), T368/T416's §Reviews (the pet panel and its capture),
and eqemu's `OP_RecipeReply` handler/struct (`/mnt/projects/akk-stack/code`
— quote the struct the server fills).

## Leg A — B335

1. Decode `OP_RecipeReply` (and its siblings the client sends for:
   search, favourites, details) into a `RecipeList` state the game thread
   publishes as a bridge event (enriched — name, id, tradeskill,
   trivial, component names); the renderer draws the window's left side
   from the XML (`COMBW_RecipeList`, the search box, `COMBW_Favorites`
   — quote Location/Size) with the same scaling T415 used; a row click
   pushes the existing recipe-details request (find it in
   `merchant_vendor_bank.cpp`). Tests, pre-fix failing: a synthetic
   `OP_RecipeReply` packet → the list state; the rows' rects match the
   XML; the enriched event carries the names.
2. Strike B335. Live proof (open a forge on the server, see the recipe
   rows) OWED to the next lock unit.

## Leg B — B352

The pet window's button row hangs outside the panel and the 10th button
is clipped (pre-dates T411; T416's `crops/`). Read `EQUI_PetInfoWindow.xml`
(`PIW_Button*` Location/Size) and lay the row out from the XML scaled
into fable's panel rect; a static-panel test (pre-fix failing) that every
button rect lies inside the panel and matches the XML. Strike B352.

## Proof

Build (background + exit file), `ctest -j6` (expected red: only
`PackReproducible`/`eqe1MappedBytes` while T417 (slot1) is replacing the
shared pack), pre-fix/post-fix, visual gate standalone + ARM (no golden
should move), file sizes; re-run `ui_panel_capture` (T416's tool) for the
pet window and the tradeskill window into `review/T419/`.

STOP: the recipe reply's struct is not decodable from the packets fable
receives (report the bytes).

## Review

Branch `task/T419-recipe-list-pet-buttons`, base `15b954df`. Two commits, one
per leg: Leg A (B335) is the first, Leg B (B352) the second — the one carrying
this Review. Slot 3, `build/` RelWithDebInfo + GLES2, no server (T418 held the
eqemu lock; nothing here opened a socket).

### Leg A — B335, the recipe list

**The struct, quoted rather than inferred.** eqemu
`common/eq_packet_structs.h:4313` (identical in
`common/patches/titanium_structs.h:2798`):

    //one sent for each item, from server in reply to favorites or search
    struct RecipeReply_Struct {
        uint32 object_type;
        uint32 some_id;  //same as in favorites
        uint32 component_count;
        uint32 recipe_id;
        uint32 trivial;
        char recipe_name[64];
    /*84*/
    };

`Client::SendTradeskillSearchResults` (`zone/tradeskills.cpp:866`) allocates one
`OP_RecipeReply` **per matching row** inside its result loop and `FastQueuePacket`s
it. It writes no count, no header and no terminator, and it returns early with
NOTHING sent when the query matches nothing.

**How it reaches the window.**

    /recipesearch  ->  MerchantComponent::SendRecipesSearch()   ---(1)
                       (or SendRecipesFavorite(), same shape)
                          |
                          | publishes TradeskillRecipeListCleared
                          v
    server   ->  OP_RecipeReply x N
                          |
                       MerchantComponent::ZoneProcessRecipeReply()
                          |
                       eqt::merchant::DecodeRecipeReply()        ---(2)
                          |
                          | publishes TradeskillRecipeReceived
                          v
                       IrrlichtBridge (two new case arms)
                          |
                       applyTradeskillRecipeListCleared() / applyTradeskillRecipeReceived()
                          |
                          v
                       TradeskillPanelState::recipes
                          |
                       renderTradeskillRecipeList()              ---(3)
                          |
                       hitTestTradeskillRecipe() -> RecipeDetailsRequestIntent
                          |
                       BridgeComponent::onRecipeDetailsRequestIntent()
                          |
                       MerchantComponent::SendRecipeDetails()  (the existing OP_RecipeDetails)

(1) **The list boundary is the REQUEST, not the reply.** This is the one design
call worth naming: because the server marks neither the start nor the end of a
run, nothing in the inbound stream can say "a new list begins". The two senders
that ask for a list therefore publish `TradeskillRecipeListCleared` as the
packet goes out, and every reply that arrives afterwards appends. A repeat of a
recipe id already in the list is ignored rather than duplicated (a favourites
fetch and a search can overlap), and the list is capped at 200 rows.

(2) The decode is a FREE function, not a private method, so
`tests/test_recipe_list.cpp` drives the real wire bytes through the real decode
with no `ClientContext`, no bridge and no server.

(3) A NEW TU, `src/client/graphics/ui/tradeskill_recipe_list.cpp` (233 lines) —
`static_panels.cpp` is already 1,106 and `tradeskill_panel.cpp` is T415's.

**DEVIATION from the plan, flagged not absorbed.** The plan asked for an
"enriched — name, id, tradeskill, trivial, component names" event. **Two of
those five are not on the wire and cannot be**: the server filters by
`tradeskill` in SQL before it builds the packet (`RecipeReply_Struct` has no
such field), and the component NAMES go out only in reply to `OP_RecipeDetails`,
as a separate hand-packed buffer (`Client::SendTradeskillDetails`,
`tradeskills.cpp:880`). The event therefore carries name, id, trivial,
component COUNT, object type and some_id — everything the struct has. This is
not the STOP condition (the struct decodes fine); it is the struct being smaller
than the plan assumed.

**The element table** — `EQUI_TradeskillWnd.xml`, a 400x525 window. Anchors
resolved (`AnchorToLeft/ToTop == false` means "from the parent's right/bottom
edge"); the derivation is written out in `tradeskill_layout.h` so a reader can
check it without redoing it.

| XML element | XML says | absolute in the 400x525 window | fable at 800x600 |
|---|---|---|---|
| `COMBW_LeftSideArea` | 2 / 0 / 160(from right) / 0(from bottom) | (2,0)-(240,525) | — (container) |
| `COMBW_SearchArea` | 0 / 0 / 0(from right) / 124 | (2,0)-(240,124) | — (container) |
| `COMBW_MinTrivialText` | Location (4,4) Size 100x24 | (6,4) 100x24 | {206,127,306,143} |
| `COMBW_MinTrivialEdit` | Location (106,4) Size 50x24 | (108,4) 50x24 | {308,127,358,143} |
| `COMBW_MaxTrivialText` | Location (4,30) Size 100x24 | (6,30) 100x24 | {206,145,306,161} |
| `COMBW_MaxTrivialEdit` | Location (106,30) Size 50x24 | (108,30) 50x24 | {308,145,358,161} |
| `COMBW_SearchTextText` | Location (4,56) Size 100x24 | (6,56) 100x24 | {206,162,306,178} |
| `COMBW_SearchTextEdit` | Location (106,56) Size 80x24 | (108,56) 80x24 | {308,162,388,178} |
| `COMBW_SearchButton` | Location (50,84) Size 100x20 | (52,84) 100x20 | {252,181,352,194} |
| `COMBW_RecipeListArea` | 0 / 124 / 0(from right) / 50(from bottom) | (2,124)-(240,475) | — (container) |
| `COMBW_KnownRecipeText` | 2 / 2 / 2(from right) / 22 | (4,126) 234x20 | {204,209,438,222} |
| `COMBW_RecipeList` | 2 / 24 / 2(from right) / 2(from bottom) | (4,148) 234x325 | {204,223,438,439} |
| ... `<Columns>` | 140 "Recipe Name", 50 "Trivial" | name x0=4, trivial x0=144 | name ends / trivial starts at x=344 |
| `COMBW_FavoritesArea` | 0 / 50(from bottom) / 0(from right) / 0(from bottom) | (2,475)-(240,525) | — (container) |
| `COMBW_FavoritesText` | 2 / 0 / 0(from right) / 22 | (4,475) 236x22 | {204,441,440,455} |
| `COMBW_AddFavoriteButton` | Location (2,24) Size 60x24 | (4,499) 60x24 | {204,457,264,473} |
| `COMBW_RemoveFavoriteButton` | Location (64,24) Size 60x24 | (66,499) 60x24 | {266,457,326,473} |
| `COMBW_ShowFavoritesButton` | Location (124,24) Size 60x24 | (126,499) 60x24 | {326,457,386,473} |

**The one number that is NOT the oracle's** is the recipe list's ROW HEIGHT.
`COMBW_RecipeList` is a `<Listbox>`; the XML gives its rect and its column
widths and says nothing about row height — the official client measures it from
the window font at runtime. `ORACLE_ROW_H = 20` is fable's choice (matching the
20px this same file uses for every Label and Editbox), flagged in the header
rather than dressed up as a quotation. It yields sixteen visible rows.

**A rounding bug found and fixed while writing the test.** T415's `scaled()`
maps an origin and a SIZE independently, which is right for an isolated rect and
wrong for a run of adjacent ones: sixteen 20px rows scaled that way gap or
overlap by a pixel wherever the two integer divisions disagree (`row 13` came
out at y=397 with row 12 ending at 396). `recipeRowRect()` now takes each row's
bottom from the NEXT row's scaled top (`scaledX`/`scaledY`), so the list tiles
exactly. `RowsTileTheListboxAndTheColumnsAreTheOracles` asserts it.

**Inert by design, boarded as B365**: the three editboxes, `COMBW_SearchButton`
and the three favourites buttons are DRAWN (dim) and not clickable, because
fable's static UI has no text-entry widget outside the chat box.
`TradeskillButton` deliberately gained no enumerators for them — an enum name no
hit test can ever return would be the same unreached-code defect one layer up.

### Leg B — B352, the pet command grid

**The element table** — `EQUI_PetInfoWindow.xml`, a 154x142 window,
`Style_Sizable true`, every child `Location`/`Size` (no anchors).

| XML element | Location | Size | `<Text>` |
|---|---|---|---|
| `PIW_PetHPGauge` | (2,0) | 108x26 | "No Pet" (the gauge carries the pet's NAME) |
| `PIW_AttackButton` | (2,28) | 108x20 | "Attack" |
| `PIW_FollowButton` | (2,50) | 54x20 | "Follow" |
| `PIW_TauntButton` | (56,50) | 54x20 | "Taunt" |
| `PIW_GuardButton` | (2,74) | 54x20 | "Guard" |
| `PIW_SitButton` | (56,74) | 54x20 | "Sit" |
| `PIW_StandButton` | (56,74) | 54x20 | "Stand" (same cell — the toggle's other face) |
| `PIW_BackButton` | (2,96) | 54x20 | "Back" |
| `PIW_LostButton` | (56,96) | 54x20 | "Go Away" |

So the oracle's shape is: full-width gauge, full-width Attack, then a
**two-column** grid of 54x20 cells, rows at y=50/74/96. **Seven distinct
command cells.**

**TWO DEVIATIONS, both measured and both written into
`include/client/graphics/ui/pet_layout.h`:**

1. **The button SET is not the oracle's, because fable's is not Titanium's.**
   `PetButton` (`include/client/pet_constants.h`) is eqemu's TEN — Sit, Stop,
   Regroup, Follow, Guard, Taunt, Hold, GHold, Focus, SpellHold — and only Sit,
   Follow, Guard, Taunt and (as "Back") Stop have a cell in this XML at all.
   Regroup, Hold, GHold, Focus and SpellHold postdate the Titanium UI file. Ten
   cells cannot be laid on a grid drawn for seven.
   *(B352's text also called the tenth button "Shrink". It is SpellHold; fable
   has no Shrink pet button. Corrected on the board.)*
2. **The grid is 5x2, not the oracle's 2x5.** Measured at 640x480, the smallest
   supported resolution: `playerStatus` ends at y=70, `groupPanel` runs 74..168,
   `petPanel` starts at 172, and `buffBar`'s top edge is at **y=228** — sixteen
   pixels of slack. The oracle's two-column grid needs five rows for ten cells;
   at the oracle's own 20px row that is 100px, and scaling it proportionally
   into 16px would put a 3px row under a 3px row. Five columns of two rows fits
   the ten in 20px with 4px to spare. `petPanel` grew 40 → 52
   (`BAR_HEIGHT*2 + MARGIN + PET_BUTTON_ROWS*PET_BUTTON_H`), bottom edge at
   y=224 at 640x480, four pixels clear of the buff bar — asserted, not eyeballed.
   Restoring the oracle's 2x5 needs a pet window that is not stacked in that
   column (resolution-aware HUD, or the oracle's own free-floating placement:
   `PetInfoWindow <Location><X>50</X><Y>160</Y>`, `RelativePosition false`).
   That is **B366**, for Will.

What IS the oracle's here: the row-major order, the cell aspect being wider than
tall, the 2px frame inset (the oracle's own `Location.X` for column 0), and the
rule that the whole grid lives inside the window. `PetLayout::buttonRect()` is
the only place a pet button rect is computed, and `UILayout::computeLayout()`
sizes the panel from the same two constants — so the grid cannot fall out of the
frame again the way the old text run did.

### Proof

| step | result |
|---|---|
| **checkout** | `task/T419-recipe-list-pet-buttons` off `fable` at **15b954df**, verified |
| **`nm` alpha companion** | `gles2SetAlphaCompanion` present in slot3's `libIrrlicht.a` (1 match) — no Irrlicht rebuild needed |
| **pre-fix red, Leg A** | production reverse-applied via exported patch (`git apply -R`, never `git stash`) + the two new production files held aside; `g++ -std=c++17 -fsyntax-only tests/test_recipe_list*.cpp` → **exit 1**, `fatal error: client/graphics/ui/pet_layout.h: No such file or directory`. None of `DecodeRecipeReply`, `applyTradeskillRecipeReceived`, `hitTestTradeskillRecipe` or the thirteen new rects existed. |
| **pre-fix red, Leg B (behavioural)** | production restored, then ONLY `UILayout::computeLayout`'s petPanel line reverted to `BAR_HEIGHT*2 + MARGIN*3`; rebuilt and ran → **3 of 5 FAILED**: `EveryButtonRectLiesInsideThePanel`, `TheTenthButtonHasItsOwnCell`, `ThePanelIsSizedFromTheGridsOwnConstants` (`p.getHeight()` 40, wanted 52). That is B352 stated as an assertion. |
| **build** (`build`, RelWithDebInfo, GLES2 config) | exit **0** (background + exit file, `wait_for.sh --exit-file`, unit-private scratch dir) |
| **post-fix tests** | `test_recipe_list` **12/12 PASSED**; `test_pet_buttons` **5/5 PASSED** |
| **`ctest -j6`** | **4307 / 4309.** The two failures are exactly the two the dispatch named as expected while T417 (slot1) replaces the shared pack: `PackReproducible`, and `visual_gate` on `eqe1MappedBytes` only (qeynos2 122665822→123015486, gfaydark 121331914→121681578, befallen 115816410→116166074 — **+349,664 in all three**, which is T417's spell sprite sheet plus its ETC1 companion, per `main/CLAUDE.md`). Nothing else red. |
| **visual gate** (standalone, after `build-gles2-x86.sh` exit 0) | **FAIL, and only on those same three `eqe1MappedBytes` lines.** CPU p95 all well under ceiling: qeynos2 5.455 (10.108), gfaydark 5.837 (10.672), befallen 6.366 (10.948). **No image golden moved** — `git status tests/goldens/ benchmarks/` is empty. |
| **ARM** (`build-arm-noble.sh`) | exit **0**; `build-arm-noble/bin/willeq` = ELF 32-bit LSB pie, ARM EABI5 |
| **file sizes** | `check_file_sizes.sh fable`: 22 changed files, **22 ok / 0 warn / 0 fail**; the three new files checked explicitly, **3 ok** (`pet_layout.h` 125, `tradeskill_recipe_list.cpp` 233, and the tests 354 + 150). Substantial new code went in NEW TUs. |
| **doc counts** | `check_doc_counts.py` failed on `RendererIntent` 36→37 (the new intent) and was corrected; event count 97→99 updated in the three docs that state it. Final: **OK: every stated count matches the tree.** |
| **captures** | `/mnt/projects/fable/review/T419/` — `after/` (all 17 panels, incl. the NEW `tradeskill_800x600.png`), `before/pet_800x600_prefix.png`, `crops/pet_before_after_4x.png`, `crops/tradeskill_after_2x.png`, and a README saying what to look at |

### Surprises

- **The board issued `B335` twice on the same day** — T414 Leg A for four inert
  `weather_effects.json` storm keys, T415 Leg A for the tradeskill window's left
  side. Only file position distinguishes them. Struck the T415 one; boarded the
  collision as **B367**.
- **`RecipeReply_Struct` is smaller than the plan assumed** (no tradeskill, no
  component names) — see the Leg A deviation above.
- **T415's `scaled()` cannot tile a run of rects.** Independent integer scaling
  of origin and size leaves 1px gaps/overlaps between adjacent rows. Only shows
  up when something *is* a run, which the container half never was.
- **`SendRecipesFavorite` is where a favourites list is asked for, and it has
  no production caller other than `/recipefavorites`** — so the `Cleared`
  publisher on that path is reachable but only by that command. Not a defect,
  just narrow.
- **The oracle draws the pet's NAME on the HP gauge** (`PIW_PetHPGauge
  <Text>No Pet</Text>`), where fable uses a separate name row above a separate
  bar. Left alone — out of scope for B352, and changing it would have cost the
  14px the grid needed. Worth knowing if B366 is ever taken up.
