# B888-CC3 (#204): character select backdrop + Explore mode -- ORACLE LEG

The oracle leg (card step 1) is complete against the OFFICIAL client on the
lane-5 rig (Wine/Xvfb, 1280x1024, the B1048 one-shot driver:
`scripts/eqemu/oracle/oracle_session.sh`). Every row below is [measured] off a
named still/video from `oracle-a6` (the A-plan, 19 steps) or `oracle-b1` (the
B-plan, 9 steps) under `/home/user/kb-evidence/kb-cc3-charselect-explore/`;
the per-step ledger is `explore.tsv` beside this file.

## The select-screen backdrop

- [measured] A2, `oracle-a6/03_select_backdrop.png`: the select screen shows a
  3D **showroom, not a loaded zone** -- a back arch, two empty side pedestals,
  a central golden display platform, trees, a rock, blue sky. ONE character
  model stands on the central platform (the login-selected row, Testcaster).
  The left list window is visible (Testcaster/Testchar/Testmelee).
- [measured] A2, `oracle-a6/capture.pcap`: **no zone-server connection at
  character-select** -- the opcode stream is login -> world -> `OP_SendCharInfo`
  only (no `OP_EnterWorld` to a zone, no zone-stream). The backdrop is a
  client-internal scene, not a server zone.
- [measured] the client's OWN load log (`oracle-c1/client_dbg.txt`, captured
  this run by letting the official client run and pulling `Logs/dbg.txt`): the
  sequence at character-select is
  `World initialized: clz` (line 276) -> `load done.` (277) ->
  `Starting char select.` (279) -> `Clearing display buffers.` (281) ->
  `Initializing character select UI.` (285). The backdrop is the **`clz`**
  zone geometry, initialized as the client-internal world *before* the
  char-select UI opens. `clz` ships in the asset set as **`clz.eqg`**
  (`data/eq_client/clz.eqg`, 3,665,802 B) -- a **PFS archive**, the same
  format the zone loader opens for a `.s3d` (`PfsArchive::open()` validates the
  `PFS ` magic, not the extension; `clz.eqg`'s header is `..z343 7 \0 PFS ...`).
  There is no `clz.s3d`; the oracle client reads the `.eqg`.
- era: `clz` is the char-select showroom, which has shipped since the original
  1999 client, so it is **base-game-era, in-era** (pre-Kunark) -- not a
  post-Velious asset. It is NOT in `data/era_zones.tsv` because that file is the
  eqemu *server* zone table and the server never serves `clz` (the char-select
  backdrop is client-internal -- the pcap's empty zone connection, A2 above).
  No post-Velious ruling is needed; the implementation proceeds (step 2).
- [measured] the format is a P1999 terrain archive, not fable's zone format:
  `clz.eqg` holds 256 members -- 68 DDS textures, 29 `.mod` + 157 `.lit`
  static objects, `ter_clz.ter`/`.lit` (the terrain fragment), `clz.zon`,
  and **no `.wld`** (census in `ExploreScreen.BackdropZoneLoadsHeadless`,
  which pins exactly those counts). The `.eqg` extension is how the P1999-era
  client shipped its terrain archives (every zone at the P1999 root is
  `<zone>.eqg` + `<zone>.s3d`; a fable-loaded zone like anguish ships BOTH,
  but the 22 MB `.eqg` is the terrain half and fable never reads it).
  Fable resolves a zone to `<zone>.s3d` (`zone_component.cpp:808`) and
  FATALs on a WLD-less archive (`zone_component.cpp:837`), and **`clz`
  ships no `.s3d` anywhere on the host** -- so the 3D slice's format work is
  parsing `ter_clz.ter` + the `.mod`/`.lit` static objects straight from the
  `.eqg` (the PFS-open leg is the only production path that reaches them
  today), in the host's own scene manager, without touching the
  production zone path.

## Explore mode

- [measured] A3, `oracle-a6/05_explore_window.png`: the Explore button is at
  **root (40,263)** (measured by a y-sweep: root y 220/231/241/252 produced no
  state change, 263 entered Explore -- full-frame diff 56.4 vs ~6 baseline).
  On press the `EXW_ExploreModeWindow` appears bottom-left with
  "Click on a character to select that character." / "Press Enter to enter the
  game, or press Esc to leave explore mode." (EQUI_CharacterSelect.xml
  EXW_InfoLabel1/2); the left list window HIDES; the account's characters stand
  on their pedestals in the 3D scene.
- [measured] A4/A5, `oracle-a6/08_*.png` and `10_*.png`: clicking the left and
  right scene positions left `EXW_CharInfoLabel` (top-left) on the login-
  selected character (Testcaster); no visible selection marker on either figure.
  See "Left open" -- I could not confirm the click landed on the model mesh.
- [measured] A6, `oracle-a6/11_cam_forward.png`: holding `d` 8 s moves the free
  camera (full-frame diff 45.48 vs the pre-gesture frame).
- [measured] A7/A8, `13_cam_orbit.png` / `15_cam_zoom.png`: the driver's
  40-step mouse-drag and 3x wheel produced no measurable change (diff 5.01 /
  2.94) -- the orbit/zoom gestures were NOT captured on this rig (see "Left
  open").
- [measured] A9, `oracle-a6/18_back_select.png`: Esc returns to the normal
  select screen (list window back, single model on the central platform,
  ExploreModeWindow gone).

## Enter from Explore (the decisive question)

- [measured] B2/B3, `oracle-b1/06_inworld_settle.png`: pressing Enter in Explore
  mode enters the world, and the in-world chat reads
  `Current Zone [timorous] (Timorous Deep)` -- that is **Testcaster's** home
  zone. The character that entered was the **LIST-SELECTED** character
  (Testcaster, the login-selected row), NOT the login target (Testchar). The
  selection cursor is independent of which account row was logged in.

## Not verified / left open

- **Orbit / zoom on this rig.** The notes say "mouse drag = orbit, wheel =
  zoom" (wine-oracle.md:272-275), but the A6 run's 40-step left-button drag and
  3x wheel did not move the camera, and this run's probe
  (`oracle-c1/`, c-plan: strace attached at launch, Explore click at Xvfb-root
  (40,263)) shows the same rig's Xvfb-root Explore coordinate (40,263) did NOT
  open Explore either (`oracle-c1/99_explore_postcondition_failed.png` still
  shows the select screen). The Explore-mode access delta for this run is
  therefore 0 (`oracle-c1/access_explore_delta.txt`) and says nothing about
  Explore file loads. The B1048 driver's window-relative click (oracle-a6 A3)
  remains the only proven Explore entry; orbit/zoom gestures stay unmeasured on
  this rig.
- **Character-click selection.** A4/A5 show the info line unchanged after
  clicking both scene positions, but I could not confirm the click landed on a
  model mesh (the fixed root coordinates may have been just off the figure).
  The B-step already proves the selection cursor drives Enter, so the selection
  mechanism is observable but not yet pinned to a click.
- **The free camera's non-forward verbs.** `exploreCameraVerb()` maps only
  what was measured (forward on `d`); its strafe/orbit/zoom fields are the
  seam the 3D slice (the clz backdrop scene, which owns the camera) fills
  when orbit/zoom get measured. It is rowed in `EXEMPT_TESTS_GROUPS`
  (`check_zero_callers.py`, B888-CC3) until that slice lands.

## Implementation leg

Step 2 (load the backdrop scene in the CC-2 host, place the characters, the
Explore free-camera + click-select + Enter/Esc state machine, the
ExploreModeWindow overlay from the XML rects) and step 3 (tests, memory delta,
docs). CC-2 (the pre-zone render host, `char_preview_host.h` /
`char_preview_scene.{h,cpp}` -- a `CharacterModelView` in its own scene manager,
**no zone geometry**) is now merged into fable (tip `4e987f0c`). The backdrop's
scene file (the last "left open" item) is measured above: the `clz` zone,
shipped as `clz.eqg` -- the PFS-open leg of the production zone-load path
works against it, but its member census (no `.wld`, `ter_clz.ter` + `.mod`
static models instead) means standing the geometry is the 3D slice's own
format work, not a free reuse of `S3DLoader::loadZone` (run 1042, below).

### Progress, run 1042 (2026-10-17)

- The backdrop's scene file is the ONE "left open" item and is now [measured]
  (`clz` -> `clz.eqg`, PFS, in-era; above). The implementation proceeds.
- **Device-free layer LANDED and tested** (`test_explore_screen`, 11 cases,
  green on the x86 dev tree): the Explore state machine
  (`explore_screen.{h,cpp}` -- open/esc, click-to-row selection with a miss
  keeping the cursor, Enter entering with the LIST cursor per oracle-b1, the
  free-camera verb: forward on `d` is the only MEASURED verb, the rest are the
  unmeasured orbit/zoom seam) and the EXW_ExploreModeWindow overlay layout
  (`explore_layout.h`, quoted from the stock XML and checked against it by
  `ExploreLayout.MatchesTheXml`). The host wires the button live (CSW_Explore_Button
  is no longer the greyed T363 control; hit-test `isExploreButtonClick`),
  routes Enter/Esc through the machine while the mode is open, hides the left
  list window and draws the EXW overlay in its place (MEASURED, oracle-a6
  A3/A9). `ExploreScreen.BackdropZoneLoadsHeadless` pins the backdrop's
  format facts: `clz.eqg` opens as a PFS archive, its 256-member census is
  measured (68 DDS + 29 `.mod` + 157 `.lit` + `ter_clz.ter`/`.lit` +
  `clz.zon`, **no `.wld`**) and `S3DLoader::loadZone` refuses exactly that
  WLD-less archive (skips where the file is absent; `WILLEQ_CLZ` overrides
  the path).
- **The 3D backdrop scene (the `clz` zone content in the host's own scene
  manager + the account's character nodes + the free camera + the click
  raycast) is NOT landed yet.** Per the measured format fact above, standing
  the geometry is the 3D slice's own format work: parse `ter_clz.ter`
  (the P1999-era terrain fragment) and/or the 29 `.mod` + 157 `.lit` static
  models straight out of the `.eqg` (or extend the loader with a WLD-less
  leg), WITHOUT touching the production zone path. The seam is sized: the
  geometry-to-mesh idiom is `equipment_model_loader.cpp:500-599`
  (per-textureIndex buffers over `ZoneGeometry` vertices/triangles), and the
  own-scene-manager + render-to-texture idiom is the CC-2 paperdoll
  (`char_preview_scene.cpp`). The host's `backdropRowForClick()` currently
  returns -1 (every click a miss, the machine's own rule, oracle-consistent
  with A4/A5) until the model nodes stand up. `exploreCameraVerb()` is rowed
  in `EXEMPT_TESTS_GROUPS` (`check_zero_callers.py`, B888-CC3) until that
  slice wires it -- the other four Explore functions are live in
  `char_select_host.cpp`. See `FOLLOW-UP` in the run's completion comment.

## Driver fixes made this run (committed on this branch)

- Explore button coordinate: root (40,263), measured by y-sweep.
- Explore postcondition gates on the line's clean tail "select that character"
  (this rig mis-OCRs the head "Click on a character" as "Click an a character").
- The Esc keysym is `Escape` (this rig rejects `Esc` and `esc`).
- Plan TSV: `wheel` is 4 columns (N in hold_ms, still name in col 4); the
  character clicks run BEFORE the camera verbs (a post-camera click hits empty
  scene).
