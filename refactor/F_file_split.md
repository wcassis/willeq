# Batch F — Mechanical File Splitting

Split `eq.cpp` (19,676 lines) and `irrlicht_renderer.cpp` (16,394 lines) —
plus `simulation_worker.cpp` and `entity_renderer.cpp` — into domain
translation units. **No class interface changes. No behavior changes.**
Method definitions move verbatim to new `.cpp` files; headers are untouched
except where a `static` file-local helper must become accessible to the moved
code (note any such change in the unit's review section).

Cluster→unit assignments and line ranges: `docs/module_map.md`.
Standing decisions (patterns, ceilings, workspace): `docs/decisions.md`.

## Process per unit

1. Orchestrator writes `refactor/F##_<name>.md`: exact method list, target
   file(s), include list, acceptance criteria.
2. Executor agent (Sonnet) in an assigned slot (`/mnt/projects/fable/wt/slotN`),
   branch `task/F##-<name>` cut from `fable`:
   - Execute the plan exactly. No scope creep, no drive-by fixes.
   - `cmake --build build -j24` clean, `ctest` green.
   - `python3 scripts/refactor/verify_move_only.py <base>..HEAD` clean.
   - `bash scripts/refactor/check_file_sizes.sh` on changed files.
   - Write the review section into the unit file; report deviations.
3. Orchestrator reviews report + diff stat + verifier output, rebases on
   `fable`, fast-forward merges in `main/`, deletes task branch, updates the
   table below.

New file naming: `src/client/eq_<domain>.cpp`,
`src/client/graphics/renderer_<domain>.cpp`,
`src/client/graphics/sim_<domain>.cpp`, `src/client/graphics/entity_<domain>.cpp`.
Each new file must be added to CMakeLists.txt.

## Test baseline (recorded 2026-07-22, slot1 @ 93a3c2a)

`willeq` and 83 tools build. 840 ctest cases: 98% pass. Pre-existing failures
are recorded in `refactor/test_baseline.txt` — **unit acceptance means zero
NEW failures relative to that file**, not a fully green suite. Compare with:
`ctest ... | grep -E '\(Failed\)|\(Not Run\)'` vs the baseline file.

Pre-existing breakage (repair tracked as F00, see below):
- 14 test targets fail to BUILD. Most are link errors — production code grew a
  `WilleqDatabase` dependency (`getDbString`, `getMusicFile`) never added to
  the tests' minimal source lists (test_string_database, test_eff_loader, and
  the audio test family). `test_spell_database` is API drift: it calls
  `loadFromFile`/`getAllSpellIds`/`getSpellsForClass`/`getSpellsByLevelRange`/
  `getBeneficialSpells`, none of which exist on `EQ::SpellDatabase` anymore.
- 6 tests genuinely fail: LoggingTest.ModuleCount_MatchesExpected,
  ComprehensiveZoneTest.{ClassicZones,OverallSummary},
  DoorLoadingUnitTest.MultipleZonesHaveObjectGeometries,
  UISoundsIntegrationTest.{SoundIdsExistInSoundAssets,SoundFilenamesMatchSoundAssets}.

## Unit — F00 (test repair, before or alongside F01)

| Unit | Scope | Status | Slot | Commit |
|---|---|---|---|---|
| F00 | Fix 13 link-broken test targets (add missing sources to tests/CMakeLists.txt lists). Rewrite test_spell_database against the current SpellDatabase API. Investigate the 6 genuinely-failing tests; fix or document each as environment-dependent in test_baseline.txt. | done | slot1 | `6955f0e` |

F00 outcome: suite fully green (1192 tests) after adding the gitignored
`data/eq_client -> /mnt/projects/EverQuestP1999` symlink each worktree needs
(provisioned in main + all slots). `refactor/test_baseline.txt` is now empty;
unit acceptance is simply a green ctest run (modulo the documented RDP flake).

## Units — eq.cpp

| Unit | Scope (module_map clusters) | Target file(s) | ~LOC | Status | Slot | Commit |
|---|---|---|---|---|---|---|
| F01 | RegisterCommands + runPmemDiagnostics/runLoadDiagnostics | eq_commands.cpp | 2,481 | done | slot2 | `cea246d` |
| F02 | Static name/string tables + dump helpers | eq_strings.cpp | 735 | done | slot1 | `d7b6371` |
| F03 | Login + world connection | eq_login_world.cpp | 672 | done | slot1 | `75fb045` |
| F04 | Guild + GM commands (UCS cluster turned out to be dead commented code — live UCS already in ucs.cpp; no eq_ucs.cpp created) | eq_guild.cpp, eq_gm.cpp | 415 | done | slot2 | `7920faa` |
| F05 | Trade + loot/vendor/bank/trainer + inventory + recipes | eq_merchant.cpp | 1,528 | done | slot1 | `8065e59` |
| F06 | Chat + text messages | eq_chat.cpp | 510 | done | slot1 | `369a5b1` |
| F07 | Stat getters + misc low-coupling clusters | eq_player_info.cpp | 1,913 | done | slot3 | `564bffd` |
| F08 | Movement + pathfinding + water + keyboard | eq_movement.cpp | 1,861 | done | slot1 | `9d2ac94` |
| F09 | Zone geometry + zone change/teardown | eq_zone.cpp | 755 | done | slot2 | `ca3b032` |
| F10 | Bridge processing (intents/events) + InitGraphics + loot flow + audio init | eq_bridge.cpp | 1,739 | done | slot1 | `beebd3f` |
| F11 | Entity/spawn tracking + appearance + world objects | eq_entities.cpp | 2,340 | done | slot2 | `3463909` |
| F12 | Group / raid / pet | eq_group.cpp | 603 | done | slot2 | `c8ff48b` |
| F13 | Combat resolution | eq_combat_actions.cpp | 495 | done | slot1 | `6ad6bc5` |

## Units — irrlicht_renderer.cpp

| Unit | Scope | Target file(s) | ~LOC | Status | Slot | Commit |
|---|---|---|---|---|---|---|
| F14 | RDP + memory report + profiling/diagnostics | renderer_rdp.cpp, renderer_diagnostics.cpp | 1,151 | done | slot2 | `efde88b` |
| F15 | Debug toggles + overlays + targeting | renderer_debug.cpp, renderer_overlays.cpp | 1,564 | done | slot3 | `02052bf` |
| F16 | Fire-glow effects + EQE1 loading | renderer_effects.cpp | 476 | done | slot2 | `2060d2c` |
| F17 | loadZoneSequential (phase-split) + zone load/unload/env | renderer_zone_load.cpp (possibly 2 files) | ~3,330 | | | |
| F18 | Zone mesh/portal draw + lazy load + progressive/PVS | renderer_zone_draw.cpp, renderer_zone_stream.cpp | 2,148 | done | slot1 | `46fa92d` |
| F19 | Player movement/collision + raycasting | renderer_player_physics.cpp | 1,550 | done | slot2 | `bb180cb` |
| F20 | Simulation bridge | renderer_sim_bridge.cpp | 1,046 | done | slot2 | `699345c` |
| F21 | Entity/inventory/combat façades + GPU upload thread | renderer_entities.cpp, renderer_gpu_upload.cpp | 863 | done | slot3 | `aa158a4` |
| F22 | processFrame + sub-passes | renderer_frame.cpp | ~1,100 | | | |

## Units — other

| Unit | Scope | Target file(s) | ~LOC | Status | Slot | Commit |
|---|---|---|---|---|---|---|
| F23 | simulation_worker compute passes | sim_particles.cpp, sim_boids.cpp, sim_weather.cpp, sim_visibility.cpp, sim_entity_sync.cpp, sim_animation.cpp | 3,882 | done | slot3 | `bb22559` |
| F24 | entity_renderer clean lifts (cast bars, combat anim, spatial grid, equipment, boats) | entity_castbars.cpp, entity_combat_anim.cpp, entity_spatial.cpp, entity_equipment.cpp | 1,128 | done | slot3 | `ef4a78c` |

## Sequencing

- **F01 is the pilot** — biggest win, most mechanical; shakes out the verifier
  and executor template before fan-out.
- Units within the same source file must not run concurrently (overlapping
  edits). Safe parallel sets: one eq.cpp unit + one irrlicht_renderer.cpp unit
  + F23/F24 at a time.
- HIGH-entanglement units (F08–F13, F17–F22) go after the CLEAN ones — by then
  the files are thousands of lines smaller and the process is proven.
