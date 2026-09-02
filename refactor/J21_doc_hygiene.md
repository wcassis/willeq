# J21 — documentation hygiene after Batch J

Branch `task/J21-doc-hygiene`, base `fable` @ `9684644f`.

## Why

Ten Batch J units split large translation units today (J01–J11, J20) and
several bug-fix units moved functions between files. Each unit correctly
left out-of-territory documentation alone and recorded what it saw. The
tree is now full of documentation and comments that describe the old
layout, plus `file.cpp:NNNN` citations that no longer resolve to what they
claim. Batch J owes five units' worth of this.

## Scope settled for this unit

IN:

1. `docs/module_map.md` — regenerated for the post-Batch-J layout: every
   new TU, current `wc -l`, and the sections the map never had
   (Application, CommandProcessor, InventoryManager, WLD loader,
   ZoneShader, tests/cmake).
2. Every `CLAUDE.md` in the tree that states a fact Batch J or a recent
   unit falsified — file layout, TU counts, enum counts, retired build
   flags, superseded rationale.
3. `docs/decisions.md` and `docs/reference/shader_overview.md` /
   `docs/future/shader_permutation_handoff.md` file+line facts.
4. Stale `file.cpp:NNNN` citations in `tests/` comments, and in `src/` /
   `include/` comments where the citation names a file Batch J split.
   Comment text only.

Scope decision: the dispatch named `renderer_zone_load.cpp:165` and
`tests/test_eqe1_composite_open.cpp:8`, so `src/` and `include/` comments
are in territory when the edit is comment-only and the fact is a
file/line/layout fact. No production code and no test logic is touched.

A tree-wide citation sweep was run rather than fixing only the reported
instances: `scratchpad/cite.py` extracted every `path:NNN` token from
every `.md`, `.cpp`, `.h` and `.cmake` outside `docs/archive`, `refactor/`
and `review/`, resolved it against the tree and reported out-of-bounds and
wrong-content hits.

OUT (deliberate):

- References to eqemu server source (`zone/client_packet.cpp:6387` and
  ~180 like it). Those name `/mnt/projects/akk-stack/code`, not this tree;
  they are provenance for wire facts and are correct as written.
- `docs/plans/` and most of `docs/future/` prose. `docs/CLAUDE.md`
  declares `docs/plans/` "historical feature implementation plans"; they
  are snapshots and are not maintained. Only the two shader documents the
  dispatch named were touched, plus the two hard-broken
  `irrlicht_renderer.cpp:1170` citations in the detail-system plan.
- `refactor/deferred.md`. It is the orchestrator's queue of record and is
  contended by three concurrent slots; board item 23 (tier 4) is now
  closed by this unit and is reported for the orchestrator to strike
  rather than edited here.
- Architecture prose that could not be verified from the code. Where a
  document asserted intent, the prose was left and only the file/line
  facts changed. Listed in the Review.

## Verification

- `cmake --build build -j24`, full `ctest -j6` — zero new failures against
  `refactor/test_baseline.txt`. Comment edits can break tests that scan
  source text (J11 found four), so the full suite is the gate, not a
  subset.
- `scripts/refactor/check_file_sizes.sh --files` on every file touched.

## Review

### Verification

| Step | Result |
|---|---|
| `cmake --build build -j24` | First invocation hit the 600 s ceiling (exit 143); re-run exit 0. Per the protocol's exit-143 trap, every file in `build/bin` (221) was scanned for ELF magic and size — all 221 carry `\x7fELF`; the nine small ones are the standalone GLES2 probe tools, not truncated links. |
| `ctest --test-dir build -j6 --output-on-failure` | **3506/3507 pass.** Sole failure `AudioManagerTest.PreloadCommonSounds` — the documented ALSA host quirk. **Zero new failures vs `refactor/test_baseline.txt`.** |
| `scripts/refactor/check_file_sizes.sh --files <81 changed files>` | exit 0 — 75 ok, 6 warn, 0 fail. All six warns pre-date this unit (`eq.h`, `irrlicht_renderer.h`, `action_dispatcher.h`, `eq_action_handler.cpp`, `eq_merchant.cpp`, `eq_movement.cpp`); the largest edit to any of them added two comment lines. |

Comment edits can break tests that scan source text, so the gate was the full
suite, not a subset. Nothing scanned any text this unit changed.

### The numbers that were asked for

- **Event types: 97.** Counted directly from `GameEventType` in
  `include/client/state/event_bus.h` (97 enumerators, no sentinel). The 97th
  is `EntitySizeChanged`, added by T318 `db61e24c` — confirmed from the diff,
  not from the dispatch note. `IrrlichtBridge`'s switch has 97 distinct
  `case eqt::state::GameEventType::` labels, so "handles 99 of them" was
  impossible on its face. Both stale sites (root `CLAUDE.md` twice,
  `include/client/state/CLAUDE.md`) now say 97.
- **`RendererIntent` variant: 33 intent structs** — the existing number was
  already right; left alone.
- **`InputAction`: 40**, not 79. **`HotkeyAction`: 76**, not "150+".
  **`ActionDispatcher`: ~180 methods**, documented as "100+". All three
  re-counted and corrected in `src/client/input/CLAUDE.md`.
- **Registry-2 registrations: 142**, not 134, and they now live in
  `command_processor_registration.cpp`.
- **No file in the tree is over the 3,000-line FAIL ceiling.** The largest
  source file is `eq_entities.cpp` at 2,786. `module_map.md`'s grandfathered-
  pair paragraph is gone.

### Method

`scratchpad/cite.py` extracted every `path:NNN` citation from every `.md`,
`.cpp`, `.h` and `.cmake` outside `docs/archive`, `refactor/` and `review/`
— 385 of them — resolved each against the tree, and reported the ones that
are out of bounds or resolve to unrelated content. Sixteen were hard
out-of-bounds; roughly forty more resolved in-bounds to the wrong line. About
180 name eqemu server source (`zone/client_packet.cpp:6387` and friends);
those are provenance for wire facts, live in `/mnt/projects/akk-stack/code`,
and were left alone. After the pass, **zero citations in the tree resolve out
of bounds.**

Where a citation was inherently fragile it was replaced with a function or
symbol name rather than a corrected line number.

### Files changed and what was wrong

**Plan**

- `refactor/J21_doc_hygiene.md` — new.

**Docs**

- `docs/module_map.md` — REGENERATED. Missed every Batch J TU. Now covers
  `src/client/eq.cpp` + 27 `eq_*.cpp`, five `application_*.cpp` (new
  section), nine CommandProcessor TUs (new section), six InventoryManager TUs
  (new section), `irrlicht_renderer.cpp` + 24 `renderer_*.cpp`, four
  ZoneShader files (new section), thirteen `entity_*.cpp`, eight WLD files
  (new section), the seven SimulationWorker TUs, and the 18 `tests/cmake`
  fragments (new section). Line counts refreshed to `9684644f`. Added a
  "cite a symbol, not a line number" convention.
- `CLAUDE.md` (root) — "96 event types" (twice, incl. the IrrlichtBridge
  switch), "18 `eq_*.cpp` domain TUs", "eq.cpp itself (3,934 lines)",
  Application listed as one file, module_map described as a map of "the
  oversized files", stale suite count `2745/2746`.
- `docs/CLAUDE.md` — the X/Y swap example attributed to `eq.cpp` (it is in
  `eq_entities.cpp`); the Irrlicht swap example attributed to
  `irrlicht_renderer.cpp` (nothing like it there); the load-bearing player
  heading flip pointed at `src/client/eq.cpp` (now
  `EverQuest::ZoneProcessPlayerProfile` in `eq_zone_entry.cpp`).
- `docs/decisions.md` — five dead line citations in the slash-command
  ruling (`eq_chat.cpp:276`, `eq_bridge.cpp:947`, `application.cpp:901`,
  `console_input_handler.cpp:276`, `application.cpp:808,818`), the CRC
  ruling's `eq.cpp:718`, the UCS ruling's `eq_login_world.cpp:834`, and the
  "superseded wording still sits in graphics/CLAUDE.md:127" pointer, which
  is now discharged.
- `docs/reference/shader_overview.md` (line 134) and
  `docs/future/shader_permutation_handoff.md` (73, 405, 407) — named files
  J09 replaced; now name the three private headers.
- `docs/plans/detail_system_implementation_plan.md` — two
  `irrlicht_renderer.cpp:1170 (updateVertexAnimations)` citations. That
  method no longer exists anywhere; the pass is
  `SimulationWorker::computeVertexAnimations` in `sim_animation.cpp`.

**CLAUDE.md files**

- `include/client/state/CLAUDE.md` — "96 event types ... IrrlichtBridge
  handles 99 of them".
- `src/client/graphics/CLAUDE.md` — `irrlicht_renderer.cpp` "3,054 lines +
  19 renderer TUs"; EntityRenderer, ZoneShader and `wld_loader.cpp` all
  described as single files; the GLES/desktop source selection described as
  living in `zone_shader.cpp`; **and the composite-entity bullet (line 127)
  giving the dead T195/T197 address-space rationale as Will's reason.**
- `src/client/input/CLAUDE.md` — CommandProcessor as one file; three wrong
  enum/method counts; `input_action_bridge.cpp:~400-440` (the file is 365
  lines).
- `include/client/graphics/ui/CLAUDE.md` — registry 2 as
  `command_processor.cpp` with 134 registrations; registry 3 without its TU.
- `docker/CLAUDE.md` — documented `scripts/build-arm-noble.sh --no-gles2`,
  retired by P32 and absent from the script's own usage text (the actual
  third flag is `--t122-texprobe`); the target table described the Pi as
  plain "Lima/Mesa GLES2" rather than the native `COpenGLES2Driver` on
  DRM/KMS; "~5,200 lines across 10 files" for `irrlicht-drm/` (6,170 across
  14). **This closes tier-4 board item 23.**

**Composite-entity dead rationale (the three sites `docs/decisions.md`
names)**

- `src/client/graphics/CLAUDE.md:127`, `renderer_zone_load.cpp` (the
  `loadZoneStep0_EQE1` comment) and `tests/test_eqe1_composite_open.cpp`
  header all said the Pi's address-space exhaustion is why composites are
  off. It is not, since T284/T289. All three now state the resident-cost
  reason and point at `docs/decisions.md` 2026-08-27.

**Test comments (stale citations)**

- `test_first_person_tunables.cpp` — `application.cpp:1231` x3,
  `application.cpp:1162`, `application.cpp:395`, `eq_chat.cpp:344` (the file
  is 948 lines).
- `test_headless_command_refusal.cpp` — "grep application.cpp:
  registry=pending"; that string is in `application_commands.cpp` now, so
  the instruction would have found nothing.
- `test_eq_action_handler_cast.cpp` — the OP_Buff path attributed to
  `eq_player_info.cpp` (now `eq_player_buffs.cpp`), plus two `eq.cpp`
  PlayerProfile references and an `eq.cpp` OP_Stun reference.
- `test_wire_layout_t145.cpp` — the four double-prefix senders placed in
  `eq_player_info.cpp`; they are in `eq_player_social.cpp` since J07.
- `test_zone_connection_flow.cpp` — nine citations (five out of bounds).
- `test_zone_position_convention.cpp` — nine (four out of bounds).
- `test_combat_manager.cpp` — five out-of-bounds citations **plus a whole
  paragraph of stale mechanism** (see "needs attention" below).
- `test_state_modules_secondary.cpp`, `test_state_modules.cpp`,
  `test_command_processor.cpp`, `test_position_store_coherence.cpp`,
  `test_position_frame_roundtrip.cpp`, `test_login_world_flow.cpp`,
  `test_scribe_scroll.cpp`, `test_first_person_body_draw_rule.cpp`,
  `test_constrained_renderer.cpp`, `test_command_output_routing.cpp`,
  `test_headless_shutdown.cpp`, `test_eq_commands_equip.cpp`,
  `test_spell_manager.cpp`, `test_graphics.cpp`,
  `test_wld_conversions.cpp` (seven `wld_loader.cpp line NNN` citations, all
  now in `wld_fragments_{mesh,actor}.cpp`), `test_wld_texture_loading.cpp`
  (two, now in `wld_fragments_material.cpp`).

**Source/header comments (comment text only, no code)**

`renderer_zone_load.cpp`, `renderer_slash_commands.cpp`,
`zone_shader_sources_gl.h`, `ui/character_model_view.cpp`,
`eq_profile_appearance.h`, `action_dispatcher.h`, `eq_zone.cpp`,
`eq_zone_entry.cpp`, `eq_commands.cpp`, `eq_commands_movement.cpp`,
`eq_commands_spells.cpp`, `eq_login_world.cpp`, `eq_merchant.cpp`,
`eq_movement.cpp`, `eq_player_buffs.cpp`, `eq_action_handler.cpp`,
`command_processor_registration.cpp`, `command_processor_social.cpp`,
`renderer_camera.cpp`, `renderer_zone_merge.cpp`, `renderer_zone_merge.h`,
`irrlicht_renderer.h`, `first_person_tunables.cpp/.h`,
`first_person_body_draw.h`, `entity_size.cpp`,
`entity_first_person_draw.cpp`, `entity_mesh_build.cpp`,
`entity_updates.cpp`, `boids_manager.h`, `static_ui_input.h`,
`merged_help.h`, `renderer_command_catalog.cpp/.h`, `combat.h`,
`command_processor.h`, `inventory_manager.h`, `spell_manager.h/.cpp`,
`skill_packet_bodies.h`, `behind_target.h`, `eq.h`, `hotbar_model.cpp`,
`state/inventory_state.cpp`, `state/player_state.cpp`.

Three recurring classes:

1. **Split-file misdirection** — a live symbol attributed to the TU it used
   to live in (`eq.cpp` -> `eq_zone_entry.cpp` / `eq_player_state.cpp` /
   `eq_zone_dispatch.cpp`; `application.cpp` -> `application_commands.cpp` /
   `application_zone_loading.cpp`; `irrlicht_renderer.cpp` ->
   `renderer_slash_commands.cpp` / `renderer_camera.cpp` /
   `renderer_init.cpp` / `renderer_hud_env.cpp`; `entity_renderer.cpp` ->
   `entity_mesh_build.cpp`; `command_processor.cpp` ->
   `command_processor_social.cpp`; `eq_player_info.cpp` ->
   `eq_player_social.cpp` / `eq_player_buffs.cpp`; `inventory_manager.cpp`
   -> `inventory_cursor.cpp` / `inventory_item_parser.cpp`;
   `wld_loader.cpp` -> `wld_fragments_*.cpp`).
2. **"X is over the file-size ceiling"** — nine comments justify a design
   choice with a ceiling that Batch J has removed. Reworded to past tense,
   because the *choice* is still the right one; only the tense was false.
   `renderer_camera.cpp` was the sharpest case: J11 moved a block whose own
   comment says "GATHER lives here, because irrlicht_renderer.cpp is the ONE
   translation unit…" into a different TU, so the comment contradicted its
   own file.
3. **Gravestone drift** — comments describing functions that were deleted
   (`Application::syncCombatState`, T104; `Application::syncInventoryState`,
   undated) as if they still ran every frame.

### Needs a code change — reported, not done

Nothing in this unit's sweep requires a production code change. Two things
worth the orchestrator's attention:

1. **`Application::syncInventoryState()` has no gravestone.**
   `application.h`'s "Synchronization" section carries proper deleted-function
   notes for `syncGameStateFromClient` (T104), `syncCombatState` (T104),
   `syncNPCInteractionState` (T49) and `syncSpellState` (T44) — but
   `syncInventoryState` was removed with no note, and three comments still
   described it as a live per-frame poll. J21 corrected the three comments
   (the writes now happen at the mutation site,
   `EverQuest::SyncInventorySlotToGameState` in `eq_merchant.cpp`) but did
   NOT add the gravestone to `application.h`, because that is a claim about
   which unit deleted it and I could not establish that from the tree.
2. **`tests/test_combat_manager.cpp`'s loot thread-trace was describing
   deleted code.** It named `EverQuest::LootAllFromCorpse` (does not exist),
   an `IsLooting()` dispatch branch (T268 removed it) and
   `ZoneProcessLootItem` (T268 deleted it). Worse, its conclusion — "the UI
   loot-all does NOT call `CombatManager::LootAll()` at all" — is now false:
   `LootAllIntent` in `eq_bridge.cpp` calls exactly that. The comment was
   rewritten from the current code; **the test bodies were not touched**, and
   they still pass. Someone who owns the loot state machine should confirm
   the tests still cover the intended path now that both routes converge.

### Deliberately left alone

- **~180 citations of eqemu server source** (`zone/client_packet.cpp:6387`,
  `common/patches/titanium_structs.h:1160`, …). They point at
  `/mnt/projects/akk-stack/code`, not this tree; the wire facts they anchor
  are the whole point and a line number there is the only useful handle.
- **`docs/plans/` and most of `docs/future/`.** `docs/CLAUDE.md` declares
  `docs/plans/` historical, and `docs/reference/shader_overview.md` carries
  a HISTORICAL SNAPSHOT banner. Only the four documents the dispatch named
  plus the two hard-broken detail-plan citations were touched; the rest of
  their prose describes designs that were never built and would be invented
  architecture to "correct".
- **`refactor/deferred.md`.** Board item 23 (tier 4, docker doc debt) is
  closed by this unit and there is a matching mention in the T215 tail
  block, but three slots are live against that file today and it is the
  orchestrator's queue of record. **Two strikes are owed there; reported,
  not applied.**
- **Architecture prose I could not verify.** Where a comment asserted intent
  rather than a location — the `eq_movement.cpp` / `eq_zone.cpp` F-unit
  "why this stayed in eq.cpp" headers, `command_processor_social.cpp`'s
  GameStateSyncBridge comparison — the prose stands and only the file names
  moved. The F-unit split headers ("Split out of eq.cpp (Batch F, unit
  F13)") are history and are correct as history; they were not rewritten.
- **`eq_entities.cpp`, `eq_group.cpp` (slot3/J08) and
  `constrained_texture_cache.cpp` (slot1/J10).** Not touched, not even in
  comments. `eq_entities.cpp`'s F11 header still discusses at length what
  "stays in eq.cpp" — accurate as Batch F history, misleading as a map now
  that J02 has moved most of it again; that is J08's or a follow-up's call.
  None of its citations are out of bounds. Comments *pointing at* those
  files from elsewhere were corrected.
