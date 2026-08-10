# T55 — The state layer's player position is never written in a live session

**Slot: t2. x86 only. No Pi. NO EQEMU** — the `ReplayZonePacket` seam
makes this unit fully headless, which is the whole reason it can run in
parallel this round. Do not start the server; do not take the
`.eqemu-lock`.

**Territory: `src/client/eq.cpp` (`ZoneProcessZonePlayerToBind` and
`SetPosition`), `src/client/eq_login_world.cpp`
(`WorldProcessZoneServerInfo` apply block), `src/client/eq_zone.cpp`
(producer A pending-zone publish only), `include/client/eq.h`
(accessors), and tests.**

**OUT of territory — do not touch:** `src/client/eq_movement.cpp`,
`src/client/eq_entities.cpp`, `src/client/eq_bridge.cpp`,
`src/client/eq_commands_movement.cpp`. Those are the per-frame movement
writes; converging them is a separate, riskier unit that Will explicitly
deferred (see "Scope decision" below). Do not change the server
(`/mnt/projects/akk-stack/code/` is read-only reference).

Sibling units this round: T54 (tests/UI) and T56 (`main.cpp`,
`willeq_database.cpp`). **No file overlap with either.** Shared file:
`refactor/deferred.md`, append only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then **`refactor/T52_zone_position_convention.md`
in full — especially its Review** (this unit is its direct successor and
inherits its vocabulary and its seam), then `docs/CLAUDE.md`'s coordinate
section, then `refactor/T39_state_layer_migration.md:26-28`.

## Why this unit exists

T52 fixed the *coordinate convention* on the forced-relocation paths.
Orchestrator scouting for the flagged "succor/evac/death-bind
implicated-but-symptomless" follow-up produced two results, and the
second is the unit.

**Result 1 — the convention worry is CLEARED. Record this and move on.**
Succor, evacuate, gate and translocate all arrive through producer A
(`eq_zone.cpp:57-61` and `:98-101`), which swaps server→client correctly.
Death/bind is a *third producer* the board had not documented —
`ZoneProcessZonePlayerToBind` (`src/client/eq.cpp:3392`, opcode
`HC_OP_ZonePlayerToBind = 0x385e`, dispatch `eq.cpp:1154-1155`) — and it
swaps identically and correctly at `eq.cpp:3427-3430`. T52's send-side
fix silently corrected producer C's outbound packet too. Spell-side
succor/evac are no-op stubs (`src/client/spell/spell_effects.cpp:274-278`,
`:783-800`; `SpellEffect::Succor = 88` has no case at all), so all succor
position work is server-driven through producer A. **There is nothing to
fix on the convention axis. Confirm this in Step 1, then stop poking at it.**

**Result 2 — the T39 pattern survived the sweep that declared it closed.**
T39's own words: *"setters were written but never wired, while readers
were repointed at the new home anyway."* That is present, now, in the
position path:

- `EverQuest::SetPosition(x,y,z)` (`src/client/eq.cpp:3109-3116`) is the
  helper that writes the raw `m_x/m_y/m_z` **and**
  `m_game_state.player().setPosition(...)`, which fires `PlayerMoved` on
  the EventBus.
- **It has zero production callers.** `grep -rn "SetPosition("` over
  `src/` returns only its own definition; the only five call sites are
  tests (`tests/test_zoning_integration.cpp:379,528`,
  `tests/test_zoning_graphics_integration.cpp:495,625,732`).
- `eq.cpp:3115` is therefore the **only** write to the state layer's
  player position anywhere in the tree, and it never runs live.

Production relocations write the raw members instead and propagate
inconsistently: `/warp` (`eq_commands_movement.cpp:238-240`) does push a
`PlayerMoved` after its raw write; the zone-in consumer
(`eq_login_world.cpp:885-894`) and death/bind (`eq.cpp:3434-3455`) push
nothing. Same asymmetry on pending-zone state: producer B publishes it
(`eq_zone.cpp:765-767` — `setZoneLineTriggered`/`setPendingZone`),
producers A (`eq_zone.cpp:121-125`) and C (`eq.cpp:3434-3438`) do not.

**Verify all of this yourself before changing anything.** It is scouting
output, not gospel — the greps are cheap and a wrong premise here wastes
the unit.

## Scope decision (Will's, 2026-08-10) — hold this line

Fix **forced relocation only**: the zone-in consumer, death/bind, and
the producer-A pending-zone publish. **Do not** converge the per-frame
movement writes in `eq_movement.cpp` / `eq_entities.cpp`. Routing hot
movement through `SetPosition` fires `PlayerMoved` per frame — an event-
storm and perf question that needs its own investigation with
measurements. If you believe a movement site must change to make the
forced-relocation fix coherent, **STOP and report**; do not expand.

## Step 1 — prove the gap with tests, before any production change

Tests first. They must **fail against the current tree** for the right
reason, and that failure is a deliverable — quote it in the Review.

The seam: `EverQuest::ReplayZonePacket(opcode, bytes, len)`
(`include/client/eq.h:711`) feeds the same opcode switch as the live
receiver with no network and no graphics; `Test_SetZoneConnectedForSeam`
(`eq.h:751`) and the T52 accessors `GetPendingZoneId/X/Y/Z`
(`eq.h:731-734`) do the rest. `tests/test_zone_position_convention.cpp:65-70`
is the exact idiom — follow it.

Add to `tests/test_zone_position_convention.cpp` (it is the right home;
this is the same subject):

- **Producer C convention** — replay `HC_OP_ZonePlayerToBind` (0x385e)
  and assert the pending x/y swap orientation. **Producer C has no test
  at all today**; this closes T52's own coverage gap and is worth landing
  even if everything else in this unit stalls.
- **Propagation** — after a forced relocation is applied, assert the
  state layer's player position matches. This needs a test-side reader
  for `m_game_state.player().x()/y()/z()`, which **has no public
  accessor**. Add a narrow one to `include/client/eq.h` next to the T52
  accessors, named and commented in the same style. Do not widen
  `GameState`'s public surface beyond what the assertion needs.
- **Same-zone death respawn** — `eq.cpp:3441-3446` logs "Same-zone
  respawn" and performs **no local position write at all**, relying
  entirely on a server `PlayerProfile` round-trip. This is the most
  exposed case and is SUSPECTED, not confirmed. Drive it headlessly and
  **find out**. If the client genuinely keeps its pre-death position when
  no fresh `PlayerProfile` arrives, that is the unit's headline finding.

## Step 2 — fix the propagation, minimally

Route the forced-relocation sites through `SetPosition` (or, where the
call shape does not fit, add the `m_game_state.player().setPosition(...)`
write alongside the existing raw write — but prefer the helper; a second
divergent path is how this bug was born):

- The zone-in consumer, `eq_login_world.cpp:885-894`.
- Death/bind, `eq.cpp:3434-3455` — including the same-zone branch if
  Step 1 confirms it needs a local write.
- Publish pending-zone state from producers A and C, matching what
  producer B already does at `eq_zone.cpp:765-767`.

**`PlayerProfile` (`eq.cpp:2154-2156`) is the packet that actually
determines landing position.** Decide deliberately whether it belongs in
this unit — it is a forced relocation by any honest reading, but it is
also on the login path where an unexpected `PlayerMoved` could have
ordering consequences. Whichever way you decide, **state the decision and
the reasoning in the Review**; do not leave it unmentioned.

Every changed site gets a comment naming the convention and the state
write, per T52's precedent — the point is that the next person does not
re-derive this.

## STOP conditions

- The fix appears to require touching out-of-territory movement files.
- `PlayerMoved` firing from a new site causes any existing test to fail
  in a way that suggests an event-ordering dependency — report the
  ordering, do not paper over it with a suppression flag.
- Any temptation to start eqemu. The scout established the headless seam
  is sufficient; if you think it is not, that itself is the report.

## Verification

Full protocol bar. Additionally:

- Quote the **pre-fix failing output** of the new tests, then the passing
  output. A propagation test that never failed proves nothing.
- Explicitly confirm no behaviour change to zone-line travel (producer B)
  — that is the common path and the regression that would actually be
  noticed. `ProducerB_ZoneLinesPassesDestinationThroughUnswapped`
  (`test_zone_position_convention.cpp:151`) must stay green untouched.
- Visual gate PASS, goldens untouched.

## Review

(executor fills: convention-cleared confirmation with line evidence; the
verified caller census for `SetPosition`; the same-zone-respawn verdict
CONFIRMED or REFUTED with the replay evidence; the `PlayerProfile`
decision and why; sites changed; pre/post test output; deviations)

Board updates (`refactor/deferred.md`, append, never delete): re-title
the succor/evac/death-bind "implicated" entry — the convention worry is
cleared and the real defect is propagation. Add producer C to the
documented producer list. **Record explicitly that the T39-T49 state-layer
sweep was declared CLOSED while this hole was open**, and note what
remains: the per-frame movement writes are still raw and unconverged, by
decision, not by oversight.
