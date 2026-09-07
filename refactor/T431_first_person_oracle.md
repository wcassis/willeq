# T431 — first-person eye placement matched to the official client (B018)

Class B; eqemu lock (atomic; must be ABSENT). Read B018,
`refactor/T301_first_person_eye_placement.md` (written, never dispatched),
T307/T309 (the measured attack-animation draw rule — do not revive the
refuted near-plane/viewmodel ideas; see the memory note), T405's rig
(`#goto x y z h` pins position and heading on BOTH clients), and
`src/client/graphics/entity_first_person_draw.cpp` + the camera code.

1. Oracle: first-person view (the official client's zoom-in-to-first-
   person key) at T405's spot, heading pinned, day: a still + a slow
   turn video; the same on fable. Compare the eye height (where the
   horizon and a known landmark sit in the frame), the pitch, and the
   near geometry visible at the bottom of the frame.
2. A/B fable's eye height (currently the static bbox crown) and any
   offset against the oracle frame until the landmark rows match within
   the noise floor; every value carries its probe. Race matters (eye
   height differs per model) — measure two races.
3. Tests pin the solved placement per race (pre-fix failing). Strike B018.
   Restore server values (world DOWN); `down.sh`; delete the lock.

STOP: lock present; the official client's first-person key cannot be
driven under Wine (report; capture third-person as a control).

---

## Review

Executed slot3, branch `task/T431-first-person`, base **`7f1f3ca9`**. Media,
frames, curves and the full write-up: **`review/T431/README.md`**.

### Verdict against the three asks

| ask | verdict |
|---|---|
| 1. oracle first-person still + slow turn at T405's spot, compared to fable | **PASS on the still, PARTIAL on the turn.** The stills are `review/T431/oracle/`; the "slow turn" video is a HOLD, not a turn — `eqclient.ini` maps turning to `a`/`d` (scancodes 30/32), not the arrow keys, and that was found after the recording. The heading comparison was made instead by a four-heading `#goto` sweep per race, which is a stronger pin than a turn. |
| 2. A/B fable's eye height against the oracle until the rows match, two races | **PASS.** Human male solves at skull fraction 0.43–0.45 (eye 6.02 ± 0.08 above its own feet); Dwarf male at 0.08–0.10 (3.58 ± 0.14). Every value carries its curve and its `[FPCAM-DUMP]`. |
| 3. tests pin the solved placement per race, pre-fix failing; strike B018 | **PASS**, with the per-race half landing as a MEASURED NEGATIVE — see below. |

### What changed, and where

| file | change |
|---|---|
| `include/client/graphics/first_person_eye.h` | `kEyeHeightFractionOfSkull` **0.65 → 0.45**, `kCrownAboveEyeUnits` 0.49 → 0.765, `kEyeHeightFractionOfStandingHeight` 0.925 → 0.887; all three comment blocks rewritten to carry T431's method, curve, error bars and history |
| `tests/test_first_person_eye_anatomy.cpp` | three new T431 tests; four existing tests that had the old constants baked in learned the new ones in place |
| `tests/test_first_person_tunables.cpp` | the literal defaults table |
| `docs/module_map.md` | registered `src/client/graphics/diagnostics/scene_node_census.cpp` — **a pre-existing red on the tip**, not this unit's: T443 added the TU and never listed it, and `DocCounts` had been failing for it |
| `README.md`, `docs/decisions.md`, `refactor/BOARD.md` | B018 struck; B470–B473 opened |

No runtime code changed at all: the eye derivation, the head-bone anchor, the
posture rule and the draw rule are untouched. Only the three numbers moved.

### Why 0.65 was stale, and why that is not a contradiction of T310

T310 Leg C's answer was an ABSOLUTE — "eye 6.18 ± 0.08 above the collision
ground" — and 0.65 produced exactly that on T310's tree. **T310 Leg B's own
`collisionZOffset` fix then landed**, taking the player from `feetEqZ=-0.119` to
`feetEqZ=0.000`, which raises every eye 0.119 units against the ground; nobody
re-solved the fraction keyed to the absolute. 0.119 of the measured 0.28 gap is
that, exactly. The remainder is inside the two sessions' error bars.

**B399's heading fix is confirmed live in passing.** T310 had to pose fable at
server heading 480 to reproduce the official client's heading-160 frame. T431
poses both at 160: the `[FPCAM-DUMP]` reports `headingEqUnits=480.000000
headingDeg=337.500000` — the documented `90 − server_heading_deg` applied once —
and the frames line up.

### The second race: a measured NEGATIVE (B470)

The plan said race matters. It does, and the parameterisation does not survive
it: Human male 0.44, Dwarf male 0.09, from the same spot by the same method.
The Dwarf's eye moves 2.74 units per unit fraction against the Human's 1.40,
because its beard is skinned to `dwmhe` and roughly doubles the span the
fraction is taken across — `max(minZ, -maxZ)` tames it but not enough. The
committed value is the Human male's and the Dwarf is left ~1.0 units high.
`OneSkullFractionCannotSatisfyBothMeasuredRaces` pins that so it cannot be
reported closed on arithmetic. **A per-race table is not the fix** and the
header says so.

### The trap that cost two sweeps, and it is now a board item

A player CORPSE (`character_corpses` id 42, Oraclealpha, since 2026-09-05) lies
1.9 units from the canonical `(-74, 428)` measurement spot in qeynos2. It is
**invisible to a Human male and fills 8–27 % of a Dwarf male's frame**, because
at level pitch the down-FOV is vFOV/2 = 24.6° and ground-level geometry at
distance `d` is in frame only while `d > 2.18 × eye height`. It looked, for two
sweeps, like the official client drawing the player's own body — i.e. like a
contradiction of T307/T309's draw rule. **It is not, and that rule stands.** The
sequence that identified it (walk, parallax ladder, `/sit`, the corpse's own
nameplate, the server row) is `review/T431/README.md` §4. Board **B472**.

The same frames show fable drawing that corpse **standing upright** where the
official client lays it flat — board **B473**, a live defect nobody had named.

### Tests

| test | pre | post |
|---|---|---|
| `ShippedMeshEyeTest.HumanMaleEyeMatchesTheT431OracleSolve` | **FAIL** (6.187 vs 5.915) | PASS (5.907) |
| `ShippedMeshEyeTest.DwarfMaleEyeIsUnderTheT431OracleUpperBound` | **FAIL** (5.320 > 4.820) | PASS (4.773) |
| `ShippedMeshEyeTest.OneSkullFractionCannotSatisfyBothMeasuredRaces` | PASS (a pinned negative) | PASS |
| `FirstPersonEyeGeometry.ABeardIsNotMistakenForTheSkull` | **FAIL** against the new constants | PASS |
| `ShippedMeshEyeTest.StandingHeightCrossCheckAgainstLegAsPoint925` | **FAIL** against the new constants | PASS |
| `FirstPersonEyePosture.SitDropIsLegAsMeasuredFractionAndStandingIsZero` | **FAIL** against the new constants | PASS |
| `FirstPersonEyePosture.SitDropIsAboutThreeAndAHalfUnitsForAHumanSizedBody` | **FAIL** against the new constants | PASS |
| `FpTunablesTest.TheDefaultsAreTodaysMeasuredNumbers` | **FAIL** against the new constants | PASS |

Logs: `review/T431/logs/test_eye_anatomy_PREFIX.log` (the three new tests
against the OLD constants, 2 failed) and
`test_eye_anatomy_OLDTESTS_NEWCONSTANTS.log` (the four pre-existing tests
against the NEW ones, 4 failed — produced by reversing this branch's test-file
patch with `git apply -R`, never `git stash`), against
`test_eye_anatomy_POSTFIX.log` (23 of 23).

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | exit 0 |
| `ctest -j6` | **4,645 of 4,645** (`review/T431/logs/ctest.log.gz`). Three earlier runs died at gtest DISCOVERY on a different binary each time (`test_loot_window_actions`, `test_zone_load_component`, `test_first_person_tunables`) with the 5 s discovery timeout and an empty output — the documented cold-cache/contention artefact, with `load average: 26.96` from the other slots. Every one of the 343 binaries in `build/bin` carries a valid ELF magic and `--gtest_list_tests` cleanly. Warming the page cache (one `--gtest_list_tests` per binary) fixed it. Two real reds were found and fixed on the way: `FpTunablesTest.TheDefaultsAreTodaysMeasuredNumbers` (this unit's own) and `DocCounts` (pre-existing on the tip, T443's unlisted TU). `check_build_tree_orphans` failed on `test_door_motion` / `test_jump_relocation` — T438's binaries left in this slot's `bin/`, deleted. |
| `bash scripts/build-gles2-x86.sh` | exit 0 |
| visual gate, standalone | **PASS**, no golden touched (`review/T431/logs/visual_gate.log`) |
| `bash scripts/build-arm-noble.sh` | exit 0 (`review/T431/logs/build_arm.log.gz`) |
| `check_file_sizes.sh fable` | 51 files checked, 51 ok, 0 warn, 0 fail |

### Server state

Every value restored with the world DOWN (only mariadb up) and verified by a
full `diff` of the `character_data` dump — **IDENTICAL**. Weather, the Dwarf
account's GM status, both characters' poses and corpse 42's `is_buried` are all
back as found. One row was CREATED and cannot be restored to absence: corpse
id 44, from the rig character being killed at the spot by a wandering NPC; it is
left buried and is recorded in B472. `down.sh` exit 0, no akk-stack container,
lock deleted.

### Deviations

* **The "slow turn" video is a hold.** Named above; the heading question was
  answered by a `#goto` heading sweep instead.
* **The Dwarf's reference frame is at `(-70, 428)`, not `(-74, 428)`** — four
  units east, so the corpse is out of frame. Its own oracle frame and every
  fable candidate scored against it use that same pose, so the solve is
  internally consistent; it is only the two races' spots that differ.
* **A third race was not measured.** Iksar (`Oracleecho`, race 128) and
  Barbarian (`Oraclebravo`, race 2) exist on the server and would have sharpened
  B470, but the corpse cost two sweeps and the session ran out of budget.
