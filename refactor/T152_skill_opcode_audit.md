# T152 — Audit all seventeen skill opcodes against their eqemu handlers

**Slot: slot1. x86. No eqemu lock needed (source verification). No Pi.**

**Territory: `src/client/skill/`, `include/client/skill/`, `tests/`,
`refactor/`. Do not touch `src/client/eq_player_info.cpp` or
`src/client/combat.cpp` beyond reading them** — if a fix reaches into either,
report the blast radius instead of following it.

Read the T151 Review first (`refactor/T151_zero_caller_triage.md`) — this unit
exists because of its closing finding — then the T146/T148 sections of
`refactor/deferred.md` for the standard of evidence this arc expects.

## The finding this unit chases

`SkillManager::sendSkillUsePacket()` (`src/client/skill/skill_manager.cpp:432`)
sends **`packet.Resize(0)`** — a zero-length body — for every skill with a
dedicated opcode. `getSkillOpcode()` (`:442`) maps **seventeen** skills to one:

`ApplyPoison`(6) `BindWound`(9) `Disarm`(16) `DisarmTraps`(17) `FeignDeath`(25)
`Forage`(27) `Hide`(29) `Mend`(32) `SenseHeading`(40) `Sneak`(42)
`PickPocket`(48) `Track`(53) `Fishing`(55) `SenseTraps`(62) `Begging`(67)
`InstillDoubt`(71) `Taunt`(73)

T151 verified exactly one — `ApplyPoison` — and found it **broken**: eqemu's
`DECODE(OP_ApplyPoison)` uses `DECODE_LENGTH_EXACT` and the handler needs
`inventorySlot`, so a zero-length packet is discarded **before the handler, with
no "Wrong size" line at all**. The `DECODE_LENGTH_EXACT` failure rewrites the
opcode to `OP_Unknown` silently. **A skill can be completely dead on the wire
while the client cheerfully reports using it and the server log says nothing.**

## Orchestrator's pre-scan — a STARTING POINT, NOT A FINDING

I ran a coarse scan of eqemu's handlers. **Treat it as a hypothesis to test, not
a result to confirm.** It looks only at the first ~12 lines of each handler and
can be wrong in both directions:

| gate found | opcodes |
|---|---|
| explicit size check | `ApplyPoison` (confirmed broken), `Disarm`, `PickPocket`, `Taunt` |
| no size check, no buffer reference in the opening lines | `DisarmTraps`, `FeignDeath`, `Forage`, `Mend`, `SenseHeading`, `Sneak`, `Track`, `Fishing`, `SenseTraps`, `InstillDoubt` |
| no size check but the handler DOES reference the buffer | `Hide`, `Begging` |
| handler not found under that name | `BindWound` — it is `Handle_OP_Bind_Wound` (underscore), `zone/client_packet.cpp:4013` |

**So the likely damage is roughly four or five of seventeen, NOT sixteen.** Many
EQ skills genuinely are empty-packet activations and a zero-length body is
correct for them. **Do not go looking for sixteen bugs.** Equally, do not assume
the ten "no gate" handlers are fine — a handler with no size check can still
read a field off an empty buffer, which is a different and worse defect than a
clean rejection.

## Method — per opcode, all seventeen

1. Find the real handler (mind `Handle_OP_Bind_Wound`'s underscore, and check
   for others).
2. Determine what the handler **actually requires**: an explicit size gate, a
   `DECODE`/`ENCODE` in `common/patches/titanium.cpp` (the arbiter rule — if one
   exists, `Titanium::structs::` is the wire format), or a bare read off
   `app->pBuffer`.
3. Decide: does a **zero-length body** reach the handler and do the right thing?
4. Classify: **CORRECT-AS-EMPTY** / **BROKEN-REJECTED** (gated, silently
   dropped) / **BROKEN-GARBAGE** (accepted but reads off an empty buffer) /
   **NEEDS-CAPTURE**.

**Also check every one for a second route.** T151 found `ApplyPoison` had a
correct dead sender competing with the broken live path. **`Taunt` is a known
second-route case**: `CombatManager::Taunt()` (`src/client/combat.cpp:739`)
sends `HC_OP_Taunt` **with** a body while skill 73 sends it empty, and eqemu
gates on `ClientTarget_Struct`. Work out which path is right and whether they
conflict. Look for the same shape elsewhere.

## The fix, for whatever turns out broken

Prefer **routing the skill path through an existing correct sender** over adding
a new one — that is T151's recommendation for `ApplyPoison` and it avoids the
duplicated-then-drifted defect this programme keeps hitting (T147 spent a whole
unit deleting the last generation of it). Where no correct sender exists, build
the body in `sendSkillUsePacket` from the struct eqemu actually requires.

**`SendApplyPoison` is a live, correct, zero-caller sender. Do not delete it —
route skill 6 through it.**

## Acceptance

**No eqemu lock, so nothing here is wire-verified** — this is a source audit
plus tests, and the Review must say so in those words. Do not write "skills work
now." The wire leg is a separate owed unit; enumerate the probes it should run,
one per opcode you changed.

Where a fix changes the bytes on the wire, add a serialization test asserting
the exact body length and content against the eqemu struct — `offsetof`-based,
per T147's `tests/captured_packets.h` convention, not a bare `sizeof`.

## Verification

Full protocol bar. Suite baseline 2524 at tip `4a73e3ab`, sole expected failure
the documented ALSA `AudioManagerTest.PreloadCommonSounds`.

## STOP conditions

- A fix requires changing `combat.cpp`'s or `eq_player_info.cpp`'s senders
  rather than the skill path — report the blast radius, do not follow it.
- An opcode where only a capture can settle whether an empty body is accepted —
  record it, do not take the lock.
- More than about six turn out broken: stop and report before fixing, because
  that would mean the pre-scan is badly wrong and the batch needs re-scoping.

## Review

(executor fills)
