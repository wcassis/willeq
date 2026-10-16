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

## Oracle leg (lanes)

The official-client session (material-10 robe + a 11..16 robe, in-zone and at
character select, ledger + stills) runs on a lane through
`scripts/eqemu/oracle/oracle_session.sh`. At the time of writing it is NOT
YET RUN, and the reason is concrete:

- **Lane 1's oracle rig is held by a LIVE b1080 session** — `b1080_oracle`
  since 2026-10-16T10:48, and its `client_audio.wav` is still being written
  (a running capture on oracle VM .91), so the lock is live, not stale.
  Must wait for it to release.
- **Lanes 2/3/4's eqemu-server container will not stay up** (host .98/.97):
  `up.sh` reports the loginserver/world/zone "adopting" 25-26 already-running
  procs, yet the `akk-stack-eqemu-server-1` container is absent a moment later
  and `eqemu_stack_ready` is false. This is a lane-infra problem on the
  shared hosts, not a code defect; it needs the lane operators.

The range is independently settled by the archive + item-table census above,
which is what the predicate and its tests are pinned to; the oracle stills are
the remaining live confirmation.

## Not verified / left open

- The **oracle stills** (acceptance A/D) and the **fable-side still of the
  same seeded character** (D): pending a free, working lane (see above).
- The CI **build/gate leg**: pipeline 1422's `hygiene` and
  `zone-budget-census` failed only on **runner placement** — they landed on a
  `thinkpad` runner that the job's own `x86-build` guard refused
  (`refusing to build on thinkpad`); fable pipelines 1408/1406 pass on the
  real runners, and local hygiene + both new tests are green at the head.
  A fresh pipeline (25904c69) re-places the jobs.
