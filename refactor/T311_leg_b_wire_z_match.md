# T311 Leg B — send what the official client sends: `groundZ + 0.625 * size`

**Slot: slot1. Branch `task/T311-legB` cut from `fable`. Owns eqemu when it
takes the live leg (the lock file is FREE — take it atomically, release it).
Needs a two-client live session. No Pi.**

Executes **Will's ruling 3 (2026-09-01): the official client is the golden for
player position; willeq is updated to send what it sends.** Leg A measured what
that is. Read `refactor/T311_oracle_position_and_death_capture.md`'s Review IN
FULL before touching code — it is the contract, and it contains more than the
formula.

## What Leg A established, on the wire

```
wireZ = groundZ + 0.625f * size          // = 0.2f * size * 3.125f
```
plus a +0.001..0.003 collision epsilon. **No race term.** Canonical packet
(Human male, size 6, qeynos2 `(-74,428)`, server raycast `Best Z = -0.00`):
`z_pos@26 = 64 10 70 40 = 3.75100040435791`, against fable's `3.00`.

Trustworthy because Titanium's `DECODE(OP_ClientUpdate)`
(`titanium.cpp:2633-2653`) is `IN()`-only — no arithmetic — so the server-side
hexdump IS the client's transmitted value.

**Size is exactly linear** (`#size 3/6/12` → 1.8780 / 3.7510 / 7.5010;
Barbarian size 7 → 4.376). **Race is not a term:** under a Dwarf illusion the
server's `Fixed Z` moved to 28.84 (EQEmu's Dwarf constant 0.7) while the client
kept sending 31.75. So the client behaves as if every model used **3.125**, and
the agreement with `Mob::GetZOffset()` is a coincidence of EQEmu's default —
**do not reimplement `GetZOffset()`; implement the constant.**

## The structural point — two variables, not one

`0.625 * size` is **no landmark of the shipped mesh** (Human male origin-to-feet
3.004, half-height 3.339, crown 3.674). So the wire z and the render node origin
must become **separate variables**:

```
nodeOriginZ = groundZ + collisionZOffset          // unchanged (T310 Leg B)
wireZ       = nodeOriginZ - collisionZOffset + 0.625f * size
```

Do not "simplify" one into the other; T310 Leg B's `collisionZOffsetFromBounds()`
is measured from the mesh and is correct for rendering. Conflating them is the
exact mistake this arc has been unwinding.

## Sites (measured at `e68f8aa5` — re-verify against your tip)

**Outbound:** `src/client/graphics/renderer_player_physics.cpp:768`;
**`src/client/eq_bridge.cpp:754` — Leg A names this the cleanest conversion
point**; `src/client/eq_movement.cpp:1101`, whose comment at `:1100` is now
provably wrong and must die with the change.

**Inbound (the mirror):** `src/client/eq_entities.cpp:416` and `:1195`;
`src/client/spell/spell_cast_checks.cpp:53-54`;
`src/client/eq_movement.cpp:1741-1742`;
`src/client/graphics/entity_renderer.cpp:2227` (**the correct model — copy it**);
`include/client/graphics/entity_renderer.h:24-52` (the convention note, update it).

⚠ **Leg A flags the inbound mirror as an INFERENCE, not a measurement.** It is
to be settled by this leg's two-client acceptance, not assumed. If the two-client
result contradicts the mirror, report that and STOP rather than forcing symmetry.

## Also in scope (small, same subsystem, found by Leg A)

- `eq_movement.cpp:1741` computes `headZ` and **never uses it** —
  `findRegionForPoint` on the next line takes `PosZ()`. A live dead local in
  water detection. Delete it or use it; say which and why.
- **Wire heading is `heading × 4`** (the oracle's `#loc 480` rode as 1920).
  Leg A did NOT check whether fable packs the same scaling at
  `eq_movement.cpp:1113`. **Check it.** If it disagrees, that is a second live
  wire defect — report it; fix it here only if it is a one-liner, otherwise
  board it.

## Explicitly NOT in scope

**The death sequence.** Leg A answered it (see below) and it needs no code in
this leg. Do not implement a dead-state position policy here.

## Acceptance

- **Two-client live** (memory `two-client-live-testing` — single-client
  verification is circular): an OBSERVING second instance sees fable's player at
  the same height as the official client at the same surveyed spot. Screenshots
  + the server's `#loc` for both, to `/mnt/projects/fable/review/T311/legB/`.
- **A unit test pinning the expression** against size inputs — including a
  non-default size, since size-linearity is the measured property.
- A packet-level check: fable's own `OP_ClientUpdate` z at the canonical
  qeynos2 spot reads `3.751`, not `3.00`.
- Full `ctest -j6` (**NOT -j24**), zero new failures vs `refactor/test_baseline.txt`.
- `check_file_sizes.sh --files` no new FAILs.

## Territory

`src/client/eq_bridge.cpp`, `src/client/eq_movement.cpp`,
`src/client/eq_entities.cpp`, `src/client/spell/spell_cast_checks.cpp`,
`include/client/graphics/entity_renderer.h`, plus your test.
⚠ `src/client/graphics/renderer_player_physics.cpp` and
`src/client/graphics/entity_renderer.cpp` are **T312's territory** (slot2, the
thread-safety unit) — if T312 has not merged when you get there, coordinate
through the orchestrator rather than editing them concurrently.

---

# Review — Leg B (slot1, `task/T311-legB`, 2026-09-02)

**Code commit: `a69b5d06`.** Twelve files, one new header, one new intent, seven
new tests. Build/test/gate bar all PASS (table at the end).

**The live acceptance IS done.** eqemu was locked, taken and released; evidence
in `/mnt/projects/fable/review/T311/legB/` with a README that maps every file.
Two commits: `a69b5d06` (the conversion) and the drift fix + live evidence.

## What was implemented, and the one design decision in it

The constant, not `Mob::GetZOffset()`:

```
wireZ = groundZ + 0.625f * size          // == 0.2f * size * 3.125f
```

`include/client/position_frames.h` (new) is the ONLY expression of the
conversion in the tree. It carries the measurement, the reason there is no race
term, and the reason `0.625 * size` is not a mesh landmark.

**The two variables are separate, as the contract required:**

```
nodeOriginZ = groundZ + collisionZOffset   // unchanged; T310 Leg B measured it
wireZ       = nodeOriginZ - collisionZOffset + 0.625f * size
```

`collisionZOffsetFromBounds()` and `getPlayerCollisionZOffset()` were not
touched. GameState's player z **stays in the RENDER frame** and the conversion
happens only at the packet edges. That choice is deliberate and differs from
Leg A's suggestion of converting at `eq_bridge.cpp:754`:

> Converting at the bridge would make GameState wire-frame, which then needs the
> INVERSE conversion at every point where a player z re-enters the renderer —
> `IrrlichtBridge::applyEvent(PlayerMoved)` plus `application.cpp:1785` and
> `:1898` (the two zone-load snapshot hand-offs). Three sites, or one shared
> site inside `IrrlichtRenderer::setPlayerPosition()` — which is in
> `irrlicht_renderer.cpp`, a file T312 currently has dirty. Converting at the
> packet edges instead is two sites, both in this unit's own territory, and it
> leaves the renderer boundary untouched. Duplicated-then-drifted is this
> project's dominant defect class; two callers of one `constexpr` function is
> the smaller surface.

### The conduit for the mesh offset — the one structural addition

The conversion needs `collisionZOffset` (renderer-measured, from the shipped
mesh) and `size` (game state). Neither layer has both, and CLAUDE.md forbids
EverQuest holding a renderer pointer. So:

- new intent `PlayerModelGeometryChanged { float collisionZOffset; }`
- published from `renderer_frame.cpp`'s `processFrameSimulation`
- consumed in `ProcessBridgeIntents`, stored **once** in `PlayerState`
- `GameStateBridge::coalesceIntents` extended to collapse it latest-wins, so
  republishing every frame cannot grow the queue

It is republished every frame rather than on-change because caching "last
published" would need a new `IrrlichtRenderer` member and that header is T312's.
**Stated as a deviation, not absorbed:** the tidier shape is one extra field on
the existing `PlayerPositionChanged` intent, filled at
`renderer_player_physics.cpp:768`. That is T312's declared territory, so it was
not taken. Worth collapsing to that after T312 merges.

**Unknown offset (no model loaded) makes BOTH conversions the identity.** That
is the pre-T311 behaviour rather than a guessed height, and it has a second
benefit: a headless run has no renderer, so the offset stays 0, and headless
`PosZ()` already comes from the server in the wire frame — the identity is
exactly right there.

## Sites changed

| # | site | change |
|---|---|---|
| 3 | `eq_bridge.cpp:749` | comment only — says which frame arrives and where it converts |
| 4 | `eq_movement.cpp` `SendPositionUpdate` | `update.z_pos = renderZToWireZ(PosZ(), offset, size)`. The "Send Z position directly without offset" comment is gone; its replacement records why the accurate half of it was exactly why the other half was a defect |
| 5 | `eq_entities.cpp` ZoneSpawns self | `- size/2` → `wireZToRenderZ(entity.z, offset, entity.size)` |
| 6 | `eq_entities.cpp` NewSpawn self | same |
| 7 | `spell_cast_checks.cpp` | the `+ mySize/2` compensation replaced by one `renderZToWireZ` call, so both ends of the LOS ray are in the server's own frame (which is the frame eqemu's `CheckLosFN` runs in) |
| 10 | `entity_renderer.h` | THE Z CONVENTION note gains a T311 addendum: the measurement, the no-race-term evidence, and an explicit "these are two variables, do not fold them together" |
| — | `eq.h`, `player_state.h`, `renderer_intents.h`, `game_state_bridge.h`, `renderer_frame.cpp` | the conduit above |

Sites 1, 2 and 9 (`renderer_player_physics.cpp`, `entity_renderer.cpp`) were
**not** touched: T312's territory, and Leg A's own analysis says 1 and 9 are
already correct.

## Heading scaling — VERDICT: fable already agrees with the oracle. No defect.

Leg A measured the oracle's 12-bit heading field carrying **1920** where `#loc`
read **480.00**, i.e. `heading x 4`, and flagged fable's packing at
`eq_movement.cpp:1113` as unchecked.

Checked. `eq_movement.cpp:1046-1048`:

```
int heading_raw = static_cast<int>(server_heading * 2048.0f / 360.0f);
uint16_t heading_scaled = heading_raw % 2048;
```

`server_heading` is in DEGREES, so `deg * 2048/360 == (deg * 512/360) * 4` —
the EQ 0-512 value times four, exactly the oracle's scaling. The `% 2048` is
`FloatToEQ12`'s own modulo. **No second wire defect here; nothing changed.**

## `eq_movement.cpp:1741` `headZ` — VERDICT: Leg A misread it. It is NOT dead.

Leg A's "found and not fixed" item 1 says `headZ` is "computed and then never
used — `findRegionForPoint` on the next line is called with `PosZ()`". The next
line is, but `headZ` is read further down, at the `findRegionForPoint` inside
the `if (isWaterRegion)` block that decides **OnSurface vs Submerged**
(`eq_movement.cpp:1787`). It is live. Nothing was deleted.

The `6.0f` itself is questionable and is boarded rather than changed: `PosZ()`
is the node ORIGIN, and for a Human male the crown is only ~3.65 above it, so
this samples ~2.3 units above the head and keeps the player "on surface" longer
than it should. Correcting it moves a water threshold by three units with no
live coverage — a behavioural change in a subsystem this unit is not about. The
comment at the site now records both the correction and the open question.

## Tests

Seven new tests in `tests/test_position_frame_roundtrip.cpp` (the T88 file —
chosen so `tests/CMakeLists.txt`, which T312 has dirty, needed no edit). They
drive the **real wiring**, not a seam: a bridge, a real
`PlayerModelGeometryChanged` intent, `ProcessBridgeIntents()`, then
`SendPositionUpdate()` read back through the existing T88 seam.

| test | pins |
|---|---|
| `DefaultHumanoidOnZeroGround_TransmitsTheOfficialClientsValue` | the canonical Leg A packet: ground 0, size 6 → **3.75** on the wire, and the render z still 3.004. Also asserts the two are NOT equal — a re-conflation fails here |
| `NonDefaultSize_ScalesLinearlyWithNoRaceTerm` | sizes 3 / 6 / 7 / 12 → 1.875 / 3.75 / 4.375 / 7.5, against Leg A's four measured rows, with the mesh offset held fixed |
| `NonZeroGround_IsGroundPlusTheOffset` | ground 28 → 31.75 (Leg A's bind-point rows) |
| `DifferentMeshOffset_SameGround_SameWireZ` | three mesh offsets, same ground: the wire z must not depend on which mesh we wear |
| `UnknownMeshOffset_ConversionIsTheIdentity` | the no-model-loaded state |
| `RenderToWireToRender_IsIdentity` | 48 combinations — the inbound and outbound halves are an exact inverse pair |
| `TheConstantIsThreePointOneTwoFive_NotEQEmusPerRaceTable` | the constant, and size 0 meaning "reported none" |

The packet-level check the contract asks for is the first of these: it reads
`Test_GetLastComputedPositionUpdate().z_pos`, which is the struct that is
`memcpy`'d into the packet, and it reads **3.751-modulo-the-epsilon, not 3.00**.
The oracle's +0.001..0.003 settling epsilon is deliberately not reproduced.

## Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **PASS** (EXIT=0) |
| `ctest -j6` | **PASS** — 3330/3331; the only failure is `AudioManagerTest.PreloadCommonSounds`, the documented ALSA host quirk in `refactor/test_baseline.txt`. **Zero new failures.** |
| `scripts/build-gles2-x86.sh` | **PASS** (EXIT=0) |
| visual gate | **PASS**, goldens untouched (`git status` clean of any golden) |
| `scripts/build-arm-noble.sh` | **PASS** (EXIT=0) |
| `check_file_sizes.sh fable` | **PASS** — 11 files checked, 6 ok, 5 WARN, **0 FAIL**; every WARN is a pre-existing oversized file this unit only added comments to |
| ctest -j6, re-run after the live-found drift fix | **PASS** — 3332/3333, same sole ALSA failure, 2 tests added |
| visual gate (re-run) | **PASS**, goldens untouched |
| `build-gles2-x86.sh` / `build-arm-noble.sh` (re-run) | **PASS** / **PASS** |
| `check_file_sizes.sh --files` (this unit's 12 files) | **PASS** — 7 ok, 5 pre-existing WARN, **0 FAIL** |
| **two-client live acceptance** | **PASS** — observer sees fable 3.62 vs official client 3.75 at the same spot (was 3.00) |
| **real-Dwarf wire z** | **PASS** — 14.50 at ground 12.00, size 4; 17.00 at size 8; no race term |

## THE LIVE LEG

Full evidence and file map: `/mnt/projects/fable/review/T311/legB/README.md`.

### The real Dwarf (the coordinator's mid-unit addition) — the formula HOLDS

Leg A's "no race term" rested on races 2/8/12 as ILLUSIONS on a Human plus one
real Barbarian; races 10/128 were VOID and no real Dwarf had ever been tested.
One was provisioned: **`Oracledwarf`, race 8 Dwarf male Warrior, account
`willeq_dwarf`, character id 82.**

| measurement | ground (`Best Z`) | size | wire z | `0.625 * size` |
|---|---|---|---|---|
| default size, first login | 12.00 | 4 | **14.50** (`17 04 68 41` = 14.50004) | 2.500 ✔ |
| relogged with `character_data.z` forced to **3.0** | 12.00 | 4 | **14.50** again, within a second | 2.500 ✔ |
| `#size 8` | 12.00 | 8 | **17.00** (`0A 02 88 41` = 17.00298) | 5.000 ✔ |

The size is not assumed — the server's own `Fixed Z` gives it independently:
`12.56 - 12.00 = 0.56 = 0.2 * size * 0.7` (EQEmu's DWARF constant) => **size 4.0**.

**And the Dwarf is the discriminator.** EQEmu's per-race constant for it is 0.7,
not 3.125, and the server duly moved its own `Fixed Z` to 12.56 — while the
client went on transmitting 14.50. Exactly what Leg A's illusion predicted, now
on a real character of that race. **No race term. The constant is settled on
real evidence, and `Mob::GetZOffset()` is correctly NOT what was implemented.**

The forced-3.0 relog is the part that matters most: handed a wrong z, the client
transmits `ground + 0.625*size` unprompted. The client is the source.

### Two-client acceptance

Three participants in qeynos2 at the surveyed spot `(-74, 428)`, server raycast
ground `Best Z = -0.00`: the **official Titanium client** (`Oraclealpha`), **fable**
(`Testchar`, graphics, `--eqe1-path`, this branch) and **a second, independent
fable instance as the OBSERVER** (`Testbuddy`). The observer's `/list`, twice,
10 s apart, identical both times:

```
Oraclealpha (ID: 276) - ... at (428.00, -74.00, 3.75)     <- official client
Testchar    (ID: 285) - ... at (432.62, -74.38, 3.62)     <- fable, this branch
```

**Before this unit fable sat at 3.00 there — 0.75 below the official client.
It now sits 0.13 away.**

`/list` printed z with `{:.0f}` before this unit and would have shown "3" and
"4"; it now prints two decimals, because that listing is the instrument the
observer is read with.

### The residual 0.13 — what it is, and what it is not

It is **not** the conversion. fable transmits `ground_fable + 0.625*size`; the
oracle transmits `ground_server + 0.625*size`; the difference is entirely
`ground_fable - ground_server`. fable's renderer snaps to the zone MESH geometry,
eqemu raycasts its own `.map`, and at this spot they disagree:
`3.6306 - 3.751 = -0.1194`. **T310 Leg C independently measured that same gap as
0.119** ("feet 0.119 BELOW that ground"). A separate, pre-existing discrepancy
between two different collision surfaces — reported, not fixed here, and boarded.

fable's measured player mesh offset came out **3.125** for this character (derived
three independent ways from the live numbers), not T310 Leg B's 3.004 for the
Human male base mesh — a different model/scale. Nothing in the fix depends on the
value; it is an input, which is exactly the point of keeping it a variable.

### The defect the live run found — a COMPOUNDING drift

fable's transmitted z climbed **0.625 every login**: `3.63063` -> `4.25` ->
`4.880627` over three successive zone-ins of the same character, against the
oracle's steady 3.751.

Root cause: **the two conversions disagreed across TIME.** The self spawn arrives
BEFORE the player model, so the inbound conversion ran with an unknown mesh
offset — the identity — and stored a raw WIRE value. By the time
`SendPositionUpdate()` ran, the model HAD loaded, so the send converted that wire
value as though it were a render value and added `0.625*size` on top of a value
that already contained it. The server saves what a client sends and hands it back
next login, so it accumulated. (`0.625` exactly per login is how it was
identified: `3.75 - 3.125`.)

Fixed by `PlayerState::zAwaitingFrameConversion`: the self-spawn paths record that
the stored z has not been converted, the send path passes it through verbatim
while that is true, and `PlayerModelGeometryChanged` performs the deferred half
exactly once (idempotently) the moment the offset first becomes measurable —
firing `SetPosition` so the renderer gets the corrected value too. Two new tests
pin it, including the three-login loop that was measured drifting.

**This is the whole argument for the two-client bar.** The unit test suite was
green, the arithmetic was right in isolation, and the defect was invisible until
a real server handed the value back on the next login.

### The inbound mirror — held, with the limit stated

No contradiction appeared, so there is nothing to STOP on. But be precise about
what was and was not tested:

* **Tested.** The self-spawn inbound path (fable's own zone-in). It is now
  demonstrably consistent with the outbound path — the drift is gone and a
  standing zone-in is a fixed point. That is the half the mirror actually
  governs in this unit's code.
* **NOT tested.** The other-entity render conversion. The observer reads
  `/list`, which prints the raw wire z straight off the packet; it never
  exercised a render-frame conversion for another player. That conversion lives
  at `entity_renderer.cpp:2227`, which this unit did not change (T312's
  territory) and which Leg A already identified as the one site that states the
  rule correctly.

So the mirror is no longer an unverified inference on the path this unit touched,
and remains unmeasured on the path it did not.

## Found and deliberately NOT fixed

1. **fable's collision ground is ~0.12-0.13 below eqemu's map ground** at the
   qeynos2 survey spot (two independent measurements, T310 Leg C and this leg).
   Two different collision surfaces. Board it.
2. **`eq_movement.cpp`'s `HEAD_HEIGHT_OFFSET = 6.0f`.** Not dead (see above), but
   wrong by ~2.3 units against the mesh. Changing it moves a water threshold with
   no live coverage.
3. **`/moveto <entity>` moves the player to the WRONG place.** Driving
   `/moveto Oraclealpha` at a character standing 20 units from Oraclealpha put it
   at `(630, -158)`, ~200 units away — the client/server x/y frame swap, on the
   command surface T88 mapped for `/move`. Live, reproducible, out of scope here.
4. **`fable` moved a long way under this branch during the live session** — from
   `1fac024a` to `68e7f621` (T312, T313 and others merged). This branch is still
   cut from `1fac024a`. **T312 has now merged, so `renderer_player_physics.cpp`
   and `entity_renderer.cpp` are no longer contested**, and the tidier conduit
   named above (one `feetZ`/offset field on the existing `PlayerPositionChanged`
   intent, filled at `renderer_player_physics.cpp:768`, replacing the new
   `PlayerModelGeometryChanged` intent) is now available. Not taken here — it
   would be an unverified redesign after the acceptance had already passed.

## eqemu availability (the dispatch said the lock was free; it was not)

The live leg was delayed ~90 minutes:

| time (UTC) | `/mnt/projects/fable/.eqemu-lock` |
|---|---|
| 02:04 (first check) | `T312 2026-09-02` — taken 02:01:42, three minutes before I looked; the server processes had just been restarted |
| 02:19 | still `T312` |
| 02:26 | `T313 2026-09-02 slot3 executor: live loot-scroll acceptance` |
| 02:26–02:34 (polled atomically every 15 s) | still `T313` |

The take was attempted with `set -o noclobber` throughout and never overwrote
another session's lock. It succeeded at **02:34**, after which:

* the previous holder had taken eqemu fully DOWN (`docker compose down` removes
  the containers, and `status.sh` reported none); this leg brought it back with
  the sanctioned `scripts/eqemu/up.sh --launcher` and left it up.
* both live items were then completed — see THE LIVE LEG above.
