# T367 — fable's night against the oracle (B080)

Class A; slot1; eqemu lock (atomic; must be ABSENT — if present read it and
STOP); official client under Wine (T361's rig in `review/T361/scripts/`,
character Oraclealpha). Read the B080 board text and `review/T361/README.md`
first: at the same spot, zone, forced 22:00 and the same 300x300 near-wall
crop, the oracle reads 0.0643 linear grey and fable 0.0018 (35x); whole
frame 0.0332 vs 0.0019 (17x). T361's lead: fable's 218 zone point lights are
"data-only" — they scale colours but light nothing — while the oracle's
lamps plainly light the near wall.

## Leg A — decompose the gap (measurement)

Do not tune anything until the gap is decomposed. At the T361 spot, night:
1. Oracle: the near-wall crop with the camera turned so NO zone lamp is in
   the frame or near (find such a spot; record heading), and with a lamp
   near. The difference isolates lamp light from ambient/sky.
2. Oracle at noon, same crops: the day/night ratio of the oracle
   (T358 measured fable's as 4x).
3. Fable: the same four frames. Also dump what fable actually feeds the
   pipeline at night: the global ambient values `updateTimeOfDay()` sets
   (smgr ambient + `ZoneShader::setAmbientColor`), the vertex-colour /
   lightmap path if any, and what the 218 lights do (read the zone shader:
   are they uploaded as point lights, or only used to tint?).
4. Table: client × (day/night) × (lamp/no-lamp) → grey, plus fable's
   ambient numbers. State which term carries the 35x: ambient floor, lamp
   contribution, vertex colours, gamma/colour space of the output (check
   the oracle's and fable's framebuffer format — an sRGB-vs-linear mismatch
   is a ~2.2 exponent and would look exactly like "35x at the dark end").

## Leg B — match the dominant term

Fix the term the table names, by A/B search against the oracle's night
no-lamp crop (ambient) and lamp crop (lights), until both are within the
still noise floor (T358: 1.3%; T361's edges were one frame wide). If it is
the point lights, make them light geometry (the renderer's light budget is
`adaptiveLightingZoneBudget` = 5 on the Pi — respect the budget; lights are
already selected per frame by `computeOnce()` — check what consumes
`selectedLights`). If it is gamma, fix the output stage, then re-run
T361's residuals (they must tighten — record the new table). Tests: a unit
test on whatever mapping changed (pre-fix failing), the live A/B frames in
`review/T367/`. Strike B080; update T361's residual note on B067.

## Proof

Build, `ctest -j6`, visual gate standalone (pixel goldens WILL move if the
night brightness changes — the gate's zones are captured at their own time
of day; if a golden moves, STOP for Will with the contact sheet, never
self-bless), ARM build, file sizes. Restore server values; `down.sh`;
delete the lock.

STOP: a pixel golden moves (report + contact sheet, do not bless); the
dominant term is not reachable without a renderer restructure (report the
table and the shape).

---

## Review

Branch `task/T367-night-brightness-oracle`, base `fbec1140`. Media, scripts and
every log quoted below: **`/mnt/projects/fable/review/T367/`** (start with its
`README.md`).

### The headline

**B080's number does not reproduce; B080's lead does.**

B080 said fable's night is ~35x darker than the official client's on near
geometry. That figure came from cropping two frames that do not show the same
thing: `#goto` leaves the official client at heading 0.00 (its own `#loc`
prints it) while fable keeps its camera yaw, so at one coordinate the two
clients render different geometry — visibly so at the lamp spot, where fable's
frame is a third filled by a brick wall the oracle's frame does not contain.
Measured at a lamp-free spot at forced 22:00, fable reads **0.005502** linear
grey and the oracle **0.004245** — fable is **1.30x brighter**, not 35x darker.

B080's lead — "the 218 zone point lights light nothing" — is confirmed with a
mechanism, and fixed.

### Leg A — the decomposition table

Crop 700x420+40+40, linear grey (`convert -colorspace RGB`), the T361 method.
Oracle rows are flat windows of a 1721-frame 5 fps video whose edges land on
the client's own chat lines to within one frame; fable rows are flat windows of
its own 804/805 dumped frames. Every fable window is flat to <0.2%, every
oracle night window to <1.1%.

| client | spot | hour | linear grey |
|---|---|---|---|
| oracle | no-lamp (-74, 428.04, 3.75) | 11:00 | 0.170679 |
| oracle | no-lamp | 22:00 | 0.004245 |
| oracle | lamp (33, 375, 8) | 11:00 | 0.131526 |
| oracle | lamp | 22:00 | 0.007703 |
| fable pre-fix | no-lamp | 11:00 | 0.127566 |
| fable pre-fix | no-lamp | 22:00 | 0.005131 |
| fable pre-fix | lamp | 11:00 | 0.173945 |
| fable pre-fix | lamp | 22:00 | 0.003449 |
| fable post-fix | no-lamp | 11:00 | 0.127372 |
| fable post-fix | no-lamp | 22:00 | 0.005502 |
| fable post-fix | lamp | 11:00 | 0.174683 |
| fable post-fix | lamp | 22:00 | 0.003678 |

The "no-lamp" spot is T358/T361's; fable's own zone-light table (dumped from
the production `SEQ Step12` trace) puts the nearest of qeynos2's 218 lights
120.8 units away with radius 100. The "lamp" spot is 6 units from zone light 63
(radius 40, colour 0.51/0.51/0.12).

**Cross-client crops are confounded by composition, so the table is read
through the per-client, composition-invariant day/night ratio at a fixed
camera:**

| spot | oracle | fable pre | fable post |
|---|---|---|---|
| no-lamp | 40.2x | 24.9x | 23.2x |
| lamp | 17.1x | 50.4x | 47.5x |

**Which term carries the gap.** Not the ambient floor (fable's lamp-free night
is 1.30x the oracle's, i.e. already above it). Not gamma or colour space
(neither client uses an sRGB framebuffer — fable's EGL config is plain
`EGL_RED_SIZE 8` with no `EGL_GL_COLORSPACE` and there is no
`GL_FRAMEBUFFER_SRGB` in the tree; and at noon the two clients agree to 1.34x,
where a 2.2 exponent would also have shown). It is the **lamp term**: next to a
lamp the official client's night sits 17.1x below its own day and fable's sits
47.5x below — **2.8x of missing night light**.

fable's own numbers, for the record: `updateTimeOfDay()` sets ambient
0.5/0.5/0.5 for hours 7–17 and 0.08/0.08/0.15 for 20–4, pushed to both
`smgr_->setAmbientLight()` and `ZoneShader::setAmbientColor()`; zone vertex
colour is hardcoded `SColor(255,255,255,255)`; zone point lights carry the WLD
radius with `attConstant=1, attLinear=1/r, attQuadratic=1/r²`, are selected
top-8 per frame and evaluated **per vertex** (only the player light is per
pixel); framebuffer 8-bit RGB, no sRGB conversion at any stage.

### Leg B — what changed

`src/client/graphics/sim_visibility.cpp`,
`SimulationWorker::computeLightVisibility` and `computeObjectLightVisibility`,
plus one private helper declared in `include/client/graphics/simulation_worker.h`.

The live census before the fix, on every frame of a five-minute session:

    ZoneLightVis: 0/218 visible (dist=194, pvs=20, frustum=4)   <- no-lamp spot
    ZoneLightVis: 0/218 visible (dist=164, pvs=43, frustum=6)   <- 6 units from a lamp
    ObjLightVis:  0/107 visible (dist=86,  pvs=17, frustum=4)

At the lamp spot 54 of 218 lights were inside the render distance and all 54
were thrown away by the PVS or frustum test, so `computeLightSelection()` had no
candidate but the player light. The rule installed: **a light whose own
influence sphere contains the camera survives both remaining culls** — the PVS
test asks whether the lamp's own BSP region is visible and the frustum test
asks whether the lamp is on screen, and neither question bears on whether the
lamp lights the wall in front of the player. Distance culling is untouched and a
light outside its own radius is culled exactly as before.

After: `1/218` and `5/218` zone lights, `3/107` and `14/107` object lights.
Worth **+7.2%** on the no-lamp night frame, **+6.6%** on the lamp night frame,
−0.2% / +0.4% by day — light added at night and none by day, the correct shape.
It closes 1.07x of the 2.8x the lamp term needs.

### The A/B search that was NOT run, and why

The plan's Leg B asks for an A/B search until the oracle's night crops are
matched. **No parameter was tuned**, because Leg A's table says no parameter is
the term: fable's night ambient is already above the oracle's, and the missing
light is a spatial pattern (bright next to lamps, dark away from them) that no
global scalar can produce. Tuning one would have made the lamp-free spot wrong
to make the lamp spot right. This is a stated deviation from the plan, not an
omission.

### STOP — the term that is left

The plan's STOP condition "the dominant term is not reachable without a
renderer restructure" fired. The official client lights zone geometry from the
**baked per-vertex colours in the zone WLD**; fable reads `header.colorCount`
and skips the array (`// Skip colors`, `wld_fragments_mesh.cpp`), then writes
white into every textured zone vertex. Measured by temporarily instrumenting
`parseFragment36` for one zone load (instrumentation reverted before commit):
all 3,830 `R<n>_DMSPRITEDEF` region meshes carry `colorCount == vertexCount`,
72,436 zone vertices carry a colour, vertex-weighted mean byte 44.2/255 with
per-mesh minima at 0 and maxima at 255 — a real baked light distribution.

Wiring it in changes the loader, `ZoneGeometry::vertices`, both zone mesh
builders and the atlased builder, and — because the shader computes
`vColor = baseLighting * uTintColor * aColor`, under which multiplying EQ's
baked light by a 0.08 night ambient makes the world darker, not brighter — the
zone lighting equation itself. That moves every pixel golden, and a golden may
not be self-blessed. Boarded as **B110 (C)** with the recommendation that it be
its own unit, brought to Will with the gate's contact sheet.

A second, smaller residual in the same area: zone point lights are evaluated
per vertex on large zone triangles, so even a correctly-selected lamp is smeared
across a region instead of making a pool. Recorded in B110.

### T361's residuals, re-checked

T361's Ultravision match keys off `applyVisionAmbientFloor` and is untouched.
At T361's own spot the nearest lamp is 120.8 units away with radius 100, so no
light passes the new rule there; the only change is the +7.2% night baseline
(one distant light now surviving, `1/218` rather than `0/218`). T361's ratios
are therefore unchanged to within their own noise. **What did change is the
attribution:** B080 is not the reason T361's residual did not close, because at
T361's spot fable's night is 1.30x brighter than the oracle's rather than 35x
darker. B067's residual note is corrected on the board and a re-measurement of
the vision ratios against the oracle table is recorded there as **owed** — this
unit did not run one.

### Tests

`tests/test_zone_light_culling.cpp`, 4 cases, registered in
`tests/cmake/renderer_guards.cmake`. No Irrlicht device, no server, no assets:
it drives the real `computeLightVisibility`, `computeObjectLightVisibility` and
`computeLightSelection` through the public `computeOnce()` seam, with a
two-region BSP whose PVS says the regions cannot see each other and a frustum
facing away from the lamp — the live qeynos2 shape.

| stage | result |
|---|---|
| pre-fix (the tree as it stood) | **3 FAILED of 4** — `LampContainingTheCameraSurvivesPvsAndFrustum`, `TheSurvivingLampReachesLightSelection`, `ObjectLightContainingTheCameraSurvivesToo` |
| post-fix | **4/4 pass** |

`LampOutsideItsOwnRadiusIsStillPvsCulled` passes in both on purpose: it pins
that the fix is not "disable PVS for lights".

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2 test tree) | exit 0 |
| `ctest --test-dir build -j6` | **3916 / 3917**, sole failure the waived `AudioManagerTest.PreloadCommonSounds` |
| `scripts/build-gles2-x86.sh` | exit 0 |
| `scripts/ci/visual_gate.sh` (standalone) | **PASS**, **no golden moved** (working tree after the run lists only this unit's four files) |
| `scripts/build-arm-noble.sh` | exit 0 |
| `scripts/refactor/check_file_sizes.sh fable` | 57 checked, 57 ok, 0 warn, 0 fail |

### Server

Lock `T367 2026-09-05` taken atomically (absent on arrival), `up.sh --launcher`.
`character_data` 65 `Testchar` and 74 `Oraclealpha` were moved with `#goto`
between the two measurement spots and restored to their as-found rows
(`-74, 428.04, 3.75, h=160` and `-74, 428, 3.751, h=160`), **verified by
SELECT**. No account row, race or `logsys_categories` row was touched.
`down.sh` run and the lock deleted at the end.
