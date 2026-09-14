# T470 — DoorManager mesh leaks (B621); the uncalled synchronous zone loader (B622)

Class A/B, **no lock, no Pi**, slot3. Both found by T468 (read its Review in
`refactor/T468_door_texture_guard_unregistered.md` and its tests). The Orange
Pi has 483 MB; a per-zone mesh leak accumulates across zone changes in a
play session, so B621 is the priority half.

## B621 — `DoorManager` leaks meshes on two paths (read from code only)

Confirm each path with a pre-fix failing test on T468's seam (bare
`IrrlichtRenderer` + EDT_NULL device + real cache): count mesh references /
`IReferenceCounted` grabs and drops, or the driver's/scene manager's mesh
cache size, across build → rebuild → `clearDoors()` → next zone. Fix with the
ownership rule the rest of `DoorManager` uses (who grabs, who drops — cite
it). If a path is refuted, say so with the evidence. Check the same pattern in
`rebuildSingleDoor()`, placeholder door rebuilds and `doorMeshCache_`
replacement.

## B622 — `Application::loadZoneGraphics()` has no caller

The synchronous twin of `loadZoneGraphicsOnThread()`. Prove zero callers per
the protocol rule (`src/ include/ tests/ tools/ benchmarks/ docker/`), then
delete it and anything that only it reaches (census those, cascade until a
fixed point, the T458 way). Keep T464's `placeCameraForZoneIn()` call in the
live loader. Update `ZeroCallers` rows, `docs/module_map.md`, and the
`application` golden if it pins the deleted body (G-correction 32, in place,
say so).

## Proof

Standard verification bar, builds strictly in sequence; G-batch goldens for
every TU touched (`application` is pinned); visual gate (no golden moves);
ARM; file sizes. Board B640–B649; strike/narrow B621/B622.

File territory: `src/client/graphics/door_manager*`,
`src/client/application_zone_loading.cpp` (the dead loader only),
`scripts/refactor/check_zero_callers.py` rows for those, door tests,
`docs/module_map.md`. **Not** `src/client/spell/` or the zone dispatch arms for
cast interrupts (T469, slot2), **not** camera rig / hotkeys (T466, slot1).

STOP: a leak fix needs a zone-lifecycle order change (report); an image golden
moves.

## Review

(executor fills)
