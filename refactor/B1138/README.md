# B1138 (#458) FX-HARNESS -- `tools/model_viewer --spell-test` as the fable-side spell FX rig

Branch `task/B1138-fx-harness`, cut from fable 285a53f6. Evidence:
`/mnt/projects/fable/review/B1138/` (outside the checkout).

## Prior work found before writing code

- `git log -- tools/model_viewer*`: the spell-test mode last changed at T381
  (fb98dc7c, vocabulary) and T385 (split). Nothing from B1105/B1131/B1132
  touched it, so it still ran the pre-B1131 path.
- **The viewer's `--spell-test --gles2` did not run at all on 285a53f6**:
  `ParticleManager::init()` logs FATAL and exits (`particle_atlas.bmp not found`)
  because the viewer never gave it a `ConstrainedTextureCache`
  (`review/B1138/tests/baseline_run.log`). Even with one it would have drawn
  nothing: `ParticleManager::updateUnified()` is a no-op since the
  SimulationWorker owns the pool, and `renderUnified()` returns early without the
  worker's render buffer. Its hand anchors were ids 101/102, not
  `SpellVisualFX::rightHandEntityId()`, and it had no head anchor (B1131).
- Frame rigs: the client's `--dump-frames-dir` (T64, stays client-only), the
  surfaceless GLES2 gtests (`test_spell_sprite_sheet_draw.cpp` and peers: one
  quad, no FX chain), and the SimulationWorker seam tests
  (`test_caster_own_landing.cpp`, `test_spell_landing_volume.cpp`, no pixels).
  No prior scripted FX renderer.
- Formats read: `spells_us.txt` field 13 via `SpellData::cast_time_ms`
  (`willeq.db`), `spells.eff` via `SpellsEffReader`, `config/spell_effects.json`,
  `constrained_presets.json` `spellParticlePool`, `willeq.eqe1` +
  `data/eqe1_texture_index.json` (T207 resolver, the
  `tools/hwcaps/zone_bench_loader.cpp` shape), the lane capture layout of
  `review/B1132/gate/fable_B` (x11grab mkv + pcap) and the B1118/B1119/B1120/B1122
  measurement scripts in `review/B1132/scripts/`.
- The installed pack `/mnt/projects/EverQuest_ETC1/willeq.eqe1` was NOT the
  pinned one during this unit (sha 1cdd31..., mtime 2026-10-23 13:03, written by
  another session; pin 8199a8...). The verification used a private pack built
  with `scripts/build_willeq_pack.sh --output <scratch>` (sha 8199a8..., equal to
  the pin and to the lane leg's `pack.sha256`). With the stale pack the 93
  landing drew blue/green star sprites.

## What changed

1. **One FX body for the client and the viewer**:
   `src/client/bridge/spell_cast_fx.{h,cpp}` holds the FX half of the three
   SpellCastVisual* arms, moved verbatim out of `irrlicht_bridge.cpp`
   (`noteSpellVocabulary` + `createCastGlow`; `releaseCastGlow` / the B1105
   action-only glow + `createLanding` on the cast-animation packet;
   `removeCastGlow`). `CastGlowRoute` takes `now` as a defaulted parameter so the
   harness drives it on its scripted clock. The viewer publishes the client's
   payloads: BeginCast, then at BeginCast + cast_ms the effect_flag 0 packet and
   the effect_flag 4 repeat 4 ms later (the pair the lane pcap shows for 93).
   The hand/head anchor resolution is shared too
   (`include/client/graphics/spell_fx_bone_anchor.h`, used by
   `renderer_entities.cpp`'s particle callbacks and the viewer).
2. **Scripted mode** (`tools/model_viewer/model_viewer_fx_harness.{h,cpp}`):
   `--plan <tsv>` (t_begin_s, spell_id, self|other, distance_units[, cast_ms]),
   `--frames-dir`, `--fps`, `--seconds`, `--headless`; fixed dt = 1/fps, the
   device timer stopped and set to the scripted time (so `drawAll()`'s node
   animation is deterministic), the particle RNG seeded (`--seed`, new
   `ParticleInput::rngSeed`, 0 = random_device as before). Writes
   `frame_%05d.png` (`createScreenShot`, the F12 path), `timing.tsv` (a row per
   frame + BeginCast/Action rows) and `scene.tsv` (each entity's screen box).
   Two runs of the same plan are byte-identical (120/120 frames md5-equal).
   The particle half is the renderer's: texture cache + willeq.eqe1 + the T207
   resolver for model textures, `applyPool(spellParticlePool)`, and each frame
   the manager's commands through `SimulationWorker::computeOnce()` with last
   frame's entity positions (renderer_sim_bridge.cpp's protocol, synchronous).
3. **`--camera b1118`**: 1280x800, caster = char 67 Testcaster's model (race 6
   dark elf, gender 1, server size 6: fable.log "Updating appearance ... race 6
   gender 1", "buildEntityMesh[Testcaster] ... (6.00/6)"), target = the `#spawn`
   human male (race 1 gender 0), both face-on to a camera 12 degrees down
   (`az 180 pitch 12`, the tour's `/camera set`), the client's 62.8 degree FOV.
   The distance and look-at height are fitted on the RENDERED silhouette: feet
   row 605, 235 px head top to feet (converges to dist 28.36, look 5.35 above the
   pose-box feet). A solid clear-colour ground plane at the feet hides particles
   below ground (two invisible variants did not work in this driver; comment in
   `addGroundPlane()`). `--backdrop <png>`, `--caster-px`, `--no-ground`,
   `--caster/--target race:gender[:size]`, `--eqe1`, `--eqe1-dir` exist for the
   sensitivity runs below.
4. **Deleted** (tool-only after step 1; census over src/ include/ tests/ tools/
   benchmarks/ docker/): `SpellVisualFX::createImpactAtPosition`,
   `createBuffAura`, `createRainEffect`, `createGroundCircle`, their node
   builders (`createAuraNode`, `createRainNode`, `createGroundCircleNode`,
   `updateAura`), `hasBuffAura` (its only caller went), the presets
   `SpellPresets::BuffAura/SpellRain/GroundCircle`, the built-in and shipped
   styles `buffAura`/`spellRain`/`groundCircle`, and
   `ParticleManager::createSpellEffectAtPosition` (callers gone).
   **Kept**: `createSpellComplete` + `createImpact` (createLanding's scene-node
   path and the projectile's arrival reach them; tests use them),
   `createProjectile` (B1128). `ZeroCallers` now lists `createProjectile`
   (board B1128) and `getSpellColorForSpell` (observer) as tests-only.
5. **ctest** `FxHarness.AnOtherTargetLandingLightsTheTargetAndNotTheCaster`
   (`tests/test_fx_harness.cpp`, label `graphics`, graphics trees only): 201
   Flash of Light (record 22, Source held 0.31 s) on a target 14 units away, 4 s
   at 30 fps; over Act+1..2 s the target box peaks 14,000-18,500 lit px, the
   caster box 0-143 (idle-animation diffs); asserts >= 3000 / <= 400. Mutation
   check: with `createLanding(d.casterId, d.casterId, ...)` it FAILS (target
   1006, caster 7816; `review/B1138/tests/mutation_landing_on_caster.log`).
6. Docs: `docs/module_map.md` row, CLAUDE.md one sentence (net size kept under
   60,000 bytes by tightening the CAP_NET_ADMIN paragraph),
   `refactor/EXECUTOR_PROTOCOL.md` (fable FX gates in the harness, lane 5 for
   oracle legs), BOARD B161 head.

## Verification: B1123 tour spell 93, harness vs lane 5 (`review/B1138/side_by_side_93.tsv`)

Plan `review/B1138/plans/tour_93.tsv` (BeginCast 1.5 s, other target at
distance 0 = the leg-B `#spawn` standing on the caster), 13.2 s, measured by
`review/B1138/scripts/harness_measure.py` = b1119_measure + b1120_measure +
b1123_measure with the cast times from `timing.tsv` instead of the pcap.

| quantity | lane 5 (B1132) | harness gate | harness + backdrop | + 60 Hz step + 228 px |
|---|---|---|---|---|
| land_cov (caster h^2) | 2.938 | 3.212 (+9.3 %) | 3.211 (+9.3 %) | 3.142 (+6.9 %) |
| end (s after OP_Action) | +4.938 | +5.000 (+0.062) | +5.000 | +5.000 |
| ROI p90 landing | 0.077 | 0.073 (-5.2 %) | 0.073 | 0.077 (0.0 %) |

**Not met: landing coverage** (+6.9 .. +9.3 % against +-3 %); ROI p90 meets
only in the 60 Hz / 228 px run; end meets in every run. What each scene factor
was MEASURED to move (none explains the coverage gap on its own):
- background (the leg's own tail frame behind the scene): -0.03 %;
- the ground plane: -0.1 % (3.216 -> 3.212);
- simulation step 60 Hz (the lane client's 60 fps, sampled at 30 like x11grab)
  instead of 30 Hz: coverage +2.6 %, ROI p90 +11 % (0.073 -> 0.081);
- caster scale: Testcaster reads ~228 px head top to feet in that leg's tail
  frame (rows 377..605), not 235: coverage -4 %, ROI -4 %.
Still different and not modelled: the target NPC dies and falls on lane 5
(death animation, corpse) while the harness target stands; zone lighting/fog
(qeynos2 fog starts at 250 units, so none at 28); the harness renders the
worker's output in the same frame (the client one frame later); the caster's
robe. The Source ROI (not in the acceptance) differs far more (0.105 vs 0.062)
and is unexplained here. Needed to close it: a lane leg recorded for the
harness's purpose (a caster-only frame at the tour pose for the pixel scale,
and the same cast with `--fps 60`-equivalent timing data), or a second fable
reference run on the harness itself replacing the lane gate as the baseline
for regression (the harness is deterministic, the lane is not).

## Builds and tests (`review/B1138/tests/`)

- Whole tree, CI flags (`-DCMAKE_BUILD_TYPE=Release -DEQT_BUILD_TESTS=ON -DEQT_GRAPHICS=ON
  -DEQT_GLES2=ON -DEQT_GLES2_IRRLICHT_DIR=...`), `cmake --build build-ci -j 4 -- -k`: rc 0
  (`build_ci_whole.log`).
- Headless (`-DEQT_GRAPHICS=OFF`, tests on): configure rc 0, whole build rc 0
  (`configure_headless.log`, `build_headless.log`); its Spell/Landing/ConfigKey/DataPath
  ctests 411/411 (`ctest_headless_targeted.log`).
- `ctest -L hygiene`: 50/50 (`ctest_hygiene.log`).
- Graphics tree, Spell/Landing/Bridge/Renderer/Sim/Particle/Reachability/ConfigKey/DataPath/golden
  subset incl. `FxHarness`: 958/958 (`ctest_targeted.log`). The full suite was not run here (CI runs it).
