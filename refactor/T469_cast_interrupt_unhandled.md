# T469 — the server's cast-interrupt packet is only logged; `SpellManager::handleInterrupt()` is reached only by tests (B604)

Class A (possible live defect, unconfirmed), **no lock (Leg A)**, slot2. Found
by T458's tests-only census (`refactor/T458_tests_only_callers.md`, B604).

## The lead

When the server interrupts a cast (movement, damage, stun, a failed
concentration check), it sends an interrupt packet (Titanium `OP_InterruptCast`
and/or a `OP_ManaChange`/`OP_ColoredText`/formatted-message pair — establish
which from the eqemu source at `/mnt/projects/akk-stack/code/zone/`,
`Mob::InterruptSpell` and friends). fable's dispatch arm only logs it, and the
state change lives in `SpellManager::handleInterrupt()`, which only tests call.
If true, after a server-side interrupt fable's client still believes it is
casting: the cast bar keeps filling, gems stay greyed/locked, the next cast may
be refused client-side (a client gate refusing what the server would accept is
a defect — Will's standing rule #12), and the casting animation/particles may
continue.

## Tasks

1. Establish from eqemu source exactly what the server sends on each interrupt
   cause (opcodes, struct, which of them carry the spell id / the message), and
   what the Titanium client does with each (existing oracle captures under
   `review/` or `captures/` if any show an interrupt; do not invent).
2. Trace fable: the zone dispatch arm(s), what `handleInterrupt()` resets
   (casting state, gem lock/recast, cast bar, animation, particles, bridge
   events), and every client-side gate that consults the casting state.
3. Pre-fix failing test: feed the real interrupt packet bytes (built from the
   eqemu struct) through the zone dispatch into a headless `EverQuest` mid-cast
   and assert the client is no longer casting, the gem is usable per the
   server's rules, and the renderer-facing event is published. Then wire the
   arm to `handleInterrupt()` (or the correct equivalent), keeping message text
   display as it is.
4. Board the live proof as owed (a real interrupted cast: move during a cast on
   the live server, two-client) for the next lock holder.

## Proof

Standard verification bar; G-batch goldens for every TU touched —
`zone_dispatch` and `bridge_component_intents` are golden-pinned (CLAUDE.md
merge rule; reviewed edits update goldens in place per G-correction 32, say
so); `ZeroCallers` (T458 made tests-only rows enforced: `handleInterrupt`'s row
must come out when it gains a production caller); visual gate; ARM; file sizes.
Board B630–B639; strike/narrow B604.

File territory: `src/client/spell/`, the zone dispatch arm(s) for the interrupt
opcode(s), bridge publishers for cast state, spell tests,
`scripts/refactor/check_zero_callers.py` rows for spell functions.
**Not** doors / `loadGlobalAssets` (T468, slot3), **not** camera rig / hotkeys
(T466, slot1).

STOP: the server sends nothing fable can recognise for some interrupt cause
(report which, with the eqemu citation); an image golden moves.

## Review

(executor fills)
