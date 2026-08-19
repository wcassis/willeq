# T149 — Finish the tradeskill feature T144 half-unblocked

**Slot: slot2. x86. NO eqemu lock (T148 holds it). No Pi.**

**Territory: `src/client/eq_merchant.cpp`, `include/client/eq.h` (recipe
declarations only), `src/client/action/`, `include/client/action/`,
`src/client/eq_action_handler.{h,cpp}`, `tests/`, `refactor/`.**

**T148 (slot1) and T150 (slot3) run in parallel.** T148 owns
`src/client/eq_gm.cpp` and `src/client/eq.cpp`; T150 owns
`src/client/eq_player_info.cpp` and `src/client/eq_group.cpp`. All three edit
`src/client/action/*` and `eq_action_handler.*` — **append your registrations,
do not reorder or reflow existing ones**, and keep `tests/CMakeLists.txt` to a
single appended block.

Read T132's Review (the net-new action-stack recipe, ~50 minutes measured) and
T144's (which built the half of this feature you are completing).

## Why this unit is the highest value in the arc

**T144 just made tradeskill containers openable.** Before it, `Object_Struct`
was 8 bytes wrong, `object_type` was read past the end of the real struct and
came out `0` every time, so `isTradeskillContainer()` was false for every object
in the world and `IrrlichtBridge` registered **zero** click volumes. T144 fixed
that and proved it live: `/clickobject 190` →
`Clicking object 190 ('IT66_ACTORDEF') type=17 (Forge)` → server answers
`Opening tradeskill container: drop_id=190`.

So the container opens — and then nothing can happen, because all four recipe
senders have **zero production callers** while **both recipe parsers are already
wired into the dispatcher**. The feature is exactly half-built, and you are
building the half that makes the other half worth having.

## The four senders

`include/client/eq.h:1088-1091`, defined in `src/client/eq_merchant.cpp`:

| sender | signature | line |
|---|---|---|
| `SendRecipesFavorite` | `(uint32_t object_type, const std::vector<uint32_t>& favorites)` | :1547 |
| `SendRecipesSearch` | `(uint32_t object_type, const std::string& query, uint32_t mintrivial, uint32_t maxtrivial)` | :1565 |
| `SendRecipeDetails` | `(uint32_t recipe_id)` | :1587 |
| `SendRecipeAutoCombine` | `(uint32_t object_type, uint32_t recipe_id)` | :1599 |

Already-wired receive side: `ZoneProcessRecipeReply` (`:1618`) and
`ZoneProcessRecipeAutoCombine` (`:1631`).

**Verify those two parsers against eqemu before you rely on them.** They have
never been reached in production — nothing has ever sent the request that
provokes the reply — so they are unvalidated by construction. `RecipeReply_Struct`
and `RecipesSearch_Struct` were checked field-by-field by the T147 sweep and
found clean, but that sweep read source, not bytes. If a parser is wrong you
will find out as garbage in the reply; treat that as a finding, not a blocker,
and report it rather than silently patching around it.

## The work

Full action stack per T132's recipe: `CommandProcessor` registration →
`ActionDispatcher` method → `EqActionHandler` → existing sender. **Register in
`action::CommandProcessor` only**, never the legacy `game` registry (T97's
shadowing bug).

`object_type` is the container's type, which is exactly what T144's
`WorldObject::object_type` now carries correctly — **source it from the open
container, not from a user-typed argument**, so the commands compose with T144's
work instead of duplicating it. If the plumbing to reach the currently-open
container from the action layer does not exist, **say so and report the gap
rather than inventing a parallel state store** — that is a real design question
and belongs to Will, not to this unit.

## Acceptance — AT THE WIRE

A recipe search that returns real server-populated recipes for a real container,
and a details lookup on one of them. Auto-combine if you can assemble the
components; if you cannot, **report it unexercised rather than manufacturing a
scenario**.

You have no eqemu lock — **T148 holds it and you cannot take it.** So this unit
ends verified against source and by tests, NOT on the wire, exactly as T145 did.
**Be explicit about that in the Review and do not write "tradeskill works now."**
The wire leg is a follow-up unit; name it as owed.

## Verification

Full protocol bar. Suite baseline 2457 (tip `359f3647`), sole expected failure
the documented ALSA `AudioManagerTest.PreloadCommonSounds`.

## STOP conditions

- Reaching the open container's `object_type` from the action layer needs new
  cross-layer state (see above).
- A recipe parser turns out to be wrong — report the evidence, do not redesign
  the parse path inside this unit.

## Review

(executor fills)
