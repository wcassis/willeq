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
- era: the char-select showroom has shipped since the original 1999 client,
  so it is **base-game-era, in-era** (pre-Kunark) -- not a post-Velious asset.
  See "Left open" for the one thing I could not pin down: the exact `.s3d`
  filename.

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

- **The exact `.s3d` filename** of the backdrop scene. The pcap proves it is
  client-internal (no zone-server load), and the client ships 1006 zone `.s3d`
  files, but these runs did not capture the client's own load log, so I have
  not confirmed which file it opens. The implementation leg needs that name to
  pull the geometry from the asset set.
- **Orbit / zoom on this rig.** The notes say "mouse drag = orbit, wheel =
  zoom" (wine-oracle.md:272-275), but my driver's 40-step left-button drag and
  3x wheel did not move the camera in the A6 run. The gesture shape likely does
  not match how this rig's DirectInput samples motion.
- **Character-click selection.** A4/A5 show the info line unchanged after
  clicking both scene positions, but I could not confirm the click landed on a
  model mesh (the fixed root coordinates may have been just off the figure).
  The B-step already proves the selection cursor drives Enter, so the selection
  mechanism is observable but not yet pinned to a click.

## Implementation leg (NOT started)

Step 2 (load the backdrop scene in the CC-2 host, place the characters, the
Explore free-camera + click-select + Enter/Esc state machine, the
ExploreModeWindow overlay from the XML rects) and step 3 (tests, memory delta,
docs) are not started. CC-2 (the pre-zone render host, `char_preview_host.h` /
`char_preview_scene.{h,cpp}` -- a `CharacterModelView` in its own scene manager,
**no zone geometry**) is now merged into fable (tip `4e987f0c`). The
implementation leg needs the backdrop `.s3d` name (above) before it can load
the zone geometry.

## Driver fixes made this run (committed on this branch)

- Explore button coordinate: root (40,263), measured by y-sweep.
- Explore postcondition gates on the line's clean tail "select that character"
  (this rig mis-OCRs the head "Click on a character" as "Click an a character").
- The Esc keysym is `Escape` (this rig rejects `Esc` and `esc`).
- Plan TSV: `wheel` is 4 columns (N in hold_ms, still name in col 4); the
  character clicks run BEFORE the camera verbs (a post-camera click hits empty
  scene).
