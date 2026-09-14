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

**Verdict: the lead is CONFIRMED for the post-BeginCast stop and REFUTED as
written for the resist. The packets DO distinguish a stop from a success, so
the STOP condition did not fire. Fixed from the wire. The evidence is the eqemu
source plus wire-byte tests; there was no live run, because Leg A held no lock.
The live leg is owed under B630.**

### 1. What the caster receives, per outcome

eqemu source at `/mnt/projects/akk-stack/code`, read at T482. All paths are
`zone/spells.cpp` unless named. Client caster, gem cast, non-bard, in queue order.

| outcome | packets to the caster | citations |
|---|---|---|
| **success, landed** | `OP_BeginCast` ... then `OP_Action` effect_flag 0, `OP_Action` effect_flag 4, `OP_ManaChange` keepcasting 1 (mana taken), **`OP_ManaChange` keepcasting 0**, [`OP_LinkedReuse` if the spell has a timer_id], **`OP_MemorizeSpell` slot=gem scribing=3** | `SendBeginCast` :497; `SpellOnTarget` :4008, :4676; `SpellFinished` mana :2765 -> `Client::SetMana`/`CheckManaEndUpdate` client.cpp:2401-2433; `CastedSpellFinished` tail :1817 (`SendSpellBarEnable` :5752), :1820-1822 (`SetLinkedSpellReuseTimer` :7173), :1824 (`Client::MemorizeSpell` client.cpp:3346-3404); Titanium encodes: ManaChange 16 B (titanium.cpp:1247), MemorizeSpell maps the slot for scribing 3 only (titanium.cpp:1260-1274) |
| **success, no landing packet** (pet summon, anything with no `SpellOnTarget`) | the same without the two `OP_Action` | as above |
| **success, fully resisted nuke** | `OP_Action` effect_flag 0, `OP_FormattedMessage` TARGET_RESISTED 425, keepcasting 1, **keepcasting 0**, [LinkedReuse], **`OP_MemorizeSpell` scribing 3**. No effect_flag 4 | the action goes out FIRST "so we get the particles even if the spell doesn't land" :3957-3962; resist :4484-4531 returns false; `SpellFinished` only returns false for a beneficial buff :2590-2593, so the mana (:2765) and recast are consumed and the success tail runs |
| **stopped after BeginCast: recast timer** | `OP_SimpleMessage` SPELL_RECAST 236, **keepcasting 0**. Nothing else | :1418-1425, :1432-1441 -> `StopCasting` :1351-1379 |
| **stopped after BeginCast: SpellFinished false** (a beneficial buff that does not take hold, no target) | [`OP_Action` effect_flag 0 if it reached `SpellOnTarget`], **keepcasting 0**. No mana, no recast | :1744-1750, :2590-2593, :4589-4617 |
| interrupt (reference, T469) | `OP_InterruptCast` 439, keepcasting 0 | :1306-1314 |
| fizzle (reference, T469) | keepcasting 0, then SimpleMessage 173, before BeginCast | :321-331 |
| bard song (reference) | the actions, then `OP_MemorizeSpell` scribing 3 ONLY, with no keepcasting 0 (Character:EnableBardMelody, default true, common/ruletypes.h:190) | :1791-1811 |

**The distinguishing signal is `OP_MemorizeSpell` with scribing 3
(memSpellSpellbar) naming the cast's spell.** Only the success tail sends it:
eqemu's comment on it is "this causes the delayed refresh of the spell bar
gems" (:1819). It is queued contiguously after the keepcasting 0 on the
reliable stream (`EQStream::QueuePacket`, common/net/eqstream.cpp:85-123), and
only `OP_LinkedReuse` can sit between the two. The keepcasting 0 itself cannot
decide the outcome; it means only that the spell bar is usable again
(`SendSpellBarEnable`, "puts the spell bar back into a usable state fast",
:5751). No oracle capture of either sequence exists; the live leg is B630.

### 2. What fable did (pre-fix)

- `SpellManager::handleAction()` ended our own cast as a **Success** on ANY
  own-sourced spell action, including the effect_flag 0 action eqemu sends
  before it knows whether the spell lands. So a buff that did not take hold
  was reported as a success, with its gem put into recast, even though the
  server started no recast.
- `handleManaChange()` ignored keepcasting 0 after BeginCast (T469's deliberate
  rule), and `ZoneComponent::OnMemorizeSpell` ignored scribing 3. So a
  recast-timer stop, or any cast with no own-sourced action (a pet summon, or a
  detrimental spell on another player, whose action source is the target,
  :3973-3981), kept the cast bar and the `Casting` gem until
  `update()`'s duration + 5 s fallback. That fallback called
  `completeCast(true)`: a **Success**, a recast started, and a synthetic impact
  burst painted. During those 5 s the next cast was refused as `AlreadyCasting`.
- The resisted nuke was already right before T482. It completed as a Success
  with a recast at its effect_flag 0 action, which is the result eqemu implies.
  B631's "resisted spell" half was wrong about which path it took.

### 3. The fix

- **keepcasting 0 after BeginCast** (`handleManaChange` ->
  `releaseConfirmedCast`) releases the cast at once. It publishes
  `SpellCastVisualComplete` with `isCastAnimation` false, which ends the cast
  bar and glow and paints nothing. It also frees the gem to `Ready` and clears
  `GameState` casting. The outcome is recorded as pending.
- **`OP_MemorizeSpell` scribing 3** (`OnMemorizeSpell` -> new
  `SpellManager::handleSpellBarRefresh`) resolves the pending outcome as
  **Success**: gem recast, callback, `SpellCastComplete`. A bard song, which is
  still casting with no keepcasting 0, completes the same way. A refresh for a
  gem holding that spell starts the gem's recast even when nothing is pending.
- **Any other packet first** (new `SpellManager::noteZonePacket`, called at the
  top of `ZoneComponent::DispatchZonePacket`, exempting `OP_MemorizeSpell` and
  `OP_LinkedReuse`) resolves the outcome as **Interrupted**. A new cast started
  by the player resolves it first as well (`beginCastFromGem`/`beginCastById`).
- **`handleAction` no longer ends our own cast.** It still publishes the
  visual event, so the server's effect_flag 0 action paints exactly as before.
- **`update()`'s duration + 5 s branch** is a last resort for a lost packet:
  WARN, `endOwnCastByServer(Interrupted)`, no synthetic impact.
- T469's before-BeginCast rule is kept unchanged.

Deviations, flagged:
1. **Plan task 2(b), "never paint an impact on a target that resisted", was
   not implemented, because the server contradicts it.** eqemu sends the
   effect_flag 0 action before the resist check precisely so the particles
   show even when the spell does not land (:3957-3962). T395 made that packet
   the paint (B204). The client displays what it is told, so a resisted nuke
   still paints once, from the server's packet. What T482 guarantees is that
   nothing paints a SECOND or synthetic impact.
2. **`handleAction()` no longer completes our own cast.** This goes beyond
   B631's literal text, but a stop that follows an action (the buff that does
   not take hold) cannot be fixed any other way.
3. **The result code is decided by the next packet, and the recast is decided
   by the MemorizeSpell alone.** eqemu sends some packets unreliably, so one
   can arrive inside the tail. The gem recast is still right then, but the
   `SpellCastComplete` result would read Interrupted. Nothing in production
   reads that result (B603). Boarded as B760.
4. **Existing tests updated in place (G-correction 32 style), each with a
   comment:** `SpellInterruptWire.KeepcastingZeroAfterBeginCastIsNotAnInterrupt`
   now asserts the outcome is pending, where it used to assert "still casting";
   `SpellManagerHandlers.HandleActionSuccessPacketCompletesOwnCastAndFiresCallback`
   became `...DoesNotEndOwnCast`;
   `SpellManagerTiming.BUG_UpdateIgnoresDeltaTimeHugeDeltaDoesNotFastForwardCastTimeout`
   now expects `SpellCastVisualInterrupted` and no paint from the timeout.
5. T469's four wire builders moved to `tests/spell_wire_payloads.h`, shared
   with the new test; their bytes are unchanged. T469's file stays 11/11.

### 4. Tests

`tests/test_spell_cast_end_wire.cpp` (new target `test_spell_cast_end_wire`)
replays real Titanium bytes (`tests/spell_wire_payloads.h`, every layout cited
to `titanium_structs.h` and the ENCODE) through `EverQuest::ReplayZonePacket`
-> `DispatchZonePacket`, into a headless `EverQuest` with the shipped spell DB
(93 Burst of Flame, 288 Minor Shielding).

| test | pre-fix (tip `39ce6203`) | post-fix |
|---|---|---|
| RecastTimerStopAfterBeginCastEndsTheCastNotAsSuccess | **FAIL** (still casting, gem Casting; after the timeout: Success, 1 impact, gem Refresh) | pass |
| NextCastIsNotRefusedAfterAPostBeginCastStop | **FAIL** (`AlreadyCasting`) | pass |
| BuffThatDoesNotTakeHoldEndsNotAsSuccessDespiteItsAction | **FAIL** (Success, gem Refresh) | pass |
| TimeoutFallbackNeverReportsSuccess | **FAIL** (Success, synthetic impact, gem Refresh) | pass |
| SuccessTailWithNoLandingEndsAsSuccessAndStartsTheRecast | **FAIL** (still casting) | pass |
| LinkedReuseBetweenTheTailPacketsIsStillASuccess | **FAIL** | pass |
| SpellBarRefreshWithoutKeepcastingZeroCompletesTheCast | **FAIL** | pass |
| UnreliablePacketInsideTheTailStillStartsTheRecast | **FAIL** | pass |
| LandedSpellEndsAsSuccessWithOneImpact | pass (guard) | pass |
| FullyResistedNukeEndsAsSuccessWithTheRecastConsumed | pass (guard) | pass |
| SpellBarRefreshForAnotherSpellDoesNotEndTheCast | pass (guard) | pass |

Pre-fix: 8 failed, 3 passed, with the shared-header refactor in place and no
source change (`test_spell_interrupt_wire` 11/11 on that same build).
Post-fix: 11/11, `test_spell_interrupt_wire` 11/11, `test_spell_manager_timing`
36/36.

### 5. Goldens

- `zone_dispatch` (G01, `tests/goldens/zone_opcodes_g01.txt`): **unchanged**.
  No opcode moved between the handled and ignored tables: `HC_OP_MemorizeSpell`
  was already handled, and `HC_OP_LinkedReuse` stays ignored. The new code is
  one call before the lookup plus one `else if` inside an existing `On*`
  handler. `test_zone_dispatch_table` and `test_zone_opcode_literals` pass in
  the full run.
- `bridge_component_intents`: not touched. No other G golden covers a touched TU.
- Image goldens: see step 4 below.

### 6. Verification

| step | result |
|---|---|
| 1. `cmake --build slot3/build -j24` | **PASS**, exit 0 |
| 2. `ctest -j6` | **PASS**, 100% of 4,804, 0 failed |
| golden tests for touched TUs (`ZoneDispatchTable.*`, `ZoneOpcodeLiteral*`) | **PASS** (inside the full run) |
| `ZeroCallers` (`check_zero_callers.py --check`) | **PASS**, exit 0, both censuses clean. `completeCast` keeps a production caller (`handleSpellBarRefresh`, bard path) |
| `DocCitations` / `DocClaims` | **PASS** (1,371 citations, 0 fail; no retired names) |
| 3. `build-gles2-x86.sh` | **PASS**, exit 0 |
| 4. visual gate | **PASS** (`=== VISUAL GATE: PASS ===`); `git status` shows no golden change |
| 5. `build-arm-noble.sh` | **PASS**, exit 0 |
| 6. `check_file_sizes.sh fable` | **PASS**: 6 changed tracked files ok (largest `zone_dispatch.cpp` 962); the three new files are also ok via `--files` (test TU 347, header 140, cmake 238) |

Builds ran strictly in sequence: the red target build, the green target build,
the full build, ctest, build-gles2, the gate, then ARM.

### 7. Board

- **B631 closed**, with the correction to its resist half.
- **B630** gains T482's three caster-side live legs: a normal nuke, a
  recast-timer stop, and a buff that does not take hold.
- **B760** (A, unconfirmed): the result code can read Interrupted for a
  success, or arrive late, when an unreliable packet lands inside the tail. The
  recast is unaffected.
- **B761** (A, unconfirmed): a bard song with `EnableBardMelody` off has no end
  packet, so it ends on the timeout without a result.
- **B762** (A, unconfirmed): `MAX_SPELL_GEMS` is 8 but Titanium has 9 gems, so
  every gem-indexed arm drops slot 8.
- `docs/module_map.md`: updated the rows for `zone_dispatch.cpp`,
  `spell_manager.cpp` and `spell_packet_handlers.cpp`. Their line counts were
  stale before T482 as well.
