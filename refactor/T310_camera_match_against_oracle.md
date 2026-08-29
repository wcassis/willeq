# T310 — Solve the first-person camera by MATCHING the official client's output

## The instruction this unit exists to follow

Will, 2026-08-29: *"WillEQ's graphical output is meant to match the official
client's graphical output. We are using the same assets as the official client.
We control the EQEmu server. There are no other players using the server. We can
set the absolute position a player is at (including heading) so that the
graphical output from two sessions is identical for the purpose of comparing
renders. ... adjust willeq's output until it matches the official client exactly.
Then you'll have your camera position. ... This should not require recompiling.
Add whatever you need to get this done efficiently."*

**The official client's output is the golden. The camera pose is SOLVED from it,
not argued from consequences.**

## Two errors this unit corrects, both mine, both load-bearing

**E1 — "pixels underdetermine the camera pose" is FALSE.** It is only true with
nothing near the camera. Standing at a wall, a 0.9-unit forward translation of
the eye is a large and obvious change in the drawn world geometry. Same assets,
same zone, same server-set position/heading, same resolution => the two frames
are comparable directly, and the camera pose is recoverable by search.

**E2 — "fable centres the model on server z, the official client stands it
feet-on-z, a ~3-unit disagreement" is FALSE.** That number is the mesh's own
origin-to-feet distance (Human male: origin 3.02 above feet on a 6.67 span), an
internal fact about the model, misread as a cross-client frame difference. Both
clients take z from the same EQEmu server via the same packets. The belief is
written into the CODE, not just the docs:
- `include/client/graphics/entity_renderer.h:119` "Offset from server Z (model center) to feet"
- `src/client/graphics/entity_renderer.cpp:2222` "Server Z is the model CENTER, not feet."
- `src/client/graphics/renderer_player_physics.cpp:243` "Server Z represents the CENTER of the model"

E2 matters beyond bookkeeping: that fabricated disagreement is what
`first_person_eye.h:95-102` cites to REJECT the 0.925-of-height eye derivation in
favour of `kCrownAboveEyeUnits = 0.60`. A measured constant was chosen against a
premise that is not real. **Do not preserve that choice out of deference — re-derive
it against the oracle.**

## Standing rules for every leg

- **Never self-declare a visual acceptable.** Capture it, say what it is, stop.
- **Video, stepped frame by frame. Stills are barred in this arc** (a still-sampled
  null was wrong once already; the draw rule's duty cycle is 22.5%).
- Oracle traps from T307 section 6: `x11grab -i :99+0+30` silently means y=0 (use
  `:99+0,30`); F10 hides the official UI but does NOT disable it — clicks and
  typing still register mid-capture.
- fable has `/hideui` (aliases `/toggleui`, `/ui`, F11) from T301 Leg D. Use it —
  T300's gate could not rule out geometry hidden under the opaque chat panel.

---

## Leg A (slot3) — MAKE THE CAMERA TUNABLE AT RUNTIME. No server, no oracle.

**The blocker Will named: this must not require recompiling.** Every first-person
camera parameter is currently a `constexpr` in `include/client/graphics/first_person_eye.h`
and `first_person_camera.h`, so each candidate value costs a full rebuild — which
is why two units picked the forward offset by argument instead of by search.

Convert them to a runtime-settable tunable set (keep the named constants as the
DEFAULTS, byte-identical values) and expose:

    /fpcam                      -- list every parameter, its value, default, and whether overridden
    /fpcam set <name> <value>
    /fpcam reset [<name>]
    /fpcam dump                 -- the RESOLVED pose: eye world XYZ, pitch, heading, fov, near plane,
                                   the model's node origin z, and eye-above-ground

At minimum: `kEyeForwardOffsetUnits`, `kCrownAboveEyeUnits`,
`kEyeHeightFractionOfSkull`, `kFirstPersonMaxDownPitchDegrees`, the FOV and the
near plane.

**Reachable from BOTH the in-game chat input AND a `--script` run / console
input.** This is not optional plumbing — T308 just finished fixing a command path
that only worked on one side, and a sweep driver needs the scripted side. Also
accept the values from the CLI/config at startup so a sweep can launch a fresh
process per candidate without typing.

`/fpcam dump` is what Leg C reads back to turn a matched frame into numbers, so
its output must be parseable and must state the units and the frame each number
is in.

### The seam, already located — do not rediscover it

`/fpcam` belongs in **registry 3** (the renderer if/else chain), because the
camera lives on `IrrlichtRenderer` and must run on the render thread
(`application.cpp:1204-1216` warns never to call renderer methods from the game
thread). Two edits, and BOTH are mandatory:

1. A new `} else if (cmd == "/fpcam") { ... }` arm in
   `IrrlichtRenderer::processSlashCommand()` (`src/client/graphics/irrlicht_renderer.cpp:1032`),
   next to the `/camera` arm at `:1078-1112`. Reply through the local `chat`
   lambda (`:1055-1065`) — it pushes a `SystemMessage` bridge event AND logs to
   `MOD_MAIN`.
2. A `{"fpcam", {}, "..."}` row in `kCatalog`
   (`src/client/help/renderer_command_catalog.cpp:22+`). `FindRendererCommand()`
   (`:86`) is the gate at `application.cpp:1231`; **without the catalog row the
   command is reported `registry=none` and never queued.**

`tests/test_merged_help.cpp:346-382` enforces set-equality between the if-chain
literals and the catalog — adding one without the other fails CI.

**Both entry points already converge on this**, so reachability comes free once
the two edits are right: the chat window via `EverQuest::ProcessChatInput()`
(`eq_chat.cpp:344`) and its unrecognized-command fallback
(`application.cpp:1329`), and `--script` via `Application::updateScript()`
(`application.cpp:1090`, dispatch at `:1144`). Script steps are HELD until
`m_fullyConnected` (`:1114-1132`).

**⚠ Trap for Leg C's sweep driver:** a renderer command always reports
`CMD outcome=queued` (`printCommandResult()`, `application.cpp:1262`) because
registry 3 is fire-and-forget with no ack path. **A sweep must assert on the log
line the `chat` lambda emits, never on `outcome`.** Build `/fpcam dump` so that
line is the sweep's parseable result.

Headless test template: `tests/test_irrlicht_camera_command.cpp` constructs a bare
`IrrlichtRenderer`, calls `processSlashCommand` directly and drains the
`SystemMessage` events. Copy it.

**Do NOT change any default.** Leg C sets the values; this leg only makes them
settable. A test must pin that the defaults still equal today's constants, that
set/reset round-trips, and that the command is reachable from both paths.

## Leg B (slot1) — THE Z CONVENTION, ESTABLISHED FROM THE SERVER. No oracle.

Settle E2 from the authority rather than from fable's comments. EQEmu source is
at `/mnt/projects/akk-stack/code`. **Determine what z means in the position
packets for a player and for an NPC** — feet/ground, or centre — and cite the
file:line that decides it.

Then correct the code to match, including `collisionZOffset`, which is class-A
item 9 on the board: `entity_renderer.cpp:529,694,1099,1132` all use
`(bboxHeight / 2.0f) * scale` where the mesh's real origin-to-feet distance is
`|bbox.MinEdge.Y| * scale` — 3.35 vs 3.05 for a Human male, so **every entity in
the world stands 0.30 above its own collision ground and the first-person eye
sits ~0.30 too high.** That is a direct confound in Leg C's measurement, which is
why it is in this unit and not deferred.

**This moves every entity, so it will move the visual goldens.** Capture
`before/` and `after/` identically, report the golden diff, and **STOP for Will's
call rather than re-blessing** — the only pre-approved re-bless is T279's EQE1 mip
case. Report the measured eye-above-ground before and after against the official
client's 6.65-6.75.

## Leg C (slot2, AFTER A and B land) — THE MATCH. Owns eqemu AND the oracle.

**Solve the camera pose by making fable's frame match the official client's
frame.** Both clients at the SAME resolution (1024x768, T307's oracle geometry),
same race/gender, same zone, same absolute position and heading set through the
server, UI hidden on both.

**Configuration 1 — static geometry at close range (Will's primary).** Stand at a
wall or other static geometry, facing it, at a known position/heading. This is the
configuration that makes the forward offset observable: near geometry converts an
eye translation into a large, unambiguous change in the drawn frame. Sweep
`kEyeForwardOffsetUnits` (and the eye-height parameters) via Leg A's command until
fable's frame matches the official client's. **Report the residual as a pixel
difference curve over the swept parameter, not as a single "it matches"** — the
curve's minimum IS the measurement, and its width is the error bar.

**Configuration 2 — two characters, same race/gender, known separation (Will's
second).** Log in two characters and have them duel at a measured distance apart.
This adds a second, independent constraint: the observed size and screen position
of a KNOWN body at a KNOWN distance pins eye height and FOV in a way a wall
cannot, and the swing frames put the opponent's weapon in frame at known model
coordinates. See `two-client-live-testing` — single-client verification is
circular.

**Configuration 3 — the own-body swing frames.** T309's draw rule is live, so
fable now draws the player's own model during an attack exactly as the official
client does. `review/T307/T307_A_01` (max down-clamp) and
`review/T307/T307_A_06` (level pitch) are golden frames of the official client's
own body and halberd seen from inside. Match against them frame for frame. This is
the constraint that both disagreeing measurements lacked: the geometry in frame is
at known model coordinates.

**What must come out of this leg**, each with its error bar and the configuration
that produced it: the eye's forward offset, the eye height (and whether it is
`crown - 0.60` or `0.925 H` — re-derive, do not inherit), the FOV, and the near
plane. Where two configurations disagree, report both and say which geometry each
one was sensitive to. **Do not average a disagreement away.**

The open mismatch Leg C must close is T309 Leg 2's: at LEVEL pitch every swing
puts fable's own face across the whole viewport for 2-3 frames (3-4+ on a Troll),
where T307 filmed the official client showing the spearhead sweeping in low with
the body below the frustum.

## Efficiency — Will asked for it explicitly, so build it

A harness, not a hand-run loop: launch fable at a given parameter set, drive it to
the fixed pose through the server, capture, diff against the stored oracle frame,
emit the score; sweep. Keep it in `scripts/` with the oracle frames it compares
against named in a README. **The point is that the next camera question is
answered in an afternoon by running this, not in three units by argument.**

---

## Review — Leg A (slot3, 2026-08-29)

**Delivered: every first-person camera parameter is settable at runtime, and no
default changed.** A candidate value now costs a slash command or a process
launch, not a rebuild.

### What was built

- `include/client/graphics/first_person_tunables.h` — nine parameters in one
  atomic, process-global store with `fpTunable(FpTunable::X)` as the read. The
  DEFAULTS ARE THE CONSTANTS, read from them directly rather than retyped.
- `src/client/graphics/first_person_tunables.cpp` — name lookup, parsing,
  formatting, and `FormatFirstPersonPoseDump()`. A new TU on purpose: the
  renderer's own TU is already over the size gate, and none of this needs a
  camera.
- `/fpcam` in `IrrlichtRenderer::processSlashCommand()` (registry 3) + the
  mandatory `kCatalog` row. `/fpcam` | `list` | `set <name> <value>` |
  `reset [<name>]` | `dump` | `help [<name>]`. A `set`/`reset` calls
  `repositionFirstPersonCamera()` so it lands on the current frame.
- `--fpcam name=value[,name=value...]`, repeatable, applied by
  `Application::initialize()` before anything reads a camera number. A rejected
  assignment is logged at ERROR and that parameter keeps its default — a sweep
  must never silently render the default and produce a flat difference curve.

### The nine, and an honest split

Live (a production consumer reads them): `eyeForwardOffset`,
`eyeHeightFractionOfSkull`, `maxDownPitch`, `maxUpPitch`, `pitchDown`, `hfov`,
`nearPlane`.

**ADVISORY (no runtime consumer): `crownAboveEye` and
`eyeHeightFractionOfStandingHeight`.** The plan names `kCrownAboveEyeUnits` in
its minimum set, so it is exposed — but a grep of the tree shows both constants
are read only by `tests/test_first_person_eye_anatomy.cpp`;
`kEyeHeightFractionOfSkull` was SOLVED against `crownAboveEye` at authoring
time and nothing consults it at runtime. Rather than pretend, both are labelled
`status=advisory` in every listing and `set` on one prints
"ADVISORY: this parameter has no runtime consumer and moves no pixels".
A tunable that silently does nothing is exactly the trap this unit exists to
remove. **Leg C: the live lever for eye HEIGHT is `eyeHeightFractionOfSkull`
(plus the existing `playerConfig_.eyeHeight` nudge), not `crownAboveEye`.**

### Two findings worth carrying forward

1. **The eye point was cached at model load** (`animated_mesh_eye.cpp`, the
   `eyeHeadBoneIndex_ == -2` block), so a runtime `eyeHeightFractionOfSkull`
   would have been frozen at whatever value happened to be set when the model
   loaded — the tunable would have looked live and been dead after the first
   frame. Fixed by caching the head-vertex CLUSTER (a real property of the
   mesh) and deriving the eye point per call; the expensive scan still happens
   once.
2. **`nearPlane` is applied in EVERY camera mode**, not just first person
   (`applyCameraProjectionForMode()` deliberately sets one near value for both
   — T300 measured the official client clipping nothing). Overriding it
   therefore changes third person too, and so would move the visual goldens.
   Said so in the parameter's own help text.

### Frames, because the dump is Leg C's input format

`/fpcam dump` emits one `[FPCAM-DUMP]` line per group, every number carrying
its units and frame in the key. The eye is reported in BOTH frames
(`eyeIrrX/Y/Z` Irrlicht world, `eyeEqX/Y/Z` EQ world) so a reader never has to
remember which one a dump is in, plus `nodeOriginEqZ`, `collisionZOffset`,
`feetEqZ`, `eyeAboveNodeOrigin`, `eyeAboveGround`, the pitch chain, the FOV,
the near plane, the target and up vectors, and a `tunables ...` line carrying
the inputs that produced the pose — so one dump is a complete sweep sample.

`collisionZOffset`/`feetEqZ` are REPORTED ONLY; Leg B owns that number and is
expected to change it, which will change `eyeAboveGround` here.

**⚠ RESTATED FOR THE SWEEP DRIVER: assert on the emitted line, never on
`outcome`.** Registry 3 is fire-and-forget; `printCommandResult()` says
`outcome=queued` whatever happened. The `chat` lambda both pushes the
`SystemMessage` and `LOG_INFO(MOD_MAIN, ...)`, so `[FPCAM-DUMP]` is greppable
from a `--script` run's log.

### Verification (by EXIT STATUS)

| Step | Result |
|---|---|
| `cmake --build build -j24` | PASS (exit 0) |
| `ctest --test-dir build -j6` | PASS — 3316 tests, 1 failure: `AudioManagerTest.PreloadCommonSounds` (the documented ALSA host quirk) |
| `tests/test_first_person_tunables.cpp` (new, 29 tests) | PASS |
| `tests/test_merged_help.cpp` (catalog/if-chain drift) | PASS — 24/24, incl. `MatchesRendererIfChainExactly` |
| `tests/test_first_person_camera_single_formula.cpp` (single-formula drift) | PASS — 15/15 |
| `scripts/build-gles2-x86.sh` | PASS (exit 0) |
| `scripts/ci/visual_gate.sh` | **PASS**, goldens untouched (`git status` clean under goldens) |
| `scripts/build-arm-noble.sh` | PASS (exit 0) |
| `scripts/refactor/check_file_sizes.sh fable` | no NEW FAIL — `irrlicht_renderer.cpp` was already 3148 (>3000) on `fable` and is 3237; both WARNs pre-existing |

**Reachability, demonstrated on both paths concretely:**
- *Chat / renderer if-chain*: `FpCamRendererTest` drives the REAL
  `IrrlichtRenderer::processSlashCommand("/fpcam ...")` on a bare renderer and
  drains the `SystemMessage` replies — list, set, reset, dump.
- *`--script` / console*: `Application::DispatchSlashCommand("/fpcam dump")` —
  the exact function `updateScript()` (`application.cpp:1162`) and piped stdin
  (`:395`) call — returns `registry=renderer`, i.e. the catalog gate at `:1231`
  admitted it; `/fpcamnotacommand` returns `registry=none`. Confirmed live:
  piping `/fpcam dump` into a real headless run printed
  `CMD outcome=held registry=pending ... cmd="/fpcam dump"` (held only because
  there is no server in this leg — Leg C owns eqemu).
- *Startup*: a real run with
  `--fpcam eyeForwardOffset=0.9,hfov=70 --fpcam bogus=1` logged
  `[FPCAM] --fpcam applied: eyeForwardOffset=0.9,hfov=70` and
  `[FPCAM] --fpcam 'bogus=1' REJECTED: unknown parameter 'bogus' ...`.

### Deviations, flagged

1. **Four constants moved file** (values and comments byte-identical):
   `kFirstPersonPitchDownDegrees`, `kFirstPersonHorizontalFovDegrees`,
   `kFirstPersonMaxUpPitchDegrees`, `kWorldNearPlaneUnits` moved from
   `first_person_camera.h` to `first_person_tunables.h`, which that header now
   includes — so every existing `EQT::Graphics::k...` spelling still resolves
   and no caller changed. The move exists solely so the default table can name
   them without a circular include. Breadcrumb comments left behind.
2. **`maxUpPitch` above 89 does not take full effect.** The camera formula
   honours it, but the mouse-look accumulator in `renderer_player_physics.cpp`
   still clamps the STORED pitch to the constant — that file is Leg B's
   territory this unit, so it was left alone. Lowering `maxUpPitch` works in
   both directions; raising it past 89 is capped by the accumulator. Not a
   solve target, and noted in the parameter's help.
3. **`crownAboveEye` / `eyeHeightFractionOfStandingHeight` are advisory**, per
   the section above.
4. **No live oracle work, by design** — this leg took no server and produced no
   pixels; that is Leg C's.

# Leg B Review (slot1, `task/T310-legB`, 2026-08-29)

## Part 1 — what z means, from the EQEmu server source

**The citation that settles it: `zone/client_packet.cpp:4831`
(`Client::Handle_OP_ClientUpdate`) together with `zone/waypoints.cpp:831-834`
(`Mob::FixZ`).** The handler assigns the packet's `z_pos` into the client's
`m_Position` verbatim — no transform, no offset — and `Mob::FixZ()` opens with
`if (IsClient() && !fix_client_z) return;`, where **no caller anywhere in the
EQEmu tree ever passes `fix_client_z = true`** (grep: the only two hits are the
declaration `zone/mob.h:1264` and the definition). The server never writes a
player's z.

Outbound it is the same quantity for everyone and for every client patch:
`Mob::FillSpawnStruct` (`zone/mob.cpp:1292`) and `Mob::MakeSpawnUpdate`
(`zone/mob.cpp:1721`) both just `FloatToEQ19(m_Position.z)`, and no
`common/patches/*` encoder adjusts z (checked titanium.cpp: `OUT(z_pos)` /
`eq->z = emu->z`).

**So E2 is confirmed: both clients take z from the same server the same way,
and there is no cross-client frame disagreement to reconcile.**

**It is NOT the feet, and the server says so explicitly for NPCs.**
`Mob::GetFixedZ()` (`zone/waypoints.cpp:796-810`) raycasts to the collision
ground and then does `new_z += GetZOffset()`; `Mob::GetZOffset()`
(`zone/waypoints.cpp:881-1006`) returns `0.2 * size * race_constant`, = **3.75
for a default size-6 humanoid**. `Mob::Summon()` (`zone/mob.cpp:4866-4868`)
converts one entity's z into another's by subtracting the difference of their
two `GetZOffset()` values — and it does that when the target is a **Client** as
well as an NPC, so the server models players and NPCs identically.

**Honest limit on that authority.** 3.75 is a heuristic, not a definition, and
EQEmu is not self-consistent about it: `aggro.cpp:1283` puts a mob's eye at
`GetZ() + size/2 * 0.9` (which only makes sense if `GetZ()` is mid-body) while
`client.cpp:12171` puts it at `GetZ() + size * 0.9` (which only makes sense if
`GetZ()` is the feet). Since the server never sets a client's z, the server
source can prove that the two clients share one convention — it cannot prove
where the official client draws the model within it. That last step is Leg C's.

**What the code should say, and now does.** "Model CENTER" is wrong and has
been removed from the subsystem. The operative fact for rendering is narrower
and is a property of the shipped assets: fable puts the scene node's **ORIGIN**
at the server z (`modelYOffset` is 0 at every site), and the EQ meshes are
authored with their origin inside the body. The Z-convention note now lives once,
at the top of `include/client/graphics/entity_renderer.h`, with these citations.

## Part 2 — collisionZOffset (class-A item 9): FIXED

Five fill sites — `entity_renderer.cpp` at the placeholder cube, both
`createEntity` paths, and both deferred-build paths — replaced by one helper,
`collisionZOffsetFromBounds(bbox, scale)` = `-bbox.MinEdge.Y * scale`.

Measured on the shipped meshes (`tests/test_entity_collision_z_offset.cpp`,
7 tests): **Human male origin-to-feet 3.004 vs half-height 3.339 — a 0.335
lift, not the 0.30 the board estimated.** Per-race the lift ranges 0.021
(Ogre m) to **0.606 (Dwarf m)**. The placeholder cube was worse than any real
mesh: it is authored on its base (`createPlaceholderMesh()` spans Y = [0,size]),
so `cubeSize/2` floated every placeholder by a full **3.0**; it is now 0.

**Golden diff: ZERO, and that is a finding, not an absence of one.** The gate
was rerun on the rebuilt GLES2 tree and PASSED with all nine golden frames
byte-identical (`cmp` clean, `review/T310/legB/{before,after}/`). No gated
golden frame contains a character entity — every `[budget]` line of the three
frame-captured runs reads `character models 0%` — and `gles2_zone_benchmark`'s
`--entities` path places its own scene nodes without going through
`EntityRenderer`, so it never reads this value either. **Nothing outside a live
session exercises `collisionZOffset`.** No re-bless was needed and none was
taken. It also means **no before/after picture of the effect exists**; making
one needs a live entity on real ground, i.e. Leg C's server.

## The eye number, and it goes the WRONG way

Human male, eye 3.077 above the node origin, mesh spans Y = [-3.004, +3.674]:

| | eye above ground |
|---|---|
| before | 3.339 + 3.077 = **6.42** |
| after | 3.004 + 3.077 = **6.08** |
| official client (measured) | **6.65 – 6.75** |

**The correction moves fable's eye 0.34 FURTHER from the official client.**
Reported, not averaged away. The likely reason is in the same measurement:
**6.65–6.75 is within 0.01 of this mesh's own crown above its own feet,
3.674 + 3.004 = 6.678.** `kCrownAboveEyeUnits = 0.60` — the constant that
`first_person_eye.h:95-102` justifies by citing exactly the fable-centres /
official-feet-on-z premise this leg has now disproved — is the same size as the
whole remaining shortfall. **Leg C should re-derive it against the oracle
rather than inherit it**, as the plan already instructs. Leg B did not touch
`first_person_eye.h` (Leg A's territory).

## Two further defects found in the same belief, NOT fixed here

1. **An unmatched size/2 on the player's own z.** `eq_entities.cpp:416` and
   `:1184` both do `SetPosLocal(..., entity.z - entity.size / 2.0f)` on the
   strength of a comment reading "SendPositionUpdate will add
   `m_game_state.player().size()/2` to convert back to model center."
   **It does not.** `SendPositionUpdate()` (`eq_movement.cpp:1100`) says "Send
   Z position directly without offset" and sends `PosZ()` verbatim, and the
   server adds nothing either (Part 1). The round trip is asymmetric by ~3.0
   units for a default humanoid until the renderer's ground snap overwrites it
   on the first movement frame. `spell_cast_checks.cpp` then adds the size/2
   back for its LOS ray, which double-counts once the snap has run.
   **Left as-is: removing it changes what fable puts on the wire, and that
   needs live two-client verification.** All three comments are corrected in
   place to describe what the code actually does, with the asymmetry flagged
   loudly at each site.
2. **`eq_movement.cpp:1741-1742`** computes `headZ = PosZ() + 6.0` for water
   detection, i.e. treats `PosZ()` as the feet — a third frame in the same
   subsystem. Noted only.

Both are Leg C's to close, because both need the server.

## Verification (by exit status)

| step | result |
|---|---|
| `cmake --build wt/slot1/build -j24` | **PASS** (exit 0) |
| `ctest --test-dir wt/slot1/build -j6` | **PASS** — 3294/3295; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `scripts/build-gles2-x86.sh` | **PASS** (exit 0) |
| `scripts/ci/visual_gate.sh` | **PASS**, goldens untouched, nine frames byte-identical |
| `scripts/build-arm-noble.sh` | **PASS** (exit 0) |
| `scripts/refactor/check_file_sizes.sh fable` | exit 1 — **no NEW fail**: `entity_renderer.cpp` was already over the 3000-line limit at the base rev and is at 3255 lines on both, byte-for-byte the same count |

Review media: `/mnt/projects/fable/review/T310/legB/` (README, before/, after/,
gate log, both measurement tables).