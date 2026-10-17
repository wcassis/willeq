# B1084 (#404): robe material range per the oracle — one predicate

**The defect.** A worn robe of material 10 (item 11641, Crimson Robe of
Alendine) drew the **naked body**: `isRobeTexture()` in
`include/client/graphics/eq/equipment_textures.h` said the robe range was
**11..16**, while the mesh builder's own comment and the robe-index
arithmetics said **10..16** (mesh_building.cpp "Robe textures 10-16 map to
clk04-clk10", `textureId - 10` indices). The two spellings disagreed by
exactly the one material the issue was filed about.

## The measured range: 10..16 [measured]

Three independent sources of truth agree:

1. **`global_chr.s3d` carries exactly seven robe texture groups,
   `clk04`..`clk10`** (six pages each: `clk0401`..`clk0406` …
   `clk1001`..`clk1006`; 1,617 archive members). A range of 11..16 (six
   materials) would leave `clk04` with no material and material 10 with no
   texture; the seven groups partition cleanly across exactly 10..16.
   Re-measured at 2026-10-16 against `data/eq_client/` via the repo's own
   PFS reader (`/home/user/kb-evidence/kb-b1084-robe-material-10/
   archive_census.tsv`).
2. **`globaldaf_chr.s3d` carries `dafch10xx`..`dafch16xx`** DAF body
   textures (3 pages each) and **nothing for 05..09** (171 members; same
   census file). The dafch10 group is what the robe body mesh textures with.
3. **The peq items table's robe-named chest items exist for exactly
   materials 10,11,12,13,14,15,16** (lane 1, 2026-10-16,
   `/home/user/kb-evidence/kb-b1084-robe-material-10/lane1/facts14.tsv`).
   Item 11641 (Crimson Robe of Alendine) = material **10**; item 1320
   (Flowing Black Robe) = material **11** (`lane1/facts13.tsv`).

The mapping, now ONE spelling (`getRobeClkTextureNumber()`,
`equipment_textures.h:93`): **10→clk04, 11→clk05, 12→clk06, 13→clk07,
14→clk08, 15→clk09, 16→clk10** — consistent with the pre-existing
mesh_building.cpp comment and the robe-index arithmetic, and with the
official client drawing the robe body + clk04 for material 10 (oracle leg,
see below).

## The change

- `equipment_textures.h`: `kRobeTextureMin/Max` + the **ONE**
  `isRobeTexture()`; `isNPCOnlyTexture()` corrected to **5..9** (10 is a
  robe, not an NPC texture); `getRobeIndex()`/`getRobeClkTextureNumber()`
  consistent with the seven-clk table.
- Every former range spelling now routes through the predicate (grep-proof:
  `git diff origin/fable...HEAD` shows the only robe-range expressions in
  the tree are in `equipment_textures.h`):
  - `mesh_building.cpp` (clk rewrite gate + target number)
  - `entity_prep_worker.cpp` (variant prefix `clk%02d`)
  - `renderer_zone_load_entities.cpp` (same prefix build)
  - `entity_renderer.cpp` (NPC robe body variant selection)
  - `animated_mesh_creation.cpp` (robe decision + bodyVariant=1)
  - `equipment_textures.cpp` (`getEquipmentTextureName()` clk spelling)

## Tests (red/green)

- **`test_robe_texture_range`** — the 0..24 + 240 table over the measured
  fact, plus the full-256 no-overlap sweep. **RED on the parent commit's
  11..16 header (3/3 fail), GREEN at the head** — `red_run.tsv`.
- **`test_robe_entity_prep`** — the production per-entity entry
  (`createAnimatedNodeWithEquipment`, the seam EntityPrepWorker's results go
  through): chest material **10 takes the robe path** (bodyVariant=1, the
  `{RACE}01` mesh), no chest material does not, material 16 (the far end of
  the range) does, material 17 (Velious) is not a robe. **RED on the
  parent for material 10 (1/4 fail — exactly the bug), GREEN at the head** —
  `red_run.tsv`.

## Oracle leg (lane 5) — RUN, 2026-10-17

One official-client session through `scripts/eqemu/oracle/oracle_session.sh`
(lane 5, ThinkPad rig) covered BOTH halves of the range in a single
seated character — Testcaster (id 67, High Elf female Wizard, gm 1):

- **Material 10 at character select.** Chest slot 22 (server numbering)
  was seeded by the host with item 11641 (Crimson Robe of Alendine,
  material 10) before login, so the character is auto-selected wearing the
  mat-10 robe. `01_char_select.png` is the still: the select preview shows
  the character **clothed in a red robe** — the official client draws the
  material-10 robe body + texture (the exact case fable drew naked). The
  in-zone part of the session was first-person camera throughout, so no
  in-zone mat-10 third-person still exists in this capture; the dense video
  sample (29 frames, t=80..220s) confirms: red robe in every third-person
  frame (character select), first-person in every in-zone frame, no naked
  model anywhere.
- **Material 11 in-zone, third-person, via the GM path.** `/say #summonitem
  1320 0` (Flowing Black Robe, material 11) + `/putdown 17` (chest, Titanium
  numbering); `10_mat11_equipped.png` shows the character in-zone **clothed
  in a black robe** — the same dafch11/clk05 body mesh, a visibly different
  texture from the mat-10 red.

Ledger: `ledger_check.txt` = **0 broken references**; `steps.tsv` carries
one row per step with epoch bounds and per-step opcodes/INI-diff files;
raw `capture.pcap` + `video.mp4` in the same directory. Session evidence:
`/home/user/kb-evidence/kb-b1084-robe-material-10/oracle_mat10/`.

The `isNPCOnlyTexture` claim: the zone has no NPC carrying robe texture 10
that this session could still, so that half rests on the archive census
(`dafch10xx` is the robe body, `archive_census.tsv`) rather than a live NPC
still — left open, not a gap in the 10..16 range itself.

## Fable still (lane 5) — RUN, 2026-10-17

The same seeded character through the FIXED client, headless EGL/X11 on
the ThinkPad's lane-5 stack (willeq build of this branch): Testcaster
zoned in, `#summonitem 11641` + `/putdown 17` equipped the mat-10 robe
(chat log: "Put Crimson Robe of Alendine in Chest [1]"), chase camera
still. **The robe mesh + crimson texture draw for material 10** — no
naked body, no missing texture
(`/home/user/kb-evidence/kb-b1084-robe-material-10/fable_still/
b1084_fable_mat10_chase.png`). Proposed visual_gate golden (BLESS, not
committed — Will's rule: new goldens are PROPOSED).

## CI

Pipeline **1491** on head `62468359` (MR !212): hygiene, build-headless,
build-test, gate, zone-budget-census all **success** (thinkpad runner per
B1087/!211). Pipeline 1477 (the pre-B1090-fix head) failed build-test only
on the `WILLEQ_DATA_DIR` runtime path that commit `62468359` corrected.
