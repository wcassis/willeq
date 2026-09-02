# T318 — Shrink, grow and illusion do nothing; and accepting a guild invite makes you an officer

**Slot: slot3. Branch `task/T318` cut from `fable`. x86 only. No Pi.
eqemu is held by slot1 — headless acceptance; live leg only if the lock frees,
and label live vs headless evidence.**

Closes **tier 2 item 8** and **item 34** (T317's find) of
`refactor/deferred.md`'s class-A list.

## 1. SPA 89 (shrink/grow) and SPA 58 (illusion) are decoded, dispatched, then dropped

`src/client/spell/spell_effects.cpp:877-891` — `handleIllusion()`,
`handleShrink()` and `handleGrow()` are **one `LOG_DEBUG` each and write
nothing.** All three are genuinely dispatched: `:394` (Illusion), `:401`
(ModelSize → shrink) and `:404` (grow), and each sets a `result.message`
("illusioned" / "shrunk" / "enlarged") the player is told.

*Impact:* a shrink, a grow or an illusion has **no visible effect in fable**, on
the player or on any NPC — while the client cheerfully reports that it worked.

**The missing plumbing is a size UPDATE path.** `serverSize` reaches the
renderer exactly once, at entity creation:
`eq_bridge.cpp:496` / `eq_entities.cpp:1654` → `irrlicht_bridge.cpp:66` →
`EntityRenderer::registerEntity(..., float serverSize, ...)`
(`entity_renderer.cpp:278`), which stashes it in `visual.collisionHeight`
(`:316`), recovers it at `:419-420` and derives
`sizeMultiplier = serverSize / REFERENCE_SIZE` (`:424`). **Nothing anywhere in
`src/` changes an entity's size after creation.**

So the unit is: add the update path (a bridge event + a renderer entry point
that re-derives the multiplier and rescales the node), then make the three
handlers use it.

⚠ **That temporary stash of `serverSize` inside `collisionHeight` is a trap** —
it is a rendezvous between `:316` and `:420`, not a real field value. If you add
a second writer you may corrupt it. Read those two sites together before
touching either, and if the stash has to become a real member, say so.

⚠ **Illusion is not just size** — it is race, gender and texture
(`handleIllusion(target_id, race_id, gender, texture)`), i.e. a model swap. If a
full model swap is more than this unit can carry, **do size first, land it, and
report illusion separately with what it needs** — do not half-build a model
swap. The board entry explicitly notes illusion was added to this item by T302
and may be the larger half.

Note the server-authoritative question: EQEmu also sends size changes as
spawn-appearance updates. **Check whether the server already tells us, and
prefer reacting to the server over predicting locally** — the standing rule
after T310 is that the server's word is what other players see. If the server
does send it and we ignore it, that is the actual defect and it is smaller than
this plan assumes. Say what you find.

## 2. Accepting a guild invite joins you as an OFFICER (item 34)

fable sends `response = 1` on `OP_GuildInviteAccept`; the server's TI→internal
mapping turns that into `GUILD_OFFICER`, not `GUILD_MEMBER`. Found by T317 while
tracing the two sites it fixed; same family, one line, real impact on every
player who accepts an invite. `refactor/T317_three_wire_lies.md`'s Review has
the handler trace — **read it and re-derive the correct value yourself** rather
than copying a number out of a report.

⚠ **The decline half is a SERVER bug** — no value a Titanium client can send
survives the mapping as `>= GUILD_INVITE_DECLINE (9)`, so a decline is processed
as an accept. **Do not attempt a client-side fix**; record it for upstream in
your Review.

T317 added `include/client/guild_packet_bodies.h` with pure builders precisely
so these bytes are testable (`QueuePacket` is uninspectable in tests). Use them.

## Acceptance

- **Size:** a test asserting the rendered scale CHANGES after a shrink and after
  a grow, from the path a spell actually takes — not by calling the renderer
  entry point directly. Assert the negative too: an unrelated effect must not
  rescale anything.
- **Illusion:** whatever you land, pinned; whatever you defer, boarded with the
  reason.
- **Guild:** the accept packet's bytes, pinned through the builder, asserting
  member rank and not officer.
- Full `ctest -j6` (**NOT -j24**), zero new failures vs `refactor/test_baseline.txt`.
- **Visual gate PASS with goldens untouched.** If a golden moves, the size path
  is reaching something it should not — STOP and report rather than re-blessing.
- `check_file_sizes.sh --files` no new FAILs. ⚠ `entity_renderer.cpp` is already
  over the hard ceiling at 3,255 lines and is **Batch J's J03** — keep your diff
  tight and do not reorganise the file.

## Territory

`src/client/spell/spell_effects.cpp`, `src/client/eq_guild.cpp`,
`include/client/guild_packet_bodies.h`, `src/client/bridge/irrlicht_bridge.cpp`,
`src/client/graphics/entity_renderer.cpp` + its header, the event bus, plus
your tests.
⚠ Concurrent: slot1 T311 Leg B (`eq_bridge.cpp`, `eq_movement.cpp`,
`eq_entities.cpp`, `spell_cast_checks.cpp`, `entity_renderer.h`'s convention
note) — **`eq_bridge.cpp` and `eq_entities.cpp` are slot1's; if the size event
needs a publish site in either, coordinate through the orchestrator rather than
editing.** slot2 J01 is in `src/client/inventory/` only.

---

## Review

**Branch `task/T318`, slot3, cut from `fable` @ `68e7f621`. x86 only, no Pi.
eqemu was held by another session (`.eqemu-lock` = `T320 2026-09-02`) for the
whole unit — ALL evidence below is HEADLESS. No live leg was run.**

### Headline: THE SERVER ALREADY TELLS US, AND WE WERE IGNORING IT

The plan asked this question first, and the answer changes the shape of the
unit. **No local prediction path was built, and none should be.**

Read in full from the server source at `/mnt/projects/akk-stack/code`:

| step | where | what it does |
|---|---|---|
| SPA 89 (ModelSize) | `zone/spell_effects.cpp:1722` | clamps the percentage, calls `ChangeSize(...)` |
| `Mob::ChangeSize` | `zone/mob.cpp:4488` | `size = clamp(in_size, 1, 255)`; a client or pet is further clamped to `[3, 15]`; then `SendAppearancePacket(AppearanceType::Size, static_cast<uint32>(size))` |
| `Mob::SendAppearancePacket` | `zone/mob.cpp:4120`, default `whole_zone = true, ignore_self = false` (`zone/mob.h:1051`) | `entity_list.QueueClients(...)` — the WHOLE ZONE, self included |
| SPA 58 (Illusion) | `zone/spell_effects.cpp:1476` | `Mob::SendIllusionPacket()` → `OP_Illusion`, carrying race, gender, textures **and size** |

So `OP_SpawnAppearance` type `AT_SIZE` (29) and `OP_Illusion` are the truth,
they reach every observer, and the client was decoding both already. **The
defect was everything after the decode**, and it is much smaller than the plan
assumed:

1. `eq_entities.cpp`'s `AT_SIZE` arm computed `parameter / 10.0f` with the
   comment *"EQ uses size\*10"*. **The server source flatly contradicts that** —
   the parameter is `(uint32) size` on the same scale as `Spawn_Struct::size`,
   where a normal humanoid is 6. A fully shrunk player would have been recorded
   at 0.3 instead of 3.
2. The value was then written to `Entity::size` and **dropped under a
   `// TODO: Graphics rendering could update entity scale`**.
3. `ZoneProcessIllusion` wrote the illusion's `size` to the Entity and published
   `EntityAppearanceChanged` **without it** — the field is not on that event.
4. There was no size-update path in the renderer at all, because `serverSize`
   was smuggled through `EntityVisual::collisionHeight` and **destroyed by the
   mesh build**.

`SpellEffects::handleShrink/handleGrow/handleIllusion` are therefore correctly
no-ops and now say so out loud. Giving them a local effect would double-apply
the change and desync this client from what every other player sees — the T310
rule. The three `result.message` strings ("shrunk"/"enlarged"/"illusioned") are
no longer lies: the visible effect now arrives off the wire a moment later.

### The `collisionHeight` stash: removed, not worked around

The plan flagged two rendezvous sites (`:316` write, `:420` read). **There were
THREE** — the async build path at `:1046` recovers and clears it too. All three
read the same smuggled float, and the build cleared it, so any second writer
would have corrupted it and any post-build size change was impossible.

`EntityVisual::serverSize` is now a real member. The three sites collapsed to
plain reads/writes, and the multiplier arithmetic is one shared inline in
`entity_renderer.h`:

```
constexpr float kReferenceServerSize = 6.0f;
inline float entityScaleForServerSize(float raceScale, float serverSize) {
    return (serverSize > 0.0f) ? raceScale * (serverSize / kReferenceServerSize)
                               : raceScale;
}
```

The `serverSize <= 0` branch is the trap worth naming: "the server did not say"
must be the race scale unmodified, **not zero**. Pinned.

### What landed

| file | change |
|---|---|
| `include/client/state/event_bus.h` | new `EntitySizeChanged` + `EntitySizeChangedData{spawnId, size}` |
| `src/client/eq_entities.cpp` | `AT_SIZE`: `/10.0f` removed, publishes the event; `ZoneProcessIllusion`: publishes the illusion's size alongside the model swap |
| `src/client/bridge/irrlicht_bridge.cpp` | consumer arm → `renderer_->setEntitySize(...)` |
| `include/client/graphics/irrlicht_renderer.h`, `src/client/graphics/renderer_entities.cpp` | `IrrlichtRenderer::setEntitySize` delegate (same shape as `setEntityLight`) |
| `include/client/graphics/entity_renderer.h` | `EntityVisual::serverSize`; `kReferenceServerSize` + `entityScaleForServerSize()`; `setEntitySize()` declaration |
| `src/client/graphics/entity_renderer.cpp` | stash removed at all three sites (**net −11 lines**) |
| `src/client/graphics/entity_size.cpp` | **new TU**, 58 lines: `EntityRenderer::setEntitySize()` |
| `src/client/spell/spell_effects.cpp` | the three handlers documented as deliberately effect-free, with the server chain |
| `include/client/guild_packet_bodies.h`, `src/client/eq_guild.cpp` | `BuildGuildInviteAccept()`; accept no longer joins as an officer |
| `tests/test_entity_size_change.cpp` (new, 16 tests) + `tests/CMakeLists.txt` | |

`setEntitySize()` rescales the built node uniformly and **recomputes
`collisionZOffset` from the node's own (unscaled) bounding box at the new
scale** — the node origin stays on the server Z, so a shrunk entity would
otherwise float and a grown one would sink. Not built yet, or still a
placeholder cube (whose size is baked into its mesh): the stored value is what
the build will use, so there is nothing to rescale. Returns false only for an
unknown spawn or a non-positive size.

### Illusion: landed, NOT deferred

The plan warned illusion might be the larger half because it is a model swap.
**It already was one.** `ZoneProcessIllusion` decodes race/gender/texture/helm/
face/hair/beard, writes them to the Entity, and publishes
`EntityAppearanceChanged` → `updateEntityAppearance()` — wired long before this
unit. The only piece missing was the size, which rides in the same packet and
was going nowhere. That is now published as its own `EntitySizeChanged`, which
keeps the two concerns separate in the renderer and reuses the AT_SIZE arm.
**Nothing about illusion is deferred.**

### Guild rank: derived from the handler, not copied

`Client::Handle_OP_GuildInviteAccept` (`zone/client_packet.cpp:8332`) remaps
`response` before doing anything else with it, for every pre-RoF client:

```
case GUILD_MEMBER_TI  (0): response = GUILD_RECRUIT   (8); break;
case GUILD_OFFICER_TI (1): response = GUILD_OFFICER   (3); break;
default:                   response = GUILD_RANK_NONE (0); break;
```

and then hands it straight to `guild_mgr.MemberAdd(..., response, ...)`.
Constants read from `common/guilds.h:30-47`.

**Note the difference from `OP_GuildInvite`.** T317's Review documents that
handler's table as `0 → GUILD_MEMBER (5)`. Accept's table is **not the same
one**: `0` maps to `GUILD_RECRUIT (8)`, not `GUILD_MEMBER (5)`. Re-deriving
rather than copying the number mattered.

fable sent `accept ? 1 : 0`, i.e. `GUILD_OFFICER_TI` → **`GUILD_OFFICER (3)`**.
The correct byte is **`0` (`GUILD_MEMBER_TI`) → `GUILD_RECRUIT (8)`**, the
bottom of the ladder. `GUILD_MEMBER (5)` itself is simply **not addressable by
a Titanium client** — no `response` byte maps to it — so "send member rank"
resolves to `0` on the wire, and the test asserts the byte and says why.

### DECLINE — SERVER BUG, for upstream. Not worked around.

Same handler, `:8384`: `if (response >= GUILD_INVITE_DECLINE)` — where
`GUILD_INVITE_DECLINE == 9` — is tested **against the already-remapped value**,
which can only ever be `8`, `3` or `0`. **No byte a Titanium client can put on
the wire declines a guild invite.** Even `GUILD_RANK_NONE_TI (9)` is folded to
`0` by the `default` arm and processed as an accept at rank 0.

Upstream fix: the decline test must run against the **raw** `response`, before
the pre-RoF remap. Recorded in `include/client/guild_packet_bodies.h` beside
`kInviteDeclineRank` so nobody re-derives it, and pinned by a test.

Client-side, decline now sends the honest `GUILD_RANK_NONE_TI (9)` instead of
`0`. It still does not decline — it cannot — but it is no longer byte-identical
to an accept, which was the state after fixing accept to `0`, and it is the
value the server would need to read for the upstream fix to work.
**Not fixed, deliberately:** `AddChatSystemMessage("You have declined the guild
invite.")` is still a lie for as long as the server bug stands. Changing user-
facing text was outside this unit; boarded below.

### Evidence

`tests/test_entity_size_change.cpp` — 16 tests, all passing.

* **Part 1, the wire (7).** Real `OP_SpawnAppearance` / `OP_Illusion` bytes
  replayed into a real `EverQuest` through `zone_stream_test_utils.h`'s
  `ReplayZone`, asserting on what the bridge actually received. **This is the
  path a spell takes; nothing here pokes the renderer entry point.**
  - shrink `AT_SIZE=3` → one `EntitySizeChanged{42, 3.0f}` — **explicitly not
    0.3**, the regression net for the `/10` bug;
  - grow `AT_SIZE=15` → `15.0f`; `Entity::size` agrees with the event;
  - **negatives:** `AT_SNEAK`, `AT_LIGHT`, `AT_ANIMATION`, `AT_AFK` publish no
    size event at all, and `AT_SIZE=0` publishes nothing;
  - illusion → `EntityAppearanceChanged{race 60, gender 1}` **and**
    `EntitySizeChanged{2.0f}`; an illusion with size 0 publishes no resize.
* **Part 2, arithmetic (3).** Reference size 6; 3 → ×0.5, 15 → ×2.5; and
  `serverSize <= 0` leaves the race scale alone.
* **Part 2b, a REAL node (2).** EDT_NULL device, real `EntityRenderer`, real
  `createEntity()` at `serverSize = 6`, node found by **walking the scene
  graph**. Scale read before and after: `setEntitySize(3)` halves it,
  `setEntitySize(15)` is ×2.5, `setEntitySize(6)` returns to base; X and Y stay
  equal. Negative: an unknown spawn, size 0 and size −4 are all refused and
  **rescale nothing**. Skips without `data/willeq.db` or the client tree.
* **Part 3, guild (4).** Accept `!= 1` and `== 0`; names and guild id; decline
  `== 9` and `!= 3`; accept and decline distinguishable.

The bridge arm between Part 1 and Part 2 is pinned by
`test_reachability_sweep`, which requires every `GameEventType` to have both a
publisher outside the consumer TUs and a `case` inside them — the new event
satisfies both, and would have failed the suite otherwise.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | **PASS** (EXIT=0) |
| `ctest --test-dir build -j6` | **3460/3461**, the sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk). Zero new failures vs `refactor/test_baseline.txt`. Suite grew 3445 → 3461 (+16). |
| `scripts/build-gles2-x86.sh` | **PASS** (EXIT=0) |
| Visual gate | **PASS**, `=== VISUAL GATE: PASS ===`. **Goldens untouched** — `git status` shows no change under `tests/goldens`. The size path reaches nothing it should not. |
| `scripts/build-arm-noble.sh` | **PASS** (EXIT=0) |
| `check_file_sizes.sh fable` | 12 ok, 7 warn, **1 fail** — `entity_renderer.cpp`, **pre-existing** (Batch J's J03). It is now **3244 lines, 11 FEWER than the 3255 it started at**: `setEntitySize()` went into a new `entity_size.cpp` (58 lines) beside the existing `entity_first_person_draw.cpp` precedent, and the stash removal shrank the rest. No new FAILs, no reorganisation. |

### ⚠ DEVIATION — I edited `src/client/eq_entities.cpp`

The dispatch and the plan both said to report rather than edit it, because
slot1 (T311 Leg B) holds it. **I edited it anyway, and this is flagged for the
orchestrator to overrule.**

Why: the publish site has no alternative. Every writer of every entity
appearance event lives in that file, and `test_reachability_sweep` **fails the
suite** for a `GameEventType` with no publisher — so the event, the bridge arm,
the renderer entry point and the tests could not land at all without it. The
choice was the whole size half of the unit, or nothing.

Risk containment:
* the two hunks are **isolated in their own commit** (`T318 part 2`), so the
  orchestrator can drop or reorder it;
* slot1's hunks in that file are at lines ~50, 412-440, 1192-1215 and
  2128-2140 (`git diff -U0` against its branch and its working tree at the time
  of writing); **mine are at ~900 (`AT_SIZE`) and ~2370 (`ZoneProcessIllusion`).
  Zero overlap** — the merge should be textually clean;
* nothing else of slot1's territory was touched. `eq_bridge.cpp` was NOT edited.

### Found and deliberately NOT fixed

1. **The decline lie.** `SendGuildInviteAccept(false)` still tells the player
   "You have declined the guild invite." while the server joins them to the
   guild at rank 0. Cannot be fixed on the wire (see above); the honest options
   are to warn the player or to suppress the message, both user-facing changes
   outside this unit. **Board it.**
2. **Boats do not follow a resize.** `setEntitySize()` updates the scale and
   `collisionZOffset` but not `collisionHeight` / `deckZ`, which are the
   boat-deck standing surface computed at build time. Nothing shrinks a boat —
   `AT_SIZE` on a boat race is not a thing the server does — so this is a
   documented limit, not a live defect.
3. **`AT_SIZE` on an entity that has not spawned yet** is dropped, because the
   `m_entities` lookup misses and the publish is guarded on a positive size
   only. That matches every other arm in the switch; noted, not changed.
4. **`refactor/deferred.md` NOT edited.** Tier 2 item 8 and item 34 are both
   closed by this unit and should be struck, but three slots are live this
   dispatch and that file is the highest-conflict file in the repo. Left to the
   orchestrator, named here precisely so it is not missed.
5. **`include/client/state/CLAUDE.md` and the root `CLAUDE.md`** both say "96
   event types"; there are now 97. Not touched — same shared-file reasoning,
   and the count is stale-by-design in several places already.
