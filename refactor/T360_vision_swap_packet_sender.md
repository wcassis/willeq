# T360 — Vision switch un-swapped (B066); appearance/animation through PacketSender (B068)

Class A + B, slot2. Two disjoint items; one branch, two commits. Read the
board text of B066 and B068 (both measured live in T358; `review/T358/`).

## Leg A — B066

`SimulationWorker::applyVisionWeatherToZoneLights` and
`computeLightAnimations` (`src/client/graphics/sim_animation.cpp`) switch
on `visionType` with hand-written `case 1: // Ultravision`, `case 2: //
Infravision` — swapped against `EQT::Graphics::VisionType` (Normal 0,
Infravision 1, Ultravision 2) and `computeVisionType`. The header comment
on `SimulationInput::visionType` carries the same wrong mapping.

1. Replace the hand-written cases with the enum (cast the int to
   `VisionType` once; switch on the enum; no magic numbers survive).
2. Correct the header comment.
3. Test, pre-fix failing: Ultravision → intensity 1.0 / redShift 0;
   Infravision → 0.75 / 0.3; Normal → untouched. Drive the two functions
   through whatever seam the existing sim tests use.
4. Leave B067 alone (it needs the oracle — a separate unit); do NOT change
   the intensity values, only which arm they belong to.
5. Strike B066.

## Leg B — B068

`EntitiesComponent::SendSpawnAppearance` / `SendAnimation`
(`src/client/entities/entities_component_world.cpp`) call
`ctx_.zone_connection->QueuePacket(p)` directly, bypassing `PacketSender`,
so the session recorder never sees them and the death burst reaches the
server with the appearance BEFORE OP_Save (T349 asserts Save first).

1. Route both through `PacketSender` (the same call `SendSave` uses —
   `SafeQueueZone`); delete the explicit `Test_RecordQueuedPacket` line if
   PacketSender already feeds that seam (check; if it does not, keep the
   seam fed from ONE place).
2. Tests, pre-fix failing: (a) a recorded session (the R01 recorder's test
   seam) contains the C→S OP_SpawnAppearance after `SendSpawnAppearance`;
   (b) the death burst order at the PacketSender level is OP_Save, then
   SpawnAppearance, then ZoneChange (extend `test_self_death_sequence` or the
   G07 golden — a statement-text golden learns a reviewed edit IN PLACE, both
   faults re-planted).
3. Any T268 ordered-capture test must still pass unchanged.
4. Strike B068.

## Proof

Build, `ctest -j6`, the new tests with pre-fix/post-fix runs, G07 golden,
file size check; Leg A touches `src/client/graphics` → visual gate
standalone + ARM build. Tip carries willeq.db v6: rebuild all three trees
first.

STOP: `PacketSender` cannot be reached from `EntitiesComponent` without a
new context service (then report the shape; do Leg A).

## Review

Branch `task/T360-vision-swap-packet-sender`, cut from `fable` at `0598fa23`
(verified). Two commits, one per leg:

| leg | commit | subject |
| --- | --- | --- |
| A (B066) | `3bca791c` | un-swap the vision-type -> zone-light mapping |
| B (B068) | `140acbd7` | appearance and animation go out through PacketSender |

No STOP fired. `PacketSender` was reachable from `EntitiesComponent` exactly as
`SendSave` reaches it (`ctx_.net`), so Leg B needed no new context service.

### Leg A — B066

The two switches in `src/client/graphics/sim_animation.cpp` were replaced by a
single `visionLightModifiers()` in a new leaf header
`include/client/graphics/vision_type.h`, which switches on `VisionType`. No
magic number survives at either call site, and the `uint8_t` the worker
boundary carries is cast to the enum in one place (the `uint8_t` overload); an
out-of-range value — including the `255` "not yet applied" sentinel
`cachedVisionType_` starts at — falls through to the `Normal` default, which is
what the old `default:` arm did.

**Deviation, flagged.** `VisionType` had to MOVE out of `renderer_types.h`.
`renderer_types.h` includes `simulation_worker.h`, so the dependency only runs
one way and `sim_animation.cpp` could not otherwise name the enum without a
cycle. It now lives in the new leaf header, which `renderer_types.h` includes
and re-exports, so every existing spelling — `EQT::Graphics::VisionType` and
`IrrlichtRenderer`'s re-export — resolves unchanged. The enum's own doc comment
travelled verbatim.

The comment on `SimulationInput::visionType`, which said
`0=Normal, 1=Ultravision, 2=Infravision` and is presumably where the swap came
from, now states the correct order and points at `visionLightModifiers()`.

**The intensity numbers were not touched** — only which arm each belongs to.
B067 (vision never reaching the global ambient) is untouched and still open.

**Test: `tests/test_vision_zone_lights.cpp`** (177 lines, new target in
`tests/cmake/renderer_guards.cmake`). It drives the two REAL production
functions through the public `computeOnce()` seam rather than reimplementing
the mapping — both are observable from a `SimulationOutput`:

- zone light **0** has no animation, so `applyVisionWeatherToZoneLights` writes
  its `diffuseColor` and `computeLightSelection` copies that into
  `output.selectedLights[]`;
- zone light **1** has a one-entry `ZoneLightAnimData`, so
  `computeLightAnimations` writes `output.zoneLightAnimColors` directly.

Both base colours are pure white, so every expected number is the modifier
itself. `loadingActive = true` takes `computeAll`'s culling-free branch (no BSP
tree, no portals, no occluders needed) and three `computeOnce()` calls reach
the background tier (`kBackgroundInterval == 3`) that owns
`computeLightAnimations`. No Irrlicht device, no thread.

| test | pre-fix | post-fix |
| --- | --- | --- |
| `VisionZoneLightsTest.UltravisionIsFullIntensityWithNoRedShift` | **FAIL** — read 0.975 / 0.6375 / 0.525 (Infravision's arm) | PASS |
| `VisionZoneLightsTest.InfravisionIsThreeQuarterIntensityRedShifted` | **FAIL** — read 1 / 1 / 1 (Ultravision's arm) | PASS |
| `VisionZoneLightsTest.NormalVisionIsTheQuarterIntensityDefault` | PASS (this arm was never wrong; it is here so a future edit cannot "fix" the swap by rotating all three) | PASS |
| `VisionTypeEnumTest.UnderlyingValuesAreNormalZeroInfraOneUltraTwo` | PASS | PASS |

`2 FAILED TESTS` before, `[  PASSED  ] 4 tests.` after — the failure is exactly
the swap T358 measured live, and it shows on BOTH functions in one run.

### Leg B — B068

`SendSpawnAppearance` and `SendAnimation` now call `ctx_.net.SafeQueueZone`.
`SafeQueueZone` feeds the T268 ordered-capture seam BEFORE its gates and the
session recorder after them, so the hand-written
`ctx_.net.Test_RecordQueuedPacket(...)` line T346 added to `SendSpawnAppearance`
was **deleted**: the seam is now fed from ONE place, for both senders, and the
recorder is fed from that same place. `SendAnimation` keeps its unreliable
stream-0 send (`SafeQueueZone(p, 0, false)`). Both senders also gained
`SafeQueueZone`'s null-connection guard, which the direct
`ctx_.zone_connection->QueuePacket(p)` did not have.

**How the recorder seam is fed now.** One call site each:
`EntitiesComponent::SendSpawnAppearance` / `SendAnimation` ->
`PacketSender::SafeQueueZone` -> (T268 `Test_RecordQueuedPacket`, then the
`m_session_recorder->record(Zone, ClientToServer, …)` line, then the
main-thread / `QueuePacketAsync` split). Nothing records a packet by hand any
more.

**Tests**, in `tests/test_self_death_sequence.cpp` (740 lines):

| test | pre-fix | post-fix |
| --- | --- | --- |
| `SelfDeathSequence.RecordedSession_ContainsTheSpawnAppearance` | **FAIL** — `appearances` 0, `animations` 0 | PASS (1 and 1, bodies intact: spawn id, type `AT_ANIMATION`, param 115) |
| `SelfDeathSequence.RecordedSession_MatchesTheOracleSequence` | **FAIL** — recorded burst was 2 lines, `C->S OP_Save` / `C->S OP_ZoneChange`, against the golden's 3 | PASS |
| the other 12 in the file, incl. `T311Capture_OutboundSequenceMatchesTheOracle` (**G07**) and `SaveBody_MatchesTheCaptureGolden` (G08) | PASS | PASS |

The first opens a real `SessionRecorder` onto a temp `.sleq`, sends one
appearance and one animation, closes, and reads the file back. The second
replays the whole T311 death burst and projects the RECORDED C->S zone stream
through the same projection, against the same golden — so
`self_death_outbound_g07.txt` now serves two observers of the same burst: the
T268 seam ("the client decided to send this") and the recorder ("this went out
through `PacketSender`"). To make that possible the projection was lifted out
of `DeathHarness` into a free `ProjectDeathSequence()` over an
`OutboundPacket` list; the projection logic itself is byte-identical.

**The golden's three compared lines are UNCHANGED.** Its header learned the
second observer IN PLACE (a hand-written paragraph; nothing regenerated, and
the "Never re-blessed" line is still there). The two faults it pins — the
three-step order with `OP_Save` first, and the absence of any
`OP_ClientUpdate` — are both still planted and both still checked, now from
two directions.

**Deviation, flagged: a fixture consequence, not a defect.**
`test_forced_pose_no_echo`'s `PoseHarness` observes the outbound stream through
the `DaybreakConnection` resend map, one layer BELOW `SafeQueueZone` — and
`SafeQueueZone` refuses to send over a connection whose stats are all zero
("appears invalid"), which is exactly what a `FakeUdpTransport` connection that
has never round-tripped looks like. Three of its tests
(`UserSit_StillSendsExactlyOneAppearance`,
`UserStand_StillSendsExactlyOneAppearance`,
`T311SelfDeathCapture_SetsDeadAndSendsLyingNotAtDie`) went red on that
production guard rather than on the code under test. `PoseHarness` now sets
`conn->m_stats.sent_packets = 1` — the same line, with the same comment, that
`DeathHarness` has carried since T346. All 8 pass. No assertion was weakened.

### Verification

| step | result |
| --- | --- |
| `cmake --build build -j24` (GLES2 config) | exit 0 |
| `ctest --test-dir build -j6` | **3865 / 3866**, sole failure `AudioManagerTest.PreloadCommonSounds` (the ALSA host quirk) |
| `bash scripts/build-gles2-x86.sh` | exit 0 |
| `bash scripts/ci/visual_gate.sh` (standalone) | **PASS**, goldens untouched (`git status` clean) |
| `bash scripts/build-arm-noble.sh` | exit 0 |
| `bash scripts/refactor/check_file_sizes.sh fable` | 40 files checked, **40 ok / 0 warn / 0 fail** |
| G07 `self_death_outbound_g07.txt` | both consumers PASS |

No eqemu lock taken; nothing here needed a server.

### Two things worth carrying forward

1. **The first full build hit exit 143 at the `ar` step and DELETED
   `build/lib/libwilleq_core.a` outright** — the absence shape the protocol
   warns about, not the truncation shape. The relaunched build restored it; the
   post-143 sweep then reported `checked=271 bad=0` over `build/bin/*` (ELF
   magic) and `build/lib/*.a` (`ar t | wc -l`). It also left an orphan `ar` temp
   `build/lib/stn9z53V`, which was removed.
2. **`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` failed
   in the first `-j6` suite run and passed standalone** — its log line was
   `failed to open data/willeq.db … (sqlite rc=14)`, i.e. contention, not a
   defect and nothing to do with either leg. Same class as the `visual_gate`
   CPU-p95 load artefact the protocol names; worth adding to that note.
