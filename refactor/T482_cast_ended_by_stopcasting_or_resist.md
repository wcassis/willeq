# T482 — a cast the server ends after BeginCast without an interrupt, or a fully resisted spell, still ends by the client timeout as Success (B631)

Class A (unconfirmed live), **no lock (Leg A)**, slot3. Read T469's Review
(`refactor/T469_cast_interrupt_unhandled.md` — the per-cause packet table and
the deliberate "after BeginCast, keepcasting 0 alone is ambiguous" rule), its
tests (`tests/test_spell_interrupt_wire.cpp`), and BOARD B631/B630. eqemu source
(read-only): `/mnt/projects/akk-stack/code/zone/spells.cpp`, `Mob::SpellOnTarget`,
`Client::CheckManaEndUpdate`.

## The lead (T469)

Two server outcomes still reach fable's `SpellManager::update()` duration+5 s
fallback, which calls `completeCast(true)` — reported as **Success**, recast
started, impact burst painted:

1. **Post-BeginCast `StopCasting()`** (recast timer not expired,
   `CastedSpellFinished` `spells.cpp:1423`, `:1439`): only `OP_ManaChange
   keepcasting 0`. T469 ignores keepcasting 0 after BeginCast because a
   *successful* cast also sends it (`spells.cpp:1817`).
2. **A fully resisted spell** (`Mob::SpellOnTarget` resist branch): establish
   exactly what the caster receives (an `OP_Action`/`OP_Damage`? a resist
   MessageString? the same keepcasting 0?).

## Tasks

1. **Disambiguate from the wire, not the clock.** From eqemu source, list the
   complete packet sequence the caster receives for: success, success-with-resist,
   post-BeginCast StopCasting, and (for reference) interrupt/fizzle. Find the
   packet or ORDER that distinguishes a success from a stop (e.g. success sends
   the spell's `OP_Action`/`OP_BeginCast`-completion/`OP_ManaChange` with the
   mana deducted, a stop sends no action and no mana deduction; or message
   string ids). Cite each line.
2. **Pre-fix failing tests** on T469's wire seam (headless `EverQuest`, real
   packet bytes built from eqemu structs, `ReplayZonePacket`): after BeginCast,
   (a) the StopCasting sequence must end the cast as NOT Success with the gem
   Ready and no recast/impact; (b) the resist sequence must end the cast with
   the result the server implies (a resisted cast still consumes the gem's
   recast in eqemu? — establish it) and never paint an impact on a target that
   resisted; (c) the success sequence must still end as Success with recast (the
   guard).
3. **Fix** using the distinguishing packet(s). The duration+5 s timeout may stay
   only as a last-resort for a genuinely lost packet, and must then log a WARN
   and NOT report Success. Keep T469's before-BeginCast rule.
4. Board the live two-client proof as owed (merge into B630's live leg).

## Proof

Standard verification bar, builds strictly in sequence; G-batch goldens for
every TU touched (`zone_dispatch`, `bridge_component_intents` are pinned —
in-place per G-correction 32, say so); `ZeroCallers`; visual gate; ARM; file
sizes. Board B760–B769; strike/narrow B631.

File territory: `src/client/spell/`, the zone dispatch arms for
ManaChange/Action/BeginCast/MessageString as they touch cast state, spell
tests. **Not** `camera_rig.h` (T478, slot1), **not** any loader or the T479
branch (slot2).

STOP: the server's packets do not distinguish a post-BeginCast stop from a
success (report with citations — then a client-side rule would be a guess, and
the orchestrator decides); an image golden moves.

## Review

(executor fills)
