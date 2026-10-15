# B888-CC2 (#204): character select + creation 3D preview

## What landed

The pre-zone 3D preview: the select screen stands up the SELECTED character's
body (race/gender/face from the T14 wire), and the creation screen stands up
the SAMPLE model of the chosen race + gender (+ committed FacePick picks).
Rotate is live (default on). The model is rendered by `CharacterModelView`
(the T60 paperdoll -- its own scene manager, its own camera/light, render-to-
texture) hosted by a new `CharPreviewScene` on the render thread, and blitted
into the model area by the pre-zone host. No zone geometry is loaded.

New TUs (M00): `char_preview_host.{h,cpp}` (device-free appearance selection),
`char_preview_scene.{h,cpp}` (the model host). Wire: the T14 parse
(`login_world_component.cpp`) now reads Gender/Face/HairColor/HairStyle from
the Titanium arrays (they were on the wire all along; fable dropped them).

## Oracle leg (lane 1, driver B1048)

Plan `scripts/eqemu/oracle/plans/charselect_preview.tsv`; run evidence in
`/home/user/kb-evidence/kb-cc2-charselect-preview/oracle10/` (38 stills + the
full video, 1280x800, 10 fps, 4053 frames). The driver (oracle10) drove the
official Titanium client end to end: login -> char select -> Rotate toggle
pair -> Human-male creation model -> FacePick modal -> close -> Gnome-female
creation. The run's cleanup exited 65 on lane teardown after the capture
completed (the stills and video are present and complete); see "Not verified".

Measurements (file: the oracle10 stills, 1280x800, crop +0+30):

| state | evidence | measurement |
|---|---|---|
| select model present | `03_still.png` | "Testchar [60 Warrior] / Greater Faydark" floats centre; the 3D figure is centre-screen on a circular platform, backdrop = a zone scene (columns, hills, water, blue sky) |
| select model framing | `03_still.png` bright-extent | model pixels x 400-898, y 160-698 (w 498 h 538) |
| Rotate button | `03_still.png` | present, LIT (default on, per UIGAP-ORACLE/318) |
| select spin | `05..08` model-region diff | the model region changed **0.00-1.34%** across the four post-toggle stills -- the figure was STATIC in this capture (the 54% on `04` is the button repaint + backdrop; the 16.95% on `08` is a backdrop shift, not the figure) |
| create model present | `09_create_human_male_model.png` | male Human on a pedestal of stone pillars, backdrop = gothic archway + foliage |
| create model framing | `09` bright-extent | model pixels x 500-818, y 192-618 (w 318 h 426) |
| FacePick over creation | `12_create_human_male_face_open.png` | the Set Face modal opened over the Human-male creation page |

The model-region pixel diffs were measured with
`/home/user/.hermes/profiles/dev/cache/scratch/rot2.py` (PIL frame differencing,
24/25 threshold); the bright extents with `bbox.py`.

## The code

- `selectPreviewAppearance()` / `createPreviewAppearance()`
  (`char_preview_host.cpp`) decide which body per state. Select: the selected
  row's T14 fields. Creation: the page-0 race + gender + FacePick picks
  (face/hairstyle/haircolor/beard/beardcolor map 1:1 to the appearance; Eyes ->
  `texture`, Wode -> `helm`, best-fit -- see below).
- `CharPreviewScene` (`char_preview_scene.cpp`) runs a `CharacterModelView`
  off the renderer's scene manager + driver, borrowing the ENTITY RENDERER'S
  race loader (B1029's trap: it never builds a second loader or re-wires the
  cache -- the loader `createEntityRenderer()` wired at init already has the
  client path AND the constrained texture cache, exactly the paperdoll's wiring
  in `OverlaysComponent::ensurePaperdollInitialized()`). The race model loads
  on demand (`createAnimatedNodeWithEquipment -> ensureModelDataLoaded ->
  loadGlobalModels`), so it stands up with no zone.
- The host (`char_select_host.cpp`) stands the model up each frame, advances
  the spin (Rotate), renders to its 256x256 texture, and blits it into
  `CharSelectLayout::modelAreaRect()` BEFORE the screen draws. A click in the
  Rotate button rect toggles the spin. The model is drawn HERE, not in
  `renderCharSelectScreen`/`renderCharCreateScreen`, so the device-free screen
  functions the visual gate (CC-4) and the frame tests call directly stay
  model-free (acceptance E: the gate is unchanged).

## Tests

`tests/test_char_preview.cpp` (new; `ctest -R CharPreview`):
- `CharPreviewAppearance.*` -- device-free: the right race/gender/appearance
  per state, and the invalid cases (no race, no row).
- `CharPreviewRotate.ToggleRoundTripsWithoutInit` -- the Rotate flag round-
  trips and the host is a safe no-op before init().
- `CharPreviewHeadlessTest.*` -- the model leg under EDT_NULL + the shipped db
  + the real client tree (skips when absent): a Human body stands up on
  `CharacterModelView` with NO zone, the pre-zone memory delta (the acceptance
  D leg), and two distinct races (Human, Dwarf) resolve to DIFFERENT model
  codes through the production loader -- the "right race model in the view"
  net, checked live (no blessed golden). The host's own spin (advance its
  running angle while Rotate is on, hold while off) is render-thread-only and
  exercised in the visual gate, not here -- a headless test that read the
  paperdoll's `getRotationY()` would credit the bare name to both this class
  and the dead `EntityRenderer::getRotationY`, so the host owns its angle and
  only ever SETS it (see the test's note).

B1029's call-order trap, cited in the test header: a fresh `RaceModelLoader`
loads nothing until setClientPath() + setConstrainedTextureCache() on the
loader AND its mesh builder land before create. The test fixture wires that
order in SetUp (db -> device -> loader -> setClientPath -> cache -> cache-on-
mesh-builder -> setCharacter); the production host sidesteps it by borrowing
the entity renderer's already-wired loader.

## Memory (acceptance D)

The pre-zone preview stands up ONE body at a time (the view releases the
previous model when the character changes), plus its 256x256 render target.
No zone geometry, no zone scene.

MEASURED live through the memory ledger by
`test_char_preview.OneModelIsTheWholePreZoneDelta` (EDT_NULL, x86 dev tree,
run 2026-10-15, `entity_models` category = the RaceModelLoader's own
MemoryScope, the same one a zone entity's model is charged to):

- first body (Human male): +28,654,160 B -- model data PLUS that race's
  shared textures, loaded with the first body and then held;
- re-stand-up of the same race+gender: +0 B (cache hit -- the host's
  per-frame steady state charges nothing);
- same-race gender switch: +0 B (old body released, new one stood up: the
  category holds one body, never two);
- after `releaseCharacterModel()`: still at the one-body level (the nodes
  are gone; the loader's shared cache -- which zone entities also use --
  holds the first body's data past the release, out of the preview's
  control).

The 256x256 render target (~131 KB RGBA8 on a GLES2 device) is charged
separately to `textures_driver` and is below the test's resolution under
EDT_NULL; the `gpu_buffers` / `gpu_driver` rows are unchanged (the model's
textures go through the same constrained cache as every other model). Net:
the pre-zone delta is bounded by ONE body -- 28,654,160 B plus 131 KB here,
and the test fails if re-standing or switching ever charges a second full
body.

## Not verified / left open

- **The select screen's spin period/direction were NOT measured from this
  capture.** The model region was static (0.00-1.34% diff) across the four
  post-toggle stills -- the 10 fps capture + the driver's 2.5-4 s wait steps do
  not resolve a slow spin, and the toggle-frame diff is dominated by the button
  repaint. The code's spin rate (0.15 deg/ms ~= 90 deg/s) is the paperdoll's
  idle rate, a reasonable default, NOT an oracle measurement. The video is in
  the evidence dir for a slower re-extraction if the rate needs to be pinned.
- **The oracle run's cleanup exited 65** (lane teardown) after the capture
  completed; all 38 stills + the video are present. The Gnome-female create
  still (`17_...`) was in progress at the last timeline entry; re-run the plan
  on a free lane for the complete Gnome still if the ledger needs it byte-for-
  byte (the Human-male leg is complete).
- **Eyes -> `texture`, Wode -> `helm`** are best-fit appearance mappings: the
  model loader consumes no field literally named "eyes"/"wode", and `texture`
  (body variant) / `helm` (head variant) are the closest variant fields. The
  FacePick picks that DO map 1:1 (face, hair, haircolor, beard, beardcolor)
  are the ones that visibly change the classic model.

## Files

- `include/client/eq_types.h` -- `CharacterSelectEntry` + gender/face/hair.
- `src/client/login_world/login_world_component.cpp` -- T14 appearance parse.
- `include/client/login_world/character_select.h` + `src/.../character_select.cpp`
  -- row carries the body; `BuildCharacterSelectRows` copies it.
- `include/client/graphics/ui/char_preview_host.h` + `.cpp` -- appearance selection.
- `include/client/graphics/ui/char_preview_scene.h` + `.cpp` -- the model host.
- `include/client/graphics/ui/char_select_host.{h,cpp}` -- host wiring, blit, Rotate.
- `include/client/graphics/ui/char_select_layout.h` -- `modelAreaRect()`.
- `include/client/graphics/irrlicht_renderer.h` -- `getSceneManager()`.
- `tests/test_char_preview.cpp` + `tests/cmake/ui_rendering.cmake` -- the net.
- `scripts/eqemu/oracle/plans/charselect_preview.tsv` -- the oracle plan.
- `README.md` (44, 68) -- the "no 3D scene / preview unimplemented" lines.
