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

### Convention worry: CLEARED, confirmed with line evidence

Verified in place, not re-derived: succor/evacuate remain client-side
no-op stubs (`src/client/spell/spell_effects.cpp:274-278`, `:783-800`;
`SpellEffect::Succor = 88` has no case), so all succor/evac position work
runs through producer A (`ZoneProcessZoneChange`, `eq_zone.cpp`), fixed by
T52. Death/bind (`ZoneProcessZonePlayerToBind`, `eq.cpp:3392`, opcode
`HC_OP_ZonePlayerToBind = 0x385e`) swaps `bind_x = zone_y; bind_y =
zone_x` at `eq.cpp:3426-3427` — the same correct convention producers A/B
use. This is now covered by its own test,
`ZonePositionConvention.ProducerC_SwapsWireOffsetsIntoClientFormat`
(`tests/test_zone_position_convention.cpp`), closing T52's own documented
coverage gap ("Producer C has no test at all today"). **Nothing to fix on
the convention axis** — confirmed, not poked at further.

### Verified caller census for `SetPosition`

`grep -rn "SetPosition(" src/ include/` (re-run at this unit's start,
before any edit): the only definition (`eq.cpp:3109`, unchanged by this
unit) plus its declaration in `eq.h`; the only *callers* anywhere in
`src/` were the two added by this unit's Step 2 (`eq.cpp`'s
`ZoneProcessZonePlayerToBind`, `eq_login_world.cpp`'s
`WorldProcessZoneServerInfo`) — zero production callers existed before
this unit, confirming the plan's premise exactly.

### Same-zone death-respawn verdict: **CONFIRMED**

Pre-fix, `ZoneProcessZonePlayerToBind`'s same-zone branch
(`target_zone_id == m_current_zone_id`, or `== 0`) logged "Same-zone
respawn" but performed **no local position write of any kind** — not even
the raw `m_x/m_y/m_z` write other forced-relocation sites at least had.
Driven headlessly via `ReplayZonePacket` + a new `zone_id=0`
`ZonePlayerToBind_Struct` payload
(`ZonePositionConvention.ForcedRelocation_SameZoneDeathRespawnPropagatesLocally`),
the state layer's player position (read via the new
`Test_GetGameStatePlayerPosition()` accessor) and the legacy raw members
(`GetPosition()`) both stayed at `(0,0,0)` — the constructor's default,
completely unaffected by the replayed death packet. **Quoted pre-fix
failure** (production code temporarily reverted to capture this, then
reapplied — see "Pre/post test output" below) confirms the client would
have kept showing the character's pre-death position with no local
correction, relying entirely on whatever server `PlayerProfile` round-trip
eventually arrives via the full zone-to-zone reconnect flow
`ZoneProcessZoneChange` always triggers (T52's own finding: that flow
runs unconditionally for every approved zone change, same-zone included).
Fixed by routing both branches through `SetPosition`/`SetHeading`.

### The `PlayerProfile` decision: **left out, deliberately**

`PlayerProfile` (`eq.cpp:2154-2156` area) is a forced relocation by any
honest reading — it decides the actual landing position on every zone
entry per T52's Review — but it was **not** touched. Two reasons, both
stated as the plan asked: (1) it falls outside this unit's explicit
territory, which names only `SetPosition` and `ZoneProcessZonePlayerToBind`
within `eq.cpp` — not every position-writing function in the file; (2) on
inspection it turned out to have its own, separate T39-pattern gap worth
flagging rather than silently expanding scope to fix: it writes only the
raw `m_x/m_y/m_z` members and pushes `PlayerMoved` **directly onto the
GameStateBridge** (`m_bridge->pushEvent`, consumed by
`IrrlichtBridge`/`ConsoleBridge`) rather than through `SetPosition` or the
state layer's `PlayerState`/EventBus at all — meaning the state layer's
player position is *still* never updated on ordinary zone-in, even after
this unit. Given the plan's own flagged risk ("it is also on the login
path where an unexpected `PlayerMoved` could have ordering consequences"),
this is exactly the kind of thing worth a dedicated unit rather than a
drive-by. Recorded in `refactor/deferred.md`'s T55 addendum for the next
person.

### Sites changed

- `src/client/eq.cpp` — `ZoneProcessZonePlayerToBind`: added the WorldState
  pending-zone publish (mirrors producer B) and routed both the same-zone
  and cross-zone branches through `SetPosition`/`SetHeading` (previously no
  local write at all). Added `Test_GetGameStatePlayerPosition()` impl.
- `src/client/eq_login_world.cpp` — `WorldProcessZoneServerInfo`'s apply
  block: replaced the raw `m_x/m_y/m_z`/`m_heading` write with
  `SetPosition`/`SetHeading`. Still structurally dead code per T52 (fixed
  as hardening, same rationale T52 used for its own fix at this exact
  site).
- `src/client/eq_zone.cpp` — `ZoneProcessZoneChange` (producer A): added
  the WorldState pending-zone publish only, matching producer B
  (`eq_zone.cpp:765-767`). No other change to producer A.
- `include/client/eq.h` — added `Test_GetGameStatePlayerPosition()`, a
  narrow read-only accessor for `m_game_state.player().x()/y()/z()`
  (previously no public reader existed); GetState's public surface was
  not widened beyond this one read.
- `tests/test_zone_position_convention.cpp` — 3 new tests (producer C
  convention, cross-zone propagation, same-zone propagation) plus a
  `BuildZonePlayerToBindPacket` helper.

### Pre/post test output

Pre-fix (production `ZoneProcessZonePlayerToBind` temporarily reverted to
the tree's original raw-storage-only behavior, test file kept as written;
re-applied immediately after capturing this):

```
[ RUN      ] ZonePositionConvention.ForcedRelocation_DeathBindPropagatesToGameStatePlayerPosition
.../test_zone_position_convention.cpp:280: Failure
Expected equality of these values:
  statePos.x
    Which is: 0
  75.0f
    Which is: 75
... (y, z likewise: got 0, expected 50 / 3)
[  FAILED  ] ZonePositionConvention.ForcedRelocation_DeathBindPropagatesToGameStatePlayerPosition (319 ms)
[ RUN      ] ZonePositionConvention.ForcedRelocation_SameZoneDeathRespawnPropagatesLocally
.../test_zone_position_convention.cpp:312: Failure
Expected equality of these values:
  statePos.x
    Which is: 0
  20.0f
    Which is: 20
... (y, z, and the raw-member checks likewise: all got 0)
[  FAILED  ] ZonePositionConvention.ForcedRelocation_SameZoneDeathRespawnPropagatesLocally (1 ms)
2 FAILED TESTS
```

Post-fix, full file:

```
[==========] Running 7 tests from 1 test suite.
[ RUN      ] ZonePositionConvention.ProducerA_SwapsWireOffsetsIntoClientFormat
[       OK ] ...
[ RUN      ] ZonePositionConvention.ProducerA_DeniedChangeClearsPendingZone
[       OK ] ...
[ RUN      ] ZonePositionConvention.ProducerB_ZoneLinesPassesDestinationThroughUnswapped
[       OK ] ...
[ RUN      ] ZonePositionConvention.Consumer_AppliesPendingZoneWithoutASecondSwap
[       OK ] ...
[ RUN      ] ZonePositionConvention.ProducerC_SwapsWireOffsetsIntoClientFormat
[       OK ] ...
[ RUN      ] ZonePositionConvention.ForcedRelocation_DeathBindPropagatesToGameStatePlayerPosition
[       OK ] ...
[ RUN      ] ZonePositionConvention.ForcedRelocation_SameZoneDeathRespawnPropagatesLocally
[       OK ] ...
[  PASSED  ] 7 tests.
```

`ProducerB_ZoneLinesPassesDestinationThroughUnswapped` (the explicitly
required regression guard for the common zone-line-travel path) and
`Consumer_AppliesPendingZoneWithoutASecondSwap` (T52's own fixed-consumer
pin) both stayed green throughout, untouched behaviorally by this unit.

### Verification bar results

1. `cmake --build build -j24` (GLES2-config tree): clean full rebuild,
   only pre-existing unrelated `-Wpointer-arith` warnings in
   `eq.cpp`'s trade-packet code (untouched by this unit). One-time
   environment fix required and applied: `third_party/irrlicht-gles2-x86/`
   was present but stale (built 2026-07-31, missing the `eqtGPUPhaseBegin`
   family added 2026-08-10), causing an unrelated link failure; rebuilt via
   `bash scripts/build-gles2-x86.sh --irrlicht-only`, unrelated to this
   unit's source changes.
2. `ctest --test-dir build --output-on-failure`: **1916/1917 passed**, sole
   failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host
   exception). New tests included and green.
3. `bash scripts/build-gles2-x86.sh`: clean build, `willeq` +
   `gles2_zone_benchmark` produced.
4. `bash scripts/ci/visual_gate.sh`: `=== VISUAL GATE: PASS ===`
   (qeynos2/gfaydark/befallen). `git status --porcelain data/` empty
   before and after — goldens untouched.
5. `bash scripts/build-arm-noble.sh`: `=== Success ===`, all ARM binaries
   built including `build-arm-noble/bin/willeq`.
6. `bash scripts/refactor/check_file_sizes.sh fable` (run from inside the
   slot on the task branch): 5 files changed vs `fable` — `eq.h` 2063
   lines (WARN, **pre-existing**: `git show fable:include/client/eq.h |
   wc -l` = 2054, already over the 1500 threshold before this unit's
   +9-line addition, same pre-existing condition T52 documented at this
   same file); `eq.cpp` 3835 lines (FAIL, **pre-existing**: `git show
   fable:src/client/eq.cpp | wc -l` = 3807, already over the 3000
   threshold before this unit's +28-line addition); `eq_login_world.cpp`
   923 (ok), `eq_zone.cpp` 884 (ok), `tests/test_zone_position_convention.cpp`
   320 (ok). **No new FAILs or WARNs** — both flagged files were already
   over their thresholds at the `fable` base revision.

### Coordinator review correction (post-initial-landing): unconditional `SetPosition` on cross-zone was a defect, fixed

The first version of this unit's `ZoneProcessZonePlayerToBind` fix called
`SetPosition`/`SetHeading` unconditionally, above the `is_same_zone` branch.
The coordinator caught this: at the moment this handler runs, the client is
still fully zoned in to the OLD zone (`IsFullyZonedIn()` true,
`m_zone_connection` still live) — `ProcessDeferredZoneChange` doesn't
disconnect until *after* the `OP_ZoneChange` handshake this handler
triggers completes. `SendPositionUpdate()` (`eq_movement.cpp:958-963`) is
gated on exactly that same `IsFullyZonedIn()`+`m_zone_connection` check,
throttled only by a 250ms timer. So the unconditional write let the client
transmit the BIND ZONE's coordinates to the OLD zone's server on its very
next position update, which would accept and persist them — T52's central
lesson verbatim, reintroduced by this unit. Worse on the denied path
(pinned by `ProducerA_DeniedChangeClearsPendingZone`): a denied cross-zone
change would leave the character sitting in the old zone at coordinates
belonging to a different zone.

**Verified the coordinator's claim rather than taking it on faith**: traced
`IsFullyZonedIn()`'s definition and confirmed nothing in
`ZoneProcessZonePlayerToBind` (or anything it calls before the position
write) touches `m_zone_connected`/`m_zone_connection` — the disconnect
genuinely happens later, in `ProcessDeferredZoneChange`, gated on
`m_zone_change_approved` which this handler's own `RequestZoneChange` call
sets only after a further server round-trip. The claim held.

**Fix**: moved `SetPosition`/`SetHeading` inside the `is_same_zone` branch
only. The pending-zone store and the `setPendingZone()` WorldState publish
stay unconditional (correct for both branches — see main Review above).
Same-zone is safe because the coordinates are valid for the very server
that will receive them; cross-zone withholds the write and leaves the
far-side landing position to the zone-in consumer
(`WorldProcessZoneServerInfo`/`PlayerProfile`), exactly as the coordinator
specified.

**New regression test**: replaced the second propagation test (which had
asserted the now-known-wrong cross-zone write) with
`ZonePositionConvention.ForcedRelocation_CrossZoneDeathRespawnDoesNotWriteLocalPosition`
— replays `HC_OP_ZonePlayerToBind` with `zone_id=99` (≠ current zone) and
asserts both `Test_GetGameStatePlayerPosition()` and `GetPosition()` stay
at the constructor's `(0,0,0)` default, while `GetPendingZoneId/X/Y/Z()`
ARE populated (75, 50, 3 per the established swap convention). Verified
this test actually catches the regression: temporarily reintroduced the
unconditional call, rebuilt, and confirmed the test fails with
`statePos.x` = 75 (expected 0) etc. — 6 assertion failures, all showing the
bind-zone coordinates leaking into local position. Restored the
branch-gated fix immediately after. The same-zone test
(`ForcedRelocation_SameZoneDeathRespawnPropagatesLocally`) is unchanged and
still pins the original CONFIRMED finding.

**Re-ran the full verification bar after this correction** (see updated
results above, which supersede the first-pass numbers): `ctest`
1916/1917 (same sole ALSA exception); `visual_gate.sh` PASS, goldens
untouched; ARM cross-compile `=== Success ===`;
`check_file_sizes.sh --files <5 touched paths>` — `eq.h` WARN (2063
lines, still the same pre-existing condition) and `eq.cpp` FAIL (3853
lines, +46 vs the 3807-line `fable` baseline — grew slightly from the
expanded justification comment, still pre-existing, not newly triggered).
Note: the bare `check_file_sizes.sh fable` form now also reports files
from sibling units T54/T56 that merged into `fable` after this branch was
cut (`main.cpp`, `willeq_database.cpp`, two test files) — an artifact of
diffing against a `fable` tip that moved forward under this branch, not
anything this unit touched; used `--files` with this unit's actual 5
changed paths instead, per the protocol's own documented escape hatch.

### Deviations from the plan, flagged rather than absorbed

- **Irrlicht bootstrap re-run required.** `third_party/irrlicht-gles2-x86/`
  existed in the slot but was stale relative to the checked-out source
  (built before an unrelated 2026-08-10 renderer change), causing a link
  failure unrelated to this unit's files. Re-ran
  `scripts/build-gles2-x86.sh --irrlicht-only` per the protocol's own
  documented fallback. Not a T55 code issue.
- **No dedicated test for producers A/C's new WorldState pending-zone
  publish.** The plan's Step 2 asked for this fix (mirroring producer B);
  it is implemented (`eq_zone.cpp` producer A, `eq.cpp` producer C) and
  verified by inspection to match producer B's existing, already-tested
  call shape exactly, but adding a dedicated assertion would have required
  a new `m_game_state.world()` test accessor beyond the one the plan asked
  for ("do not widen GameState's public surface beyond what the assertion
  needs") — the propagation tests already exercise the player-position
  half of the same fix. Flagged rather than silently skipped.
- **No live eqemu acceptance leg** — correctly out of scope per the unit's
  own header ("NO EQEMU... fully headless"); all evidence above is
  `ReplayZonePacket`-driven.

Board updates: `refactor/deferred.md` — re-titled the succor/evac/death-bind
"implicated" Index bullet and appended a "T55 addendum" subsection to the
`## \`#goto\` same-zone teleport lands at the DB position` heading (the
original body left intact per the never-delete rule), documenting the
convention-cleared verdict, producer C added to the documented producer
list, the T39-T49 sweep-closed-with-this-hole-open note, and what remains
open by decision (per-frame movement writes; the `PlayerProfile` handler's
own separate, unfixed propagation gap).
