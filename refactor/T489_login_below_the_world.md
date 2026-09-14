# T489 — a character saved below the zone floor logs in "outside of the world" and keeps sinking (Will, 2026-09-14)

Class A (Will's own first login as Testcaster in Qeynos was "outside of the
world"; he `/warp`ed out). **lock, x86 first; Pi only for the fall-through leg.**
Reproduced by the 2026-09-14 audit: `review/AUDIT-0914/x86/raw/R6_x86_testchar_qeynos2_login`
— `Testchar` is saved at (x -0.83, y 102.72, z **-495.40**) in qeynos2
(zone safe z = 3). The tip client logs in there, draws sky above a flat grey
void (`frames/frame_000020.png`), `/loc` reports `102.6, -0.8, -495.4`, and
over 335 frames the player z drifts -495.40 → -496.01: gravity runs with no
ground under it, and each position update re-saves the deeper z.

Two questions, two legs:

## Leg A — how does a saved z get 500 units under the floor? (the cause)

Nothing in the DB moved Testchar there by hand. Candidates, confirm or refute
each with a red test or a live run:
1. **Gravity before collision.** `PlayerCameraComponent::updatePlayerMovement`
   (`src/client/graphics/player/renderer_player_physics.cpp:277-296`) integrates
   `verticalVelocity -= gravity * dt` whenever the player is not grounded. If
   the physics loop runs while the zone's collision mesh / BSP is not yet
   loaded (the "Finalizing connection 40%" stall, B751, or any zone whose
   graphics load lags the world entry), the player falls freely and
   `SendPositionUpdate()` reports it. Check the ordering: when does physics
   first run vs when `setBspCollision` / the ground mesh is wired
   (`renderer_zone_load_env.cpp:1118`), and does anything hold the player at
   the server's spawn z until then?
2. **A big-dt step tunnelling through the floor.** `renderer_frame.cpp:83` only
   WARNS on a frame over 100 ms; the integrator takes the whole dt. A 1.4–1.9 s
   frame (seen on the Pi during entity mesh builds, `PERF: createAnimatedNode
   ... took 1903 ms`, and during frame dumps) moves the player 60–90 units in
   one step. Measure: does the ground snap catch a step that starts above and
   ends below a dune face? (`review/AUDIT-0914/pi/raw/R4L2_tip_1280_long`
   frame 481 is such a step: 62 units in one frame; it happened to stay on
   the surface).
3. **Server-side**: eqemu's own `zone.safe_z` / `Client::CheckZone` — rule
   out that the server, not the client, wrote -495 (query the eqemu logs for
   Testchar's saves).
Pin the cause with a test that fails first (a headless physics case: no
collision mesh yet → z must not change; dt 2.0 s → z must not pass through
the floor).

## Leg B — what happens on login below the floor (the symptom)

What the OFFICIAL client does when a character is under the world is the
oracle: measure it (log Testchar in with the Wine/Xvfb official client,
`eq-oracle-provisioning`; it is on account willeq_test — coordinate the
lock). Then match it. Whatever it is, the client must not keep re-saving a
deeper z forever; and the CLIENT must not be the thing that put the
character there (Leg A).

## Proof

Standard verification bar; the physics goldens (`renderer_player_physics`
G-batch) updated in place and said so; ARM; file sizes. Live: Testchar's
login on x86 after the fix (frames + `/loc`), and the Leg A mechanism red →
green. Restore Testchar to qeynos2's safe point (`-74, 428, 3`) ONLY after
the login-below-the-floor capture is taken — it is the evidence. `review/T489/`
with a README.

File territory: `renderer_player_physics.cpp`, the zone-load collision wiring,
`movement_update.cpp` (position sends), physics tests. **Not** the camera rig
(camera_controller/camera_rig), not the UI, not the loaders.

STOP: the cause is server-side only; the official client also sinks forever
(then the fix is Leg A alone — report).
