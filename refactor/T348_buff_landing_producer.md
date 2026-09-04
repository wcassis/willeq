# T348 — The buff-landing producer (B055 reopened) and the racial vision floor (B056)

Class A, both measured live by T345. Slot2.

## B055 — measured cause

`ZoneProcessBuff` calls `setPlayerBuff()` (the only production caller
that fires `BuffManager`'s apply hook) only on an `OP_Buff` with
`fade != 1`; eqemu never sends that shape (server-side the only
`bufffade = 0` sender is gated on `zone->BuffTimersSuspended()`). The
route that DOES deliver a landed self-buff on this server is the one
T345 saw: `#castspell` produced two `SpellAction` packets and the buff
appears in the next zone-in PlayerProfile → `setPlayerBuffs()` (plural),
which does not fire the callback.

Measure first, from T345's logs (`review/T345/`) and a packet read of
`src/client/player/player_buffs.cpp` / `spell_manager.cpp`: on a
self-cast that lands, which packets arrive in order (`OP_Action`,
`OP_Buff` with `fade=1`?, `OP_ManaChange`, …) and which handler updates
the buff list. The producer goes where the buff list is actually
updated on landing: if that is `setPlayerBuffs()` (plural, zone-in and
possibly the post-cast refresh), it fires the apply hook per buff added
(diff old vs new lists); if landing is delivered through `OP_Action`'s
buff-effect arm, that arm calls `setPlayerBuff()`. Match what the
official client would do: it shows the buff icon the moment the cast
lands, so whichever packet carries the landing is the trigger.

Test: replay the T345 cast log's packets (`zonetest::ReplayZone`) and
assert `VisionChanged` publishes Ultravision after the landing packet;
pre-fix failure pasted. T339's tests keep passing.

## B056 — the racial vision floor

`PlayerCameraComponent::setPlayerRace()` logs at INFO on every call and
that line appears in none of T345's five session logs; its only call site
is `IrrlichtRenderer::createEntity()` while the live path builds entities
through `src/client/graphics/zone_load/renderer_zone_load_entities.cpp`.
Measure: is `setPlayerRace` reached on the live path at all (add the call
where the player's own entity is created on the zone-load path if not),
and what the racial floor should be (the race table in `renderer_camera.cpp`).
Test: after a zone-in with a Dark Elf / Human, `getVisionType()` is the
racial floor (Ultravision / Normal) before any buff. Pre-fix failure.

Live proof: owed — a night session after both fixes, `#castspell 46`
with `invulnerable=0` (T345 found an invulnerable account refuses its
own cast), before/after frames; `review/T348/` if the lock is free.

Territory: `src/client/player/player_buffs.cpp`, `src/client/spell/`
(the landing handler), `src/client/graphics/zone_load/renderer_zone_load_entities.cpp`
(one call), `src/client/graphics/player/renderer_camera.cpp` (the race
table only if wrong), tests, `refactor/BOARD.md`. A graphics file
changes: visual gate + ARM NOT skippable.

STOP: the landing packet cannot be identified from the logs (then a
capture unit with the official client comes first — say so); a
state-module change beyond a publish.
