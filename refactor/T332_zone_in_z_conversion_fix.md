# T332 — Fix the login z-compounding on the graphical path (B049, writer named by T331)

Class A. Slot1. `bridge_component.cpp` only touches the bridge family —
disjoint from G05 (`graphics/frame/`).

## The defect (T331, `review/T331/README.md`)

`BridgeComponent::OnGraphicsMovement` (`src/client/bridge/bridge_component.cpp`,
~line 717) clears `player.zAwaitingFrameConversion` on the premise that a
z coming from the renderer is already in the render frame. At zone-in
that is false: the renderer is SEEDED with the raw wire z
(`setPlayerPosition` from the zone-load snapshot, and again after the
model loads) and echoes it back through `PlayerPositionChanged`. The
correct deferred conversion (`bridge_component_intents.cpp:132-146`,
`wire → render: z − 0.625·size`) never runs, so the next
`SendPositionUpdate` re-adds `+0.625·size` to a z that was never
converted. Headless there is no renderer echo, so the flag survives and
the conversion runs — which is why T311's three-login test is green and
wrong.

## The fix (smallest correct change; measure the alternatives in the Review)

Preferred: `OnGraphicsMovement` must NOT clear the flag when the
renderer's z is the seeded wire z. Discriminate by provenance, not by
value: the seed path (`setPlayerPosition` from the snapshot) marks the
renderer-side position as "seeded from wire" (a bool alongside the
position in the intent, or a generation counter the snapshot bumps), and
`OnGraphicsMovement` runs the conversion (the same code the intents path
has at :132-146 — factor it into one function called from both) when the
flag is set AND the echo is the seed. After the first genuine renderer
movement (physics ran), the flag is cleared as today. Alternative, if
provenance cannot be carried: run the conversion in `OnGraphicsMovement`
whenever the flag is set (never just clear it) — say why the preferred
form was or was not possible.

## The regression test — MUST have a renderer present

`tests/test_zone_in_z_conversion.cpp`: construct the graphical harness
the existing renderer tests use (`test_position_frame_roundtrip.cpp` is
the closest — read it), seed a zone-in position with a known wire z for a
size-6 race, let the renderer echo it (drive one `PlayerPositionChanged`
through the bridge with the seeded value), then read the z the next
`SendPositionUpdate` would pack (the T234/T237 transform seam) and assert
it equals the wire z exactly (no +0.625). Second case: after a genuine
movement (a different z from the renderer), the flag is clear and the
packed z is `renderZ + 0.625·size`. Third: three consecutive zone-ins in
one process do not accumulate. **Prove the test catches the bug:** run it
against the pre-fix code first (`git stash` is forbidden — export a
patch, apply -R, run, re-apply) and paste the failing output.

Also: T311's headless three-login test stays; annotate it as the
headless arm.

## Live proof (owed unless the lock is free — it is, T331 released it)

Three graphical login/camp cycles of `Testchar` exactly as T331 ran them
(reuse `review/T331/` scripts); `character_data.z` must read 3.751 after
each. Table to `review/T332/README.md`. Restore the character as found;
`down.sh`; delete the lock.

## Territory

`src/client/bridge/bridge_component.cpp`, `src/client/bridge/bridge_component_intents.cpp`
(the factored conversion), `include/client/bridge/bridge_component.h`,
`include/client/events/renderer_intents.h` (only if provenance is carried
in the intent — one field), the renderer's seed call site
(`src/client/graphics/zone_load/…` or `application_zone_loading.cpp` —
one line to mark the seed; if it is under `src/client/graphics`, the
visual gate + ARM run), the new test + cmake, `refactor/BOARD.md` (strike
B049 with the live table), `review/T332/`.

STOP: the fix needs a thread-ownership change; the pre-fix test does not
fail (then the test is wrong, not the fix).

---

## Review (2026-09-04, slot1, branch `task/T332-zone-in-z-fix`, base `bf0e26bc`)

**B049 is closed, measured live.** Three graphical login/camp cycles of
`Testchar` at the canonical qeynos2 spot now re-persist the same z; T331
measured +0.625 every login on the same character at the same spot.

| cycle | z before | z after | delta | T331's same cycle |
|---|---|---|---|---|
| 1 | 3.751 | **3.75** | −0.001 | 3.75 -> **4.38063** |
| 2 | 3.75 | **3.75** | **0** | 4.38063 -> **5.00563** |
| 3 | 3.75 | **3.75** | **0** | 5.00563 -> **5.63063** |

The −0.001 on cycle 1 is the spawn packet's 1/8-unit fixed-point encoding —
T331's own no-drift `--no-graphics` arm shows exactly the same first-cycle step
and then zero. The official client's residual +0.001 is the collision epsilon
`include/client/position_frames.h` says is deliberately not copied; the oracle's
own `/loc` reads 3.75. **Deviation flagged**: the plan asked for 3.751 after each
cycle. 3.75 is the fixed point; 3.751 is not reachable without copying that
epsilon, and copying it would make the value creep, not settle.

### The plan's premise was half right, and the live run said so

The plan (following T331) named `OnGraphicsMovement` clearing
`zAwaitingFrameConversion` as the defect. That hole is real and is fixed here.
**It was not what fired.** The first live cycle with only that fix in place
still persisted **4.38063** — identical to T331. Rather than reason further, the
run was instrumented at every writer of the flag (`review/T332/logs/fable_gfx_cyclediag.log`):

```
11:10:06.743 [ZFRAME] self ClientUpdate SetPosLocal z=3.7500 awaiting=false offset=0.0000
```

`awaiting=false` on the FIRST self position update: the flag was never set in a
live graphical session at all. Neither self-SPAWN branch (the two T311 marked)
ever ran — no `ZoneSpawns [SELF]`, no `Found our own spawn in NewSpawn`; on a
real zone-in the spawn id arrives with the PlayerProfile instead.

**The actual writer is `ZoneProcessPlayerProfile` (`src/client/zone/zone_entry.cpp`)**,
which stored the server's WIRE z raw (`SetPosition(server_y, server_x, z)`) and
never said the conversion was owed. So nothing ever converted, and
`SendPositionUpdate()` added `0.625*size` to a value that already contained it —
persisted by the server, handed back at the next login, compounding.

### The fix as shipped, and why that form

Four changes, all of them the same rule — **a wire z must be converted or
marked, at every edge, and the conversion lives in one function**:

1. `src/client/zone/zone_entry.cpp` — PlayerProfile converts with
   `wireZToRenderZ()` and marks the conversion owed when no mesh offset is
   measurable yet. Two lines, identical in shape to the spawn edges. *This is
   the one that closes the live defect.*
2. `src/client/bridge/bridge_component.cpp` — `OnGraphicsMovement` discriminates
   by **provenance**, the plan's preferred form. `PlayerCameraComponent::setPlayerPosition`
   (the only external writer of the renderer's player position) marks what it
   wrote; the movement integrator clears the mark when it writes those members
   itself; the bit rides on `PlayerPositionChanged::seededPosition`. An echo is
   not stored at all — it carries no information the game thread does not
   already hold — and is taken as the cue to run the deferred conversion. No
   thread-ownership change: the flag is written only on the render thread, read
   only into an intent, and consumed on the game thread.
3. `src/client/bridge/bridge_component_intents.cpp` — the conversion is now ONE
   function, `BridgeComponent::ConvertSelfZToRenderFrame`, called by both
   discoverers (model-load and echo). It also publishes the corrected position
   onto the **bridge**: `SetPosition` alone fires `PlayerMoved` on the state
   layer's own EventBus, which nothing forwards to the renderer (`zone_entry.cpp`
   and `zone_component.cpp` both say so), so the renderer kept the seeded wire z
   and drew the player `0.625*size − collisionZOffset` off the ground. T311's
   "the renderer needs the corrected one" comment was true in intent and inert
   in fact.
4. `src/client/entities/entities_component_updates.cpp` — the server's echo of
   our own position (`HandleClientUpdate`, self branch) was a fifth unmarked
   wire-z store; it now takes the same conversion as every other inbound edge.

The provenance bit lives on `PlayerCameraComponent`, not on `RendererContext`,
so `irrlicht_renderer.h` (no-grow allowlist) does not grow.

### The regression test, and its pre-fix failure

`tests/test_zone_in_z_conversion.cpp`, 7 tests, registered in
`tests/cmake/zone_integration.cmake` behind `EQT_GRAPHICS AND IRRLICHT_LIBRARY`.
Real (bare, never-`Init()`'d) `IrrlichtRenderer`, seeded the way the zone-load
snapshot seeds it, with the provenance bit read out of the renderer's own state.
`test_position_frame_roundtrip.cpp`'s Part 3 is annotated as the headless arm.

Pre-fix run, patch exported and applied `-R` (never `git stash`):

**(a) reverting only the `OnGraphicsMovement` hunk — 4 of 5 then-existing tests fail:**

```
[ RUN      ] ZoneInZ.RendererEchoOfTheSeedDoesNotCountAsRenderFrame
tests/test_zone_in_z_conversion.cpp:129: Failure
The difference between transmitted and kOfficialWireZ is 0.74599981307983398, which exceeds 1e-3f, where
transmitted evaluates to 4.4969997406005859,
kOfficialWireZ evaluates to 3.750999927520752
B049: a standing zone-in must transmit back exactly what the server handed us. ...
[  FAILED  ] ZoneInZ.RendererEchoOfTheSeedDoesNotCountAsRenderFrame
[ RUN      ] ZoneInZ.ThreeConsecutiveZoneInsDoNotAccumulate
... carried evaluates to 5.2430000305175781 (login 1), 5.9890003204345703 (login 2)
[  FAILED  ] ZoneInZ.ThreeConsecutiveZoneInsDoNotAccumulate
[  FAILED  ] ZoneInZ.GenuineRendererMovementIsStillTheRenderFrame
[  FAILED  ] ZoneInZ.EchoBeforeTheModelLoadsLeavesTheConversionOwed
 4 FAILED TESTS
```

**(b) reverting only `zone_entry.cpp` — the two tests that replay the real
`OP_PlayerProfile` packet fail, and nothing else does:**

```
[ RUN      ] ZoneInZ.PlayerProfileZIsAWireValueAndIsConvertedWhenTheModelLoads
tests/test_zone_in_z_conversion.cpp:299: Failure
... z_pos evaluates to 4.4969997406005859, kOfficialWireZ evaluates to 3.750999927520752
live, this read 4.38063 against the official client's steady 3.751
[  FAILED  ] ZoneInZ.PlayerProfileZIsAWireValueAndIsConvertedWhenTheModelLoads
[ RUN      ] ZoneInZ.TheDeferredConversionReachesTheRenderer
Value of: sawCorrectedZ  Actual: false  Expected: true
[  FAILED  ] ZoneInZ.TheDeferredConversionReachesTheRenderer
 2 FAILED TESTS
```

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** |
| `ctest --test-dir build -j6 --output-on-failure` | **3758 / 3759**, sole failure `AudioManagerTest.PreloadCommonSounds` (the known ALSA quirk). `test_position_frame_roundtrip` 13/13, `test_eq_action_handler_transforms_real` 28/28 (T234/T237), `test_bridge_intent_table` 6/6, `test_reachability_sweep` 5/5, `test_player_camera_component` 9/9, new `test_zone_in_z_conversion` 7/7 |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate | **PASS**, goldens untouched (`git status` clean of `tests/goldens`) |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `check_file_sizes.sh --all` | 865 ok, **0 warn, 0 fail** |
| live, 3 graphical cycles | table above; character restored as found; `down.sh` exit 0; lock deleted |

One build was killed at the 600 s ceiling (exit 143). Per protocol every
`build/bin` artifact was scanned for ELF magic: one truncated binary
(`test_buff_click_off`, 14 MB of garbage that made ctest die at discovery, not a
test failure) plus 22 zero-byte files were deleted and rebuilt; `ar t` on both
archives (`libwilleq_core.a` 321 members, `libEtcLib.a` 15) confirmed neither
was gutted. Re-scanned clean after the final full build.

### Deviations

- **The plan's named writer was incomplete** (see above). The preferred
  provenance fix was implemented exactly as specified, and kept, but on its own
  it did not close the live defect; `zone_entry.cpp` did. Both are pinned.
- **3.75, not 3.751**, after each cycle — the 1/8 quantisation, explained above.
- Two extra instrumented live cycles (`cyclediag`, `cyclediag2`) were run to name
  the real writer instead of guessing. The `[ZFRAME]` logging is not committed.

Media and logs: `/mnt/projects/fable/review/T332/`.
