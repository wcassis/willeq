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

**Verdict: the lead is CONFIRMED (from the eqemu source and wire-byte tests;
no live run -- Leg A held no lock). Fixed. The live proof is boarded as B630.**

### 1. What the server sends, per interrupt cause

eqemu source at `/mnt/projects/akk-stack/code`, read at T469. Titanium has no
ENCODE for `OP_InterruptCast` (`common/patches/titanium_ops.h`), so
`InterruptCast_Struct {uint32 spawnid; uint32 messageid; char message[];}`
(`common/patches/titanium_structs.h:426`) is the wire form. `OP_ManaChange` is
encoded to 16 bytes, dropping the server struct's `slot`
(`titanium.cpp:1247`, `titanium_structs.h:441`).

| cause | eqemu path | to the caster | to bystanders |
|---|---|---|---|
| moved or failed channelling during the cast | `CastedSpellFinished` -> `InterruptSpell()` (`zone/spells.cpp:1557`) | `OP_InterruptCast` 8 bytes, messageid 439 "Your spell is interrupted." (`spells.cpp:1306`), then `OP_ManaChange` keepcasting 0 with the spell id (`SendSpellBarEnable`, `spells.cpp:5752`) | `OP_InterruptCast` messageid 12478 "%1's casting is interrupted!" + the caster's name (`spells.cpp:1339-1344`, `ignore_sender = true`) |
| damage interrupt roll | `attack.cpp:4456` -> `InterruptSpell()` | same | same |
| stun / mez | `Mob::Stun` `spells.cpp:5781`, `Mob::Mesmerize` `:5840` | same | same |
| death | `attack.cpp:1901` | same | same |
| no target / no mana (before BeginCast) | `DoCastSpell` `spells.cpp:400`, `DoSpellInterrupt` `:490` | same, plus a separate MessageString | same |
| divine aura | `spells.cpp:568` `InterruptSpell(173, ...)` | `OP_InterruptCast` messageid **173** SPELL_FIZZLE + ManaChange | 1218 variant |
| bard song ends | `InterruptSpell` with the song messages; `DoBardCastingFromItemClick` `spells.cpp:7285` | `OP_InterruptCast` 12686/12687 | song "other" variants |
| **fizzle** | `DoCastSpell` `spells.cpp:321-331` -> `StopCasting()` `:1351` | **only** `OP_ManaChange` keepcasting 0, then MessageString 173. **No `OP_InterruptCast`**, and before `SendBeginCast` (`:450`) | `FilteredMessageCloseString` 1218 |
| recast timer not expired (after BeginCast) | `CastedSpellFinished` `spells.cpp:1423`, `:1439` -> `StopCasting()` | only `OP_ManaChange` keepcasting 0 | nothing |

Periodic mana ticks carry **keepcasting 1** (`Client::CheckManaEndUpdate`,
`zone/client.cpp:2432`). A **successful** cast also sends keepcasting 0
(`SendSpellBarEnable` at `spells.cpp:1817`), so after BeginCast the ManaChange
alone is ambiguous. No oracle capture of an interrupt exists under `review/`
(T101's log records only a client-side `/interrupt`).

No STOP condition fired: every cause sends a packet fable recognises and now
acts on, except the post-BeginCast recast-timer stop, which fable recognises
but cannot tell from a success (B631).

### 2. What fable did (pre-fix)

- `ZoneComponent::IgnoredZoneOpcodes()` (`src/client/zone/zone_dispatch.cpp`)
  listed `HC_OP_InterruptCast` with "spell manager handles this via other
  packets". **No other packet did.** `PlayerComponent::ZoneProcessManaChange`
  (`src/client/player/player_buffs.cpp`) parsed `keepcasting` and only logged
  it; `SpellManager::handleManaChange` was log-only.
- `SpellManager::handleInterrupt` (`src/client/spell/spell_packet_handlers.cpp`)
  had only test callers, and its signature `(caster, spell_id, message_type)`
  could not have been fed from the wire: the struct has no spell id, and
  `message_type == 2 -> Fizzle` matched no message id eqemu sends.
- Result on an interrupted cast: `m_is_casting` stayed true and the gem stayed
  `Casting`, so `beginCastFromGem()` refused the next cast as `AlreadyCasting`
  (a client gate refusing what the server accepts). `SpellManager::update()`
  ended the cast only after duration + 5 s via `completeCast(true)`: **reported
  as Success**, gem recast started, `SpellCastVisualComplete` with
  `isCastAnimation` (an impact burst). The cast bar kept filling and the cast
  glow stayed. A cast interrupted before BeginCast, or a fizzle, sat for 5 s on
  the `m_waiting_for_server_confirm` timeout. A bystander's interrupted cast
  left their entity cast bar and glow up. The "Your spell is interrupted." line
  never reached chat.

### 3. The fix

- `ZoneComponent::OnInterruptCast` (new, `zone_dispatch.cpp`, beside `OnStun`)
  parses the struct (spawnid, messageid, the optional NUL-terminated name,
  `strnlen`-bounded) and calls `SpellManager::handleInterrupt`. The opcode
  moved from `IgnoredZoneOpcodes()` to `ZoneOpcodeTable()`.
- `handleInterrupt(caster_id, message_id, caster_name)`: resolves the spell id
  from our own cast or the target-cast tracking, puts the eqstr text in chat
  (formatted with the name on the bystander copy), ends our own cast
  (Fizzle for 173/180, otherwise Interrupted), and for someone else publishes
  `SpellCastVisualInterrupted` (`isPlayerCast = false`, `isTargetCast` when it
  was our target).
- `handleManaChange(..., keepcasting)`: keepcasting 0 for the spell we are
  casting, **while BeginCast has not confirmed it**, ends the cast
  (Interrupted; eqemu sends the fizzle message separately). After BeginCast it
  is deliberately ignored.
- `SpellManager::endOwnCastByServer(result)` (new, private) is the one body
  both use: cast visuals cancelled, gem back to `Ready` (eqemu starts no recast
  on an interrupt), callback, `SpellCastComplete`, state cleared.
- `check_zero_callers.py`: the `handleInterrupt` B604 row and its reason are
  removed; `--check` exits 0 with the tests-only census clean at 290 rows
  (109 boarded, down from 110).

Deviations, flagged:
1. **Message text display is new, not "kept as it is".** The plan assumed the
   text came through another packet; it does not. The interrupt packet IS the
   message (`spells.cpp:1338`), so before T469 fable showed nothing. It now
   goes through the same eqstr lookup as every other message
   (`data/willeq.db` eqstrings), and a lookup miss (`[Unknown message #...]`)
   is not shown.
2. **`OP_ManaChange` is wired too**, which is beyond the literal lead: the
   fizzle and the refused-before-BeginCast stops send no `OP_InterruptCast`,
   so the InterruptCast arm alone would not have fixed them.
3. **`InterruptMessageTextReachesChat` was added after the red run** and was
   never run on the pre-fix tree. It is red by construction there (the opcode
   was in the ignored table, so `DispatchZonePacket` returned before any code
   ran), but that is argued, not measured.
4. The existing `handleInterrupt` unit tests in
   `tests/test_spell_manager_timing.cpp` were rewritten for the new signature
   (message type 2 became message id 173, and so on), and
   `HandleManaChangeDoesNotCrash` became `HandleManaChangeKeepcastingOneDoesNotEndACast`.

### 4. Tests

`tests/test_spell_interrupt_wire.cpp` (new target `test_spell_interrupt_wire`)
builds the packets byte by byte from the eqemu structs and drives them through
`EverQuest::ReplayZonePacket` -> `ZoneComponent::DispatchZonePacket` into a
headless `EverQuest` part-way through a gem cast.

| test | pre-fix (unfixed tree, T469 tip) | post-fix |
|---|---|---|
| ServerInterruptOfConfirmedCastEndsTheCast | **FAIL** (still casting, gem Casting, no events) | pass |
| ServerInterruptBeforeBeginCastEndsTheCast | **FAIL** | pass |
| NextCastIsNotRefusedAsAlreadyCastingAfterInterrupt | **FAIL** (`AlreadyCasting`) | pass |
| InterruptedCastIsNeverReportedAsSuccessByTheTimeout | **FAIL** (SpellCastComplete result Success, 1 SpellCastVisualComplete) | pass |
| FizzleManaChangeBeforeBeginCastEndsTheCast | **FAIL** | pass |
| OtherCastersInterruptCancelsTheirCastNotOurs | **FAIL** | pass |
| ManaTickWithKeepcastingOneNeverEndsACast | pass (guard) | pass |
| KeepcastingZeroAfterBeginCastIsNotAnInterrupt | pass (guard) | pass |
| KeepcastingZeroForAnotherSpellDoesNotEndTheCast | pass (guard) | pass |
| TruncatedInterruptPacketIsIgnored | pass (guard) | pass |
| InterruptMessageTextReachesChat | not run (added after, deviation 3) | pass |

Pre-fix run: 6 failed, 4 passed. Post-fix: 11/11, and
`test_spell_manager_timing` 36/36.

### 5. Goldens

- `zone_opcodes_g01.txt` (the `zone_dispatch` G01 golden) is **unchanged**: the
  opcode set is the same, only the handled/ignored split moved, and the
  two-direction partition tests stay green.
- `ZoneDispatchTable.EveryIgnoredOpcodeCarriesItsReason` pinned "exactly three
  comment-only arms" and went red in the first full suite run. **Updated in
  place to 2 per G-correction 32**, with the reason written beside it.
- `bridge_component_intents` was not touched. No image golden moved (visual
  gate PASS, `git status` shows no golden change).

### 6. Verification

| step | result |
|---|---|
| 1. `cmake --build slot2/build -j24` | **PASS**, exit 0 |
| 2. `ctest -j6` | first run 4,749/4,750: red was `EveryIgnoredOpcodeCarriesItsReason` (fixed, above). Second run 4,749/4,750: red was `DocCitations`, which T469's own new board lines caused by citing `zone/spells.cpp` repo-relative; fixed with absolute akk-stack paths and re-run (0 fail). Focused ctest re-run of DocCitations, ZeroCallers, ZoneDispatchTable, SpellInterruptWire and SpellManager: all pass |
| golden tests for touched TUs (`test_zone_dispatch_table`, `test_zone_opcode_literals`) | **PASS** 6/6, 3/3 |
| `ZeroCallers` (`--check`) | **PASS**, exit 0 |
| 3. `build-gles2-x86.sh` | **PASS**, exit 0 |
| 4. visual gate | **PASS** (`=== VISUAL GATE: PASS ===`), goldens untouched |
| 5. `build-arm-noble.sh` | **PASS**, exit 0 |
| 6. `check_file_sizes.sh fable` | **PASS**: 6 changed files ok; the new test TU and cmake file are also ok via `--files` |

Builds ran strictly in sequence: main build, ctest, build-gles2, gate, ARM.

### 7. Board

- **B604 closed.**
- **B630** (A, owed live proof): a two-client interrupted cast plus a fizzle,
  with the caster-side and observer-side assertions spelled out.
- **B631** (A, unconfirmed live, rare): a post-BeginCast `StopCasting` (recast
  timer) still resolves through the duration + 5 s success fallback, and so
  does a fully resisted spell (the resist branch of `Mob::SpellOnTarget`
  returns before the `effect_flag 0x04` packet). Measure what the official
  client does with a post-BeginCast `keepcasting 0` before changing it.

Not done and not in scope: `CastResult::Fizzle` is reported for a fizzle only
when it arrives as `OP_InterruptCast` 173. The `OP_ManaChange` fizzle reports
`Interrupted`, because the packet cannot tell a fizzle from a refused cast. The
gem and cast-bar outcome is identical, and the callback has no production
registrant (B603).
