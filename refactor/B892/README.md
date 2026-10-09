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

## The two motion numbers — [measured]

`kLevitationHoverHeight` and `kLevitationDescentRate` live in ONE table,
`include/client/spell/levitation.h`. **Both are [measured]** by the lane-1
oracle capture (criterion 3, below) and carry the measured value with the
method in the header comment:

- **`kLevitationHoverHeight = 1.5f`** — the hover LINE above the floor (feet).
  The classic client's self-levitation does not sit at a fixed height; it
  **bobs** about the line (C→S z: feet 1.458 low .. 4.556 high, period ~4.4 s).
  fable's renderer does not animate the bob (it pins the feet at the line and
  descends to it), so the line it holds is the bob's **low** — the point the
  classic client re-anchors the feet at every cycle — **1.458 ≈ 1.5**.
- **`kLevitationDescentRate = 1.3f`** — descent (units/s) of the feet when they
  are above the hover line. The capture's 33 high→low levitation legs descend
  at a median **1.294 u/s** (range 0.697–1.487; the spread is 1 Hz wire
  sampling jitter on a ~2.6 u drop). The old assumed 10.0 u/s was ~8× too fast.

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

## Criterion 3 — the lane-1 oracle capture landed (2026-10-09)

The official client, in-world as Testchar (GM=1), East Freeport (freporte,
zone_id 10) on the lane-1 oracle VM (172.16.200.91, Wine/Xvfb/llvmpipe,
`LP_NUM_THREADS=1`), GM-cast `#castspell 261 300` via the say-line. The buff
landed (the Effects window shows the Levitate icon —
`refactor/B892/evidence/b892_hover_bufficon.png`) and the client's own
computed position streamed on the C→S `OP_ClientUpdate` wire.

**Method** (same rig as B893): eqemu packet logging (categories 83/84,
`log_to_file=3`) dumps every C→S `OP_ClientUpdate` to the freporte zone log;
the 48-byte `PlayerPositionUpdateClient_Struct` decodes `z_pos` at offset +26.
The player stood still at (x=20, y=5.0) before the buff (593 constant C→S
samples, all at z=−38.2157, 05:10:17..05:21:40), giving the standing floor
**−38.2157** — byte-identical to B893's floor at the same spot. The
`#castspell 261 300` say-line GM-cast applied the buff in the 05:20:48 burst of
S→C `OP_Action` packets (the Effects window shows the Levitate icon —
`b892_hover_bufficon.png`), and the player reached (x=20, y=3.1554) at 05:21:41
already levitating: the C→S z there bobbed from the first sample. The capture
carries "no longer levitate" `OP_SpawnAppearance` pairs at 05:23:56 and
05:33:49 (a GM expiry / re-cast cycle); the analysis windows below
(05:34:10+ / 05:35:30+) sit after the 05:33:49 event, inside a levitation span
in which the buff icon was up and the bob's low had settled near 1.458 u. The
two spots are ~1.85 u apart in y on flat East Freeport ground, so the y=5.0
floor is the reference for the bob-spot heights (a flat-ground assumption the
constant y=5.0 samples make as explicit as the wire can). Evidence: `refactor/B892/evidence/` —
`b892_levitate_wirecap_freporte.log` (the raw C→S dump, the capture window),
`b892_levitate_wirecap.decoded.txt` (one `ts seq x y z` line per packet),
`b892_verify_decode.py` (the independent raw decode: confirms the decoded file
is byte-identical to the raw log, 1494/1494 records, and recomputes every
cited number — the strongest artifact in this directory),
`b892_analyze_histo.py` + `b892_analyze_descent.py` (the two analyses), and the
buff-icon still.

**The two numbers, read off that wire:**

| quantity | value | source |
|---|---|---|
| standing floor (feet, at y=5.0) | −38.2157 | 593 constant C→S samples, 05:10:17..05:21:40 |
| hover height, low of bob | 1.458 u | min of C→S z − floor over the 75-sample resting window (05:35:30+) |
| hover height, mean of bob | 2.989 u | mean / median 2.939 of the same window |
| hover height, high of bob | 4.556 u | max of C→S z − floor |
| bob period | ~4.4 s | 17 upward crossings over the 75-s window |
| **descent rate (high→low legs)** | **1.294 u/s median** | 33 legs over the 149-sample window (05:34:10+), range 0.697–1.487 (1 Hz dt jitter) |

**`kLevitationHoverHeight` = 1.5** (the bob's LOW — the point the classic
client re-anchors the feet at each cycle; fable's renderer pins the feet at
the line between bobs). **`kLevitationDescentRate` = 1.3** (the median
high→low leg velocity; the old assumed 10.0 was ~8× too fast). Both constants
carry `[measured]` in `include/client/spell/levitation.h` with this method in
the comment; the levitation clamp test reads the constants symbolically (no
literal), so it is value-agnostic and re-verifies the new numbers.

The login flake that blocked the 2026-10-08 attempt (password keystrokes
dropped under slow llvmpipe) did not recur: `LP_NUM_THREADS=1` was set and the
in-world GM commands were driven by **clipboard paste + a vision check of the
say-line before each submit** (the `#` keysym alone kept getting mangled to
other characters, so the whole command went through `xclip` → `ctrl+v`,
verified by OCR/vision of the input field, then Return).

## Left open

- **Other levitators drawn hovering.** The server DOES send other entities
  their flymode, and the renderer already places each entity at the server-sent
  Z (no ground-snap), so a flymode-2 peer already renders at its server Z.
  What is NOT done is drawing the levitation *animation/bob* for them (the
  pre-existing `// TODO: Graphics rendering could show levitation animation` at
  `entities_component.cpp`) — that is the "draw hovering" half of the issue and
  is left open, matching the scope the issue's parallel legs assign to a
  separate lane (Leg E). **Out of scope for this card** (the operator's
  2026-10-09 dispatch names it OUT of scope explicitly).
- **The levitation bob is not animated by fable's renderer.** The classic
  client bobs ~4.4 s about the hover line (feet 1.458 .. 4.556); fable's
  `ClampLevitationFeet()` pins the feet at the hover line between any motion,
  so a levitating player in fable sits still at 1.5 u rather than bobbing.
  That is the same "animation" gap as the other-levitator case above and is
  left open with it (a bob would be a renderer-side animation, not a change to
  either of the two measured constants).

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
