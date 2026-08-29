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
