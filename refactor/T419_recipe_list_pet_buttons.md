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
