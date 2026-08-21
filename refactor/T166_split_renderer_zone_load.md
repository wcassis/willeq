# T166 — Split `src/client/graphics/renderer_zone_load.cpp` off the ceiling

**Slot: slot1. x86 only. NO eqemu, NO lock. NO Pi — the Pi is offline as of
2026-08-21, and this unit does not need it: the ARM leg is a cross-compile on
the dev host.** Orchestrator executed directly, as with T164/T165.

**Territory: `src/client/graphics/renderer_zone_load*.cpp`,
`include/client/graphics/zone_load_context.h`, `CMakeLists.txt`,
`refactor/`. This is a MOVE-ONLY change — no behaviour, no interface, no
production logic touched.**

## Why

The file was **2996 lines, sitting on `check_file_sizes.sh`'s 3000-line FAIL
ceiling** — the last companion to T165's test-file problem, and recorded in
`refactor/deferred.md` since Batch F. Unlike the test file it is production
code, and the ceiling had already started deforming it: **T115's crash fix had
to be compressed onto a shared line with another statement to fit**, which is
the ceiling causing exactly the harm it exists to prevent.

## What was done

The file was already well decomposed — `loadZoneSequential()` plus **20
numbered step methods** (G01's decomposition). The split follows that seam
exactly; no method was reorganised, renamed, or re-ordered.

| file | lines | contents |
|---|---|---|
| `renderer_zone_load.cpp` | 919 | the `loadZoneSequential()` driver + steps 0 (EQE1), 1 (S3D), 2 (BSP), 4 (Regions), 5 (Assets) |
| `renderer_zone_load_objects.cpp` | 578 | steps 6 (Objects), 7 (Doors), 9 (Collision) |
| `renderer_zone_load_entities.cpp` | 511 | step 8 + sub-steps 8a–8f |
| `renderer_zone_load_env.cpp` | 1139 | steps 10 (Sky), 11 (Env), 12 (Lights), 12b (Simulation), 13 (Cleanup) |
| `include/client/graphics/zone_load_context.h` | 79 | `IrrlichtRenderer::ZoneLoadContext` |

**Step 9 (Collision) is grouped with 6 and 7, not with the environment
steps.** It consumes exactly what those two produce — region, object and door
geometry — into the collision sets, so the group is the data dependency, not
the step number.

**The one structural change: `ZoneLoadContext` became a header.** It was
defined file-locally in the .cpp precisely because it was an implementation
detail of one TU; four TUs need the complete type, so it moves to
`include/client/graphics/zone_load_context.h`. `irrlicht_renderer.h:1316`
still only forward-declares it, so nothing outside the zone-load TUs can see
it — the encapsulation the original comment was protecting is intact. This is
the "a file-local helper must become accessible to the moved code" case Batch
F's process explicitly allows, reported here as it requires.

Each new TU carries the original's full include block. Trimming per-file
include sets would be a second, riskier change wearing the same commit; the
compile-time cost of three more TUs over this header set is not worth trading
for that risk in a move-only unit.

## Verification

1. **`verify_move_only.py fable..HEAD` clean** — the project's own tool for
   exactly this, reported below.
2. **Content preservation also proven independently**: the four files' bodies
   re-concatenated are **byte-identical** to the original's step-method
   ranges. All **21** `IrrlichtRenderer::` definitions present, exactly once
   each, same set before and after.
3. Full build clean **first try**.
4. `ctest` **2595/2596** — sole failure the documented ALSA
   `AudioManagerTest.PreloadCommonSounds`.
5. `scripts/build-gles2-x86.sh` clean.
6. **Visual gate PASS, goldens untouched** (`git status` clean of
   `refactor/artifacts/`). This is the real behavioural evidence, not the
   compile: the gate loads and renders **qeynos2, gfaydark and befallen**,
   which drives the whole `loadZoneSequential()` sequence — every step this
   unit moved — and compares the rendered frames against committed goldens.
   A split that broke a step would not produce matching frames.
7. `scripts/build-arm-noble.sh` clean; fresh ARM `willeq` produced. **The Pi
   being offline does not weaken this** — the ARM leg of the bar has always
   been the cross-compile, and no unit runs the Pi without an explicit Pi
   assignment.
8. `check_file_sizes.sh`: 5 files, **5 ok, 0 warn, 0 fail** — the whole point
   being that nothing is now even at the 1500-line WARN line.

## Follow-ups this did NOT do

- `refactor/deferred.md`'s over-1500 list still names **eq_entities.cpp
  (2,438), eq_player_info.cpp (2,006), eq_movement.cpp (~1,975),
  eq_bridge.cpp (1,837), renderer_frame.cpp (1,583),
  renderer_player_physics.cpp (1,570), eq_merchant.cpp (1,558)** plus the
  accepted cores eq.cpp (~3,600) and entity_renderer.cpp (3,201). None is at
  the hard ceiling; none is urgent. The list is now accurate again.
- `renderer_zone_load_env.cpp` at 1139 is the largest survivor and the one to
  watch. Steps 11 (Env) and 12b (Simulation) are each ~270–430 lines and
  would separate cleanly if it ever grows.
