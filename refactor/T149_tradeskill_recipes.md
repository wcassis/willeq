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

Executed in `/mnt/projects/fable/wt/slot2`, task branch
`task/T149-tradeskill-recipes`, off `fable` tip `72169720` (where this plan
exists). **No eqemu lock — T148 holds it. Nothing in this unit was observed on
the wire.** See "What is and is not proven" below before reading anything else
here as an acceptance claim.

### No STOP: the plumbing to reach the open container already existed

The plan's first STOP condition was "reaching the open container's
`object_type` from the action layer needs new cross-layer state". **It does
not.** Two public `EverQuest` accessors, both pre-existing, are all that was
needed:

- `EverQuest::GetActiveTradeskillObjectId()` (`eq.h:932`) returns
  `m_active_tradeskill_object_id`, which `ZoneProcessClickObjectAction()`
  (`eq_entities.cpp:2450`) sets when the **server** sends
  `OP_ClickObjectAction` with `open==1`, and clears on `open==0`. That is the
  same server round trip T144 accepted on.
- `EverQuest::GetWorldObject(drop_id)` (`eq.h:931`) returns the
  `WorldObject` whose `object_type` T144 made correct.

`EqActionHandler` holds an `EverQuest&`, so
`EqActionHandler::resolveOpenContainerObjectType()` is a six-line lookup
against state the production packet path already maintains. **No parallel
state store was invented, and no `object_type` argument appears anywhere in
the action stack** — not in `CommandProcessor`, not in `ActionDispatcher`, not
in `IActionHandler`. A user cannot type it, because there is nowhere to type
it.

### The four commands: full stack

| command (aliases) | `CommandProcessor` | `ActionDispatcher` | `EqActionHandler` | `EverQuest` sender | object_type source |
|---|---|---|---|---|---|
| `recipesearch` (`recipes`) | `cmdRecipeSearch` | `recipeSearch(query, min, max)` | `recipeSearch(...)` | `SendRecipesSearch(type, query, min, max)` | open container |
| `recipedetails` (`recipeinfo`) | `cmdRecipeDetails` | `recipeDetails(id)` | `recipeDetails(id)` | `SendRecipeDetails(id)` | n/a on the wire; gated on an open container |
| `recipecombine` (`autocombine`) | `cmdRecipeAutoCombine` | `recipeAutoCombine(id)` | `recipeAutoCombine(id)` | `SendRecipeAutoCombine(type, id)` | open container |
| `recipefavorites` (`recipefav`) | `cmdRecipesFavorite` | `recipesFavorite(ids)` | `recipesFavorite(ids)` | `SendRecipesFavorite(type, ids)` | open container |

Registered in `action::CommandProcessor` **only** — nothing was added to the
legacy `game` registry (T97's shadowing bug). Category `Tradeskill` (new
category; the four are the only members).

All registrations, virtuals and implementations were **appended** into the
existing Tradeskill sections next to T144's `clickWorldObject`/
`tradeskillCombine`, not at the end of the files, precisely so T148's and
T150's appends land elsewhere. `git diff --stat 72169720` is 10 files,
**621 insertions / 8 deletions**, the 8 deletions all inside
`SendRecipesFavorite` (below). No reorder, no reflow.

### FINDING 1 — `SendRecipesFavorite` could never have worked (source-verified)

`SendRecipesFavorite` wrote a **variable-length** body of
`8 + 4*favorites.size()` bytes. eqemu's handler begins with a hard equality
gate (`zone/client_packet.cpp`, `Handle_OP_RecipesFavorite`):

```
if (app->size != sizeof(TradeskillFavorites_Struct)) { LogError(...); return; }
```

and `TradeskillFavorites_Struct` is `{ uint32 object_type; uint32 some_id;
uint32 favorite_recipes[500]; }` == **2008 bytes, fixed**
(`common/patches/titanium_structs.h:2779`). Every packet that function was
capable of producing — for any favorites list, including the maximum 500 —
would have been dropped by the server with a log line and no reply. It had
zero production callers, so nothing ever noticed.

**Fixed in this unit** (`src/client/eq_merchant.cpp`, my territory): the body
is now always the full 2008 bytes, zero-padded; eqemu skips zero entries when
building its id list. `some_id` stays 0, which is what eqemu requires for a
**world** combiner.

This is a *sender* bug, not one of the two parsers the plan's STOP condition
guards — wiring a command that the server is guaranteed to reject would have
been the exact "technically wired, practically unreachable" defect this arc
exists to remove. It is reported here rather than absorbed silently.

The other three senders were checked the same way and are **correct**:

| opcode | eqemu size gate | what we send | verdict |
|---|---|---|---|
| `OP_RecipesSearch` | `== sizeof(RecipesSearch_Struct)` (80) | 80-byte body | OK |
| `OP_RecipeDetails` | `>= sizeof(uint32)` | 4-byte body | OK |
| `OP_RecipeAutoCombine` | `== sizeof(RecipeAutoCombine_Struct)` (20) | 20-byte body | OK |
| `OP_RecipesFavorite` | `== sizeof(TradeskillFavorites_Struct)` (2008) | **was 8+4n** | **FIXED** |

### FINDING 2 — a search with the naive trivial range returns nothing

eqemu's search SQL (`Handle_OP_RecipesSearch`) filters
`tr.trivial >= mintrivial AND tr.trivial <= maxtrivial`. **A `maxtrivial` of 0
matches no recipe at all.** The obvious call —
`SendRecipesSearch(type, query, 0, 0)` — silently returns an empty result set
and looks exactly like "the feature doesn't work".

`cmdRecipeSearch` therefore defaults to `min 0, max 1000` (Titanium's own
tradeskill window ships a trivial slider; this is the equivalent), overridable
with `--min N` / `--max N`. Pinned by
`T149_RecipeSearch_DefaultTrivialRangeIsNotZeroToZero`, whose whole point is
that a future "simplification" to 0/0 breaks a test instead of the feature.

### The two parsers: verified against eqemu source, field by field

Both were checked directly against `common/patches/titanium_structs.h` (the
Titanium patch structs, not the internal ones) and against the servers' own
send sites. **Both are clean.**

| our struct | eqemu struct | fields | size | send site |
|---|---|---|---|---|
| `RecipeReply_Struct` | `titanium_structs.h:2798` | `object_type, some_id, component_count, recipe_id, trivial, char recipe_name[64]` — identical order and widths | 84 == 84 | `Client::SendTradeskillSearchResults`, `new EQApplicationPacket(OP_RecipeReply, sizeof(RecipeReply_Struct))` |
| `RecipeAutoCombine_Struct` | `titanium_structs.h:2809` | `object_type, some_id, unknown1, recipe_id, reply_code` — identical | 20 == 20 | `Object::HandleAutoCombine`, same `sizeof` |
| `RecipesSearch_Struct` (send) | `titanium_structs.h:2786` | `object_type, some_id, mintrivial, maxtrivial, char query[56], unknown4, unknown5` — identical | 80 == 80 | — |

`ZoneProcessRecipeAutoCombine` treats `reply_code == 0` as success; eqemu's
default-fail value is `0xFFFFFFF5` and success overwrites it with 0, so the
test is right.

**This is source verification, not byte verification.** T144's standing lesson
is that a struct agreeing with source can still disagree with the wire —
`Object_Struct` did, for the life of the repo, through a passing
`static_assert`. Neither parser has ever been reached in production and
neither is byte-pinned in `tests/captured_packets.h`. That capture is owed
(below).

### What is and is NOT proven

**Proven, by tests, against real captured bytes and a real `EverQuest`:**

- `OP_GroundSpawn` (T144's captured qeynos forge, `drop_id=190`) →
  `m_world_objects[190].object_type == 17` → `OP_ClickObjectAction open=1` →
  `GetActiveTradeskillObjectId() == 190` → the resolver finds type 17. The
  whole chain the design point rests on, driven through the production replay
  seam (`test_eq_action_handler_tradeskill.cpp`, 5 cases).
- No container open → all four commands refuse, tell the player why, and send
  nothing. Container closed again → refuses again. Container open per the
  server but with no ground-spawn record → **reported, not guessed** (never
  fabricate an `object_type`).
- All four commands are registered in `action::CommandProcessor` under
  `Tradeskill` with their aliases, parse their arguments, and reject
  malformed input without dispatching
  (`test_command_processor.cpp`, 16 cases).

**NOT proven — no eqemu lock, nothing was observed on the wire:**

- No recipe search has ever been sent to a server, and **no
  `OP_RecipeReply` has ever been received by this client**. The two parsers
  remain unreached in production exactly as they were before this unit.
- The 2008-byte favorites fix is verified against eqemu's size gate in source.
  No favorites packet has been sent.
- No auto-combine has been attempted, and no component set was assembled.
  Per the plan: **reported unexercised, not manufactured.**
- The senders queue onto `m_zone_connection`, which is null in the replay
  harness, so the tests assert the *resolution chain*, not the bytes. The
  test file's header says so in as many words.

**Tradeskill does not "work now".** T144 made containers openable; T149 makes
the four recipe requests reachable and correct as far as source and tests can
establish. Whether a real forge answers a real search is an open question that
this unit did not and could not answer. T145 is the precedent.

### Owed follow-ups (name these as units, do not fold them into a merge note)

1. **The wire leg.** With the eqemu lock: open a real qeynos forge
   (`/clickobject 190`, T144's exact scenario), `/recipesearch`, and confirm
   real server-populated `OP_RecipeReply` packets arrive and parse. Then
   `/recipedetails` on one of the returned ids, and `/recipefavorites` with
   those ids to prove the 2008-byte fix. Auto-combine if components can be
   assembled (a GM `#si` of the components makes this cheap).
2. **Capture the reply bytes into `tests/captured_packets.h`.** The moment an
   `OP_RecipeReply` is on the wire, that unit should pin it the way T144
   pinned `OP_GroundSpawn` — these are two more structs currently guarded only
   by a `static_assert` that agrees with the code it guards.
3. **`OP_RecipeDetails` has no reply handler.** eqemu answers a details
   request on the *same opcode* (`Client::SendTradeskillDetails`,
   `tradeskills.cpp:879`) with a variable-length body: a **big-endian**
   `recipe_id`, ten `0xFFFFFFFF` slots that get memmove'd over, then
   `{be32 item_id, be32 icon, NUL-terminated name}` per component. Our zone
   dispatcher has no `case HC_OP_RecipeDetails`, so the reply is silently
   dropped and `/recipedetails` prints nothing. **Deliberately not written
   here**: that format is exotic enough (byte-order flips, variable length,
   trailing-block memmove) that a parser written from source without bytes is
   exactly the class of code T144 proved unreliable. It should be written
   against a capture, in the wire-leg unit.
4. **Inventory tradeskill containers are out of scope.**
   `m_active_tradeskill_object_id` tracks **world** containers only. An
   inventory container (mixing bowl, sewing kit) needs `some_id` set to the
   container's item id — every sender here leaves `some_id` at 0, correct for
   a world combiner and wrong for an inventory one. Recorded in the handler's
   comment; a real unit, not a tweak.

### Verification

| step | result |
|---|---|
| 1. `cmake --build build -j24` | PASS |
| 2. `ctest --test-dir build --output-on-failure` | **2478/2479**, sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds` |
| 3. `bash scripts/build-gles2-x86.sh` | PASS |
| 4. visual gate | **PASS**, goldens untouched (`git status` shows only the 10 source/test files + the new test file) |
| 5. `bash scripts/build-arm-noble.sh` | PASS |
| 6. `check_file_sizes.sh 72169720` (from slot2, on task branch) | 9 files checked, **0 FAIL**, 5 WARNs — see the note below |

**Suite arithmetic.** The plan's stated baseline of 2457 is off by one: the
same tip `72169720` checked out at `/mnt/projects/fable/main` reports
`Total Tests: 2458`. 2458 + 21 (16 command-processor + 5 action-handler) =
**2479**, which is exactly what this branch reports. No test was lost or
double-counted.

**File-size WARNs.** Four of the five (`action_dispatcher.cpp`,
`command_processor.cpp`, `eq_merchant.cpp`, `test_command_processor.cpp`) were
already over 1500 before this unit. **One is new and is flagged, not
absorbed**: `src/client/eq_action_handler.cpp` crossed the line, 1473 → 1512.
Splitting it was not attempted — it is the canonical home for every
`IActionHandler` override and carving out four of them would fragment the
file's one job. Note also that
`check_file_sizes.sh fable` now compares against a **moved** `fable`
(`fd09ceee`, T150's work merged mid-unit) and so reports files this unit never
touched; run it against `72169720` for a true reading of this branch.

### Parallel-unit boundary (T148, T150)

Held. `git diff --stat 72169720` touches exactly: the four `action/` files,
`eq_action_handler.{h,cpp}`, `eq_merchant.cpp`, and three `tests/` files.
`src/client/eq_gm.cpp`, `src/client/eq.cpp` (T148),
`src/client/eq_player_info.cpp`, `src/client/eq_group.cpp` (T150) and
`include/client/eq.h` are **untouched** — `eq.h` needed no change at all,
since both accessors the design point relies on already existed.
`tests/CMakeLists.txt` gains **one appended block** at the end of the file.

### Deviations

1. **`SendRecipesFavorite`'s packet body was rewritten** (Finding 1). This is
   a behavioral change to an existing sender, not pure wiring. It is inside
   the plan's stated territory (`src/client/eq_merchant.cpp`) and the sender
   had zero production callers, so nothing can regress; but the plan asked for
   four senders to be *called*, not fixed, so it is flagged. Not
   byte-verified — see "owed follow-ups".
2. **A new test TU, `tests/test_eq_action_handler_tradeskill.cpp`**, plus one
   appended `tests/CMakeLists.txt` block. `test_command_processor.cpp` can
   only assert against a recording fake, which by construction cannot show
   that `object_type` is resolved *below* the action layer — the one claim
   this unit most needs to substantiate.
3. **`/recipedetails` is gated on an open container** even though
   `OP_RecipeDetails` carries no `object_type` on the wire. Ids come from a
   search reply, and a details lookup with no container open is meaningless;
   the gate keeps all four commands' preconditions uniform. Flagged because it
   is a choice, not a wire requirement.
4. **No wire leg, by instruction** (T148 holds the lock). Stated
   throughout rather than papered over.
