# B892 — the player's own levitation (SPA 57)

Issue #208. Class B feature gap. The official client floats a levitating player;
willeq never applied the caster's own levitation.

## The gap, measured at origin/fable 471fbeaa

- eqemu sends the caster the levitate appearance with `ignore_self`
  (akk-stack `code/zone/spell_effects.cpp:1452-1455`,
  `SendAppearancePacket(FlyMode, Levitating, true, true)`), so the caster's
  own client never receives `AT_FLYMODE 2` from a self-cast. The fade DOES
  reach self (the default `ignore_self=false`).
- `SpellEffect::Levitate` (SPA 57, `include/client/spell/spell_constants.h:184`)
  had **zero readers** in the tree.
- `PlayerMovementState::isLevitating` (`renderer_types.h:327`) was fed only
  from `SwimmingStateChanged` and only suppressed idle sinking in water; it
  floated nothing on land and slowed no fall.

Shipped-data confirmation (this tip, `data/willeq.db`): spell **261 "Levitate"**
carries SPA 57 in effect slot 1 (`effect_id_1 = 57`); **970 "Levitation"**
carries it in slot 3 (`effect_id_3 = 57`); **288 "Minor Shielding"** carries
neither. (Query: `select id,name,effect_id_1,effect_id_2,effect_id_3 from
spells where id in (261,970,288)` — the DB-gated tests assert the same rows.)

## The fix

1. **The predicate** — `include/client/spell/levitation.h`, `EQ::
   PlayerIsLevitating(buffs, db, selfFlymode, exclude_spell_id)`. The first
   reader of SPA 57: any active buff whose spell carries `SpellEffect::Levitate`,
   **OR** a self flymode 2/5 the server did send (a GM `#levitate`-style
   effect reaches self even though the cast path does not). `db` may be null
   (a pre-`InitializeSpellSystem` rig); then only the flymode half fires. The
   fade hooks fire while the fading buff is still in the list (notify-before-
   erase, `buff_manager.cpp`), so the fade-side recompute excludes the leaving
   spell — the same pre-erase shape T339's `computeVisionType()` uses.
2. **A dedicated event** — `GameEventType::LevitationChanged` with
   `LevitationChangedData{isLevitating}`, an absolute value. NOT piggy-backed on
   water transitions (the issue's explicit instruction). Published on the game
   thread by `MovementComponent::SyncLevitation()`, called from the buff apply
   hook (no exclusion) and the buff fade hook (fading spell excluded), and from
   a self flymode change (`entities_component.cpp` AT_FLYMODE handler). It
   publishes only on a flip, so a per-buff recompute of a non-levitating buff is
   a no-op. Reset silently on zone transition (`CleanupZone`), because the
   entity map the flymode reads from is cleared there and the buff list is
   re-applied from the profile on the way in.
3. **The renderer hover** — the bridge arm records
   `playerMovement_.isLevitating`; `PlayerCameraComponent::applyLevitationClamp()`
   (one call per frame after the lift carry) puts the feet on the hover line
   (`floor + kLevitationHoverHeight`) and descends from height at
   `kLevitationDescentRate` instead of free-falling. The pure decision is
   `ClampLevitationFeet()` in `ground_query.cpp`, beside its siblings. No-ops:
   not levitating, in water, mid-jump, or no collision data (a BSP miss is not
   a floor — the T440 lesson, so the feet keep falling exactly as a normal fall
   does). The server is told via the throttled `PlayerPositionChanged` intent
   (the lift-ride's ~250 ms rhythm), because a hovering player may be standing
   still and the ordinary movement sync only fires on a frame with input.
4. **The scenario** — `levitate_applies` (`scenario_levitate_applies.cpp`),
   pure logic over `IScenarioWorld`, registered in `kScenarios`. Prep is
   `#set invulnerable off` + `/target <self>`, then the GM `#castspell <id> 1`
   (see "Criterion 4" below for why the GM cast, not a gem), and waits until
   the buff is in the list **and** the client reports levitating
   (`IScenarioWorld::PlayerLevitating()`, read by the real
   `ApplicationScenarioWorld` from the `MovementComponent`'s stored state). A
   stale self flymode without the buff, and a preexisting buff, are not landings.

## The two motion numbers — [assumed]

`kLevitationHoverHeight` (1.5 units) and `kLevitationDescentRate` (10.0 units/s)
live in ONE table, `include/client/spell/levitation.h`. **Both are [assumed]**:
they are the classic client's behaviour as observed (hover roughly a foot off
the ground, a slow roughly-constant descent off a ledge), and no Z trace has
been recorded for them yet. They are the smell the process rules forbid, so they
are marked [assumed] here and in the header, not [measured]. The oracle capture
(acceptance criterion 3) is what re-points them to [measured] — see "Not
verified / left open".

## Offline evidence

**RED** (the pre-fix tree cannot build the new test — every name it uses was
absent). Built `tests/test_levitation.cpp` + `include/client/spell/levitation.h`
against unmodified `origin/fable` (`/tmp/b892-red`, same `-j3` Release tree);
`gmake ... Error 1` on 24 distinct compile errors, e.g.:

```
tests/test_levitation.cpp:142:55: error: ‘LevitationChanged’ is not a member of ‘eqt::state::GameEventType’; did you mean ‘VisionChanged’?
tests/test_levitation.cpp:192:23: error: ‘class eqt::movement::MovementComponent’ has no member named ‘PlayerLevitating’
tests/test_levitation.cpp:251:10: error: ‘class eqt::movement::MovementComponent’ has no member named ‘SyncLevitation’
tests/test_levitation.cpp:289:26: error: ‘ClampLevitationFeet’ has not been declared in ‘EQT::Graphics’
tests/test_levitation.cpp:346:10: error: ‘bool {anonymous}::LevFakeWorld::PlayerLevitating() const’ marked ‘override’, but does not override
```

The full list is `red_errors.txt` beside this file (in `/home/user/kb-evidence/
b892-levitation/`).

**GREEN** (this branch): `./bin/test_levitation` — **15 tests pass**
(`test_levitation_green.log` beside this file). The DB-gated wire tests (the SPA-57
apply→publish-up / fade→publish-down chain, the slot-3 row, the non-levitating
negative, the self-flymode-2 half) run against the real `data/willeq.db`, not a
skip. The offline predicate, clamp and scenario state-machine tests run with no
DB.

Neighbouring suites, all green on this branch:
- `test_reachability_sweep` — 5/5. `LevitationChanged` has a publisher outside
  the consumer TUs and a case in `IrrlichtBridge::applyEvent`, so the sweep's
  "every event has a publisher and a consumer" holds.
- `test_scenario_state_machines` — 52 pass (the shipped-scenario census is now 33;
  `levitate_applies` is asserted by name).
- `test_scenario_ui_legs`, `test_movement_component`, `test_buff_manager` — green.
- `python3 scripts/refactor/check_doc_counts.py` — "OK: every stated count
  matches the tree" (the `GameEventType` row now reads 107, updated in
  `CLAUDE.md` and `include/client/state/CLAUDE.md`; the scenario-census row is a
  test, not a doc count).

## Criterion 4 — the live scenario PASSED on lane 1 (exit 0)

`scripts/scenario.sh levitate_applies --spell 261 --timeout 300 --unit B892`
on lane 1 (Testchar, GM=1), 2026-10-08T07:44:

```
[SCENARIO] GM cast attempt 1: /say #castspell 261 1
[SPELL]    Levitation up: buffs=1, selfFlymode=0
[MAIN]     Cast Levitate (261) on yourself instantly.
[SCENARIO] VERDICT pass (exit 0) after 23.628s: spell 261 landed on Testchar
           and the client is levitating 0.068s after #castspell attempt #1
           (buff slots 0 -> 1, LevitationChanged events 1)
```

Snapshot restored byte-identically, lock released. The mechanism line
`Levitation up: buffs=1, selfFlymode=0` is the point of the card: the client
went levitating **from the buff list alone** — the self flymode stayed 0, which
is exactly what eqemu's `ignore_self` levitate appearance implies
(`code/zone/spell_effects.cpp:1452-1455`,
`SendAppearancePacket(FlyMode, Levitating, true, true)`).

The cast is the GM `#castspell <id> 1`, not a gem cast. The first lane-1 run
(07:36) used the T401 gem recipe and FAILED for a resource reason, not a
defect: `#memspell 261 0` succeeded ("Server memorised 'Levitate' (261) into
gem 1") but all three `/cast 1` were refused "Insufficient Mana to cast this
spell!" — Testchar (a GM) has GM invulnerability off but low mana, so the buff
never landed. `#castspell` calls `SpellFinished` directly
(gm_commands/castspell.cpp) — no mana check, no fizzle roll — and applies the
buff through the same OP_Action path a landed gem cast would. This is the card's
own recipe ("have a GM cast Levitate"). Full client log:
`/home/user/kb-evidence/b892-levitation/scenario_lane1_client_gmcast.log`.

## Not verified / left open

- **The two motion numbers are [assumed]** — hover height
  (`kLevitationHoverHeight = 1.5`) and descent rate
  (`kLevitationDescentRate = 10.0` u/s), in
  `include/client/spell/levitation.h`. They are the classic client's behaviour
  as observed (hover ~a foot off the ground, a slow roughly-constant descent);
  no Z trace has been recorded for them yet, so they are marked [assumed] here
  and in the header — not [measured], never invented.
- **Criterion 3 (oracle leg)** — **not done.** The lane-1 oracle capture that
  would re-point the numbers to [measured] (official client in-world as
  Testchar, GM-cast Levitate, a hover still + a walk-off-a-drop video, hover
  height vs model height, fall time) was **not completed**: the official client
  under Wine/Xvfb/llvmpipe on the oracle VM (172.16.200.91) could not be driven
  reliably through login — the SOE login form dropped the password keystrokes
  under slow llvmpipe and the client self-exited to the title screen on each
  attempt (a known gotcha in `~/src/willeq-notes/wine-oracle.md`), and the dev
  seat's window ran out before the capture landed. What blocked it: the
  password field was empty at submit in every full-drive attempt; title-screen
  LOGIN button and field geometry were re-measured but the login submit itself
  is the flaky step. No hover still, no drop video, no hover-height-vs-model or
  fall-time measurement, no willeq side-by-side. To finish it: the oracle recipe
  is `~/src/willeq-notes/wine-oracle.md` (lane 1: `export EQEMU_LANE=1`, oracle
  VM 172.16.200.91, GM-cast via the say-line `#castspell 261 1` once in world),
  then re-point the two constants in `levitation.h` and flip this [assumed] to
  [measured] with the frames cited.
- **Other levitators drawn hovering.** The server DOES send other entities their
  flymode, and the renderer already places each entity at the server-sent Z
  (no ground-snap), so a flymode-2 peer already renders at its server Z. What is
  NOT done is drawing the levitation *animation/bob* for them (the pre-existing
  `// TODO: Graphics rendering could show levitation animation` at
  `entities_component.cpp`) — that is the "draw hovering" half of the issue and is
  left open, matching the scope the issue's parallel legs assign to a separate
  lane (Leg E).

## B1011 (2026-10-09): the swim event's second writer, removed

The B892 review noted, as non-blocking, that `setSwimmingState()` still wrote
`playerMovement_.isLevitating` from `SwimmingStateChangedData::isLevitating`.
It was blocking: that field read the entity flymode -- which eqemu never sends
for a self-cast, B892's own root cause -- so a self-levitating player who
touched water had the hover flag cleared on entry AND on exit, and nothing
restored it (`LevitationChanged` fires only on a buff flip). B1011 deleted the
field, the publisher's flymode read and the parameter;
`PlayerCameraComponent::setLevitationState()` is the flag's one writer, and
`tests/test_movement_component.cpp::SwimmingEventCarriesNoSurfaceHeight` fails
if the swim payload ever carries levitation again.
