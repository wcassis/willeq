# T104 — Replace the combat per-frame poll with bridge events

**Slot: slot2. Headless first, then a LIVE leg. T103 runs in parallel and also
wants eqemu — do all headless work first, then contend for the lock; if held,
wait, then report the live leg outstanding rather than stealing it.**

**Territory: `src/client/application.cpp` (removing the poll),
`src/client/combat.cpp` / `include/client/combat.h`,
`include/client/state/`, `include/client/bridge/`,
`src/client/bridge/game_state_sync_bridge.*`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T104/`.**

**Will's ruling: Trade and Combat should go through the bridge.** This unit is
Combat. T103 is Trade.

Read T81's Review first — it is the model for this exact kind of migration —
then T78's, which deferred combat as *"still uses the old syncCombatState poll
and works."* Will has now called it in.

## The defect

`Application::syncCombatState()` (`application.cpp:1830`, called per tick from
~line 1827) **polls** rather than receiving events. It mirrors:

- `targetId` from `CombatManager`
- target `name`, `hp_percent`, `level` from **the entity map**
- subsequent target HP changes
- the auto-attack flag

and keeps its own dedupe state (`m_lastCombatTargetId`, `m_lastCombatTargetHp`,
`m_lastAutoAttacking`).

This is the last category of T78's Option B migration. Entities (T81) and group
(T99) are done; doors remain separately blocked on routing.

## The hard part — name it before you design

**The poll reads two domains together and events do not.** Target *identity*
comes from `CombatManager`; target *name, HP and level* come from the entity
map. A single poll gets both consistently for free. Event-driven, you must
correlate them:

- when the target changes, the entity may not be in the map yet (the poll
  already handles this, mirroring the id with placeholder fields);
- when the target's HP changes, that is an **entity** event, not a combat one,
  and only matters if that entity happens to be the current target.

Decide deliberately whether target HP should be projected by the combat domain
reacting to entity events, or read from the entity projection at the point of
use. **State your choice and why.** Getting this wrong produces a target whose
HP silently stops updating — which a poll would never do, and which a
casual test would not catch.

## Follow T81's discipline

- **Verify reconstructibility, do not assume it.** For each field the poll
  copies, prove the event stream carries enough to rebuild it. T81 did this by
  showing its source republished the complete struct at every call site.
- **Add typed events rather than widening existing ones.**
- **Apply the falsification test.** T81's plan said: if consumers need
  changing, the diagnosis was wrong and this was never a projection gap. State
  whether consumers needed changes.
- **Report field-loss cases** rather than glossing them.

## Remove the poll — that is the point

Deleting `syncCombatState()` and its three dedupe members is the deliverable.
A migration that leaves the poll in place alongside events has made things
worse, not better: two writers to the same projection.

**STOP and report** if the poll cannot be removed. Knowing precisely which
field defeats the event model is worth more than a half-migration.

## Live leg — the bar

Combat state is observable, so verify it honestly:

- Target something and confirm the projection reflects the target and its HP.
- Damage it and confirm HP **keeps updating** — this is the correlation risk
  above, and the single most likely thing to be quietly broken.
- `/combat on` and confirm the auto-attack flag tracks.
- Clear the target and confirm the projection clears.

Note T102's live experience: `/attack` drew a real "too far away" refusal and a
GM `#goto` to close distance did not work. Plan for getting genuinely adjacent
to something attackable, or use a pet.

Session discipline: binary freshness against HEAD first; daylight
`/say #set time 11 0`; pad scripts long; `-d 4` plus `--log-level=DEBUG`;
restore DB state and confirm by `SELECT` **after logout**; kill clients by real
binary PID, not `nohup`'s `$!`; clean teardown, delete the lock.

## What you may not do

- Do not touch trade state — that is T103, running in parallel. Overlapping
  edits to `game_state_sync_bridge.cpp` are likely, so keep changes narrow and
  additive.
- Do not change `CombatManager`'s own behaviour. It is authoritative; this unit
  changes how its state *reaches the projection*, not what it does.
- Do not leave both the poll and the events in place.

## Verification

Full build and suite (current total 2217; sole pre-existing failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`). Every new test
**proven able to fail in both directions**. Visual gate — goldens must not
move.

## Review

(executor fills: the reconstructibility evidence per polled field; how target
HP correlation was resolved and why; the events added; whether consumers needed
changes (the falsification test); confirmation the poll and its dedupe members
are gone; live evidence that target HP keeps updating under damage; any
field-loss case; deviations)

## Board

This closes the last category of T78's Option B source→projection migration
apart from doors. Record that.
