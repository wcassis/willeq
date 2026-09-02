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
