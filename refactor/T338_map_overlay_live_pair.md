# T338 — The owed Ctrl+M map-overlay live pair (B054 close-out proof)

Measurement only; slot1; eqemu lock. T336's driver (`review/T336/t336_live.sh`)
with the key changed from Ctrl+N to Ctrl+M, at the current tip (must
contain T335 + T335b — check `git log`). OFF and ON captures, same
character/spot/daylight, `--eqe1-path`; the ON frame must show the map
overlay lines AND the 2D UI. Optionally the same for Ctrl+V (frustum) if
the binding exists (`hotkey_manager.cpp`). `review/T338/README.md` + PNGs;
strike the "owed" note on B054; restore the character as found; `down.sh`;
delete the lock. STOP: lock held; the toggle does not toggle (report the
log line, do not touch code).

## Review

Measurement only; no source file changed. Session 2026-09-04, slot1, branch
`task/T338-map-overlay-pair`, base/tip `03239def`, which carries `860d66a1`
(T335) and `e5a70f03` (T335b). The client binary was rebuilt from this slot
before the session (`scripts/build-gles2-x86.sh`, exit 0, ELF verified, 81.9 MB
at 14:03) so it carries T335's world-line guard.

**Media: `/mnt/projects/fable/review/T338/` (README.md names what to look at).**

### Result

| toggle | log evidence | ON frame |
|---|---|---|
| Ctrl+M, map overlay | `Map overlay: ON` then `Map overlay: Updated with 151 triangles` | magenta/pink wireframe lines drawn across wall, ground and a tree; bars, `FPS: 18`, chat panel, hotbar 1-0 and the A1-A8 bar all present |
| Ctrl+V, frustum debug | `Frustum culling: ON + DEBUG DRAW (Ctrl+V to cycle)` | green/blue/yellow region bounding boxes drawn out across the scene; bars, `FPS: 13`, chat panel, hotbar and A1-A8 bar all present |

Baseline `T338_off_overlay_1024x768.png` was captured with the identical
config, driver, spot and forced 11:00 daylight; the only difference between it
and each ON shot is one keypress. Judgement of the visuals is Will's — the
factual claim is only that the lines and the listed UI elements are present.

The Ctrl+V binding does exist (`hotkey_manager.cpp:501`,
`ToggleFrustumCulling`, Ctrl + KEY_V) and cycles ON -> ON+DEBUG -> OFF
(`renderer_frame_input.cpp:245`), so a single Ctrl+V from the default enabled
state reaches DEBUG DRAW. The pair was captured.

Neither STOP condition fired: the lock was taken atomically and held for the
whole session, and both toggles toggled.

### One thing found on the way: T336 ran with no collision map at all

The first Ctrl+M attempt toggled cleanly and drew nothing, with no
`Updated with N triangles` line. Cause:

    [ERROR] [MAP] Unable to open map file: /mnt/projects/maps/qeynos2.map

`HCMap::LoadMapFile()` composes `{maps_path}/{zone}.map`
(`src/client/hc_map.cpp:845`) and the `.map` files live under
`/mnt/projects/maps/base/`. T336's `willeq_t336.json` set `maps_path` to
`/mnt/projects/maps`, so **every T336 session ran with
`ctx_.collisionMap == nullptr`** — invisible there because the navmesh overlay
reads `navmesh_path`, which was correct. With `maps_path` corrected to
`/mnt/projects/maps/base` the map loads (32276 vertices, 24285 faces) and the
overlay draws.

Not boarded: it is a harness config path, not a product defect, and the client
does report it. Recorded here and in the review README because a null
`collisionMap` silently disables both the map overlay and the HCMap collision
path behind a single ERROR line logged ~30 s before the symptom.

### Server state

`Testchar` was found at `zone_id=2, x=-74, y=428, z=3.751, h=160` — exactly
where T336 left it — and restored to exactly that after four graphical
login/camp cycles had moved it to `y=428.04, z=3.75`. `Testcaster` and
`Testmelee` were never written. eqemu `down.sh` exit 0; lock deleted.

Incidental, corroborating T332 and T336 a third time: four more login/camp
cycles left `z=3.75`. The `+0.625`-per-login compounding T329 recorded in this
same rig did not recur.
