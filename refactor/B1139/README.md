# B1139 (#459) FX-HARNESS-CALIBRATE -- spell 93 harness vs lane 5 on pack 1cdd31e7

Branch `task/B1139-fx-harness-calibrate`, cut from fable cc3fc7cf. Evidence:
`/mnt/projects/fable/review/B1139/` (README, `progress.log`, `runs/`, `scripts/`, `verify/`, `tests/`).
No lane was used: every lane number is from captures already in `review/` (B1132 leg B; B1127's final leg B).

## Prior work found

- `git log -- tools/model_viewer src/client/bridge/spell_cast_fx.cpp src/client/graphics/spell_visual_fx.cpp
  include/client/graphics/environment/particle_manager.h`: B1138 882c31f4 (the harness), B1137 8435c968 (field-145
  rows; 93 unchanged), B1127 b109b3ef (field-145 ART: 93/38 draw record 80's sprites), B1132 b2ad407c, B1131 d32ddad4.
- B1138's side-by-side (`review/B1138/side_by_side_93.tsv`) predates the rebase: it ran pack 8199a8 (pre-B1127 art).
- **A second lane truth already exists on this tree's art**: `review/B1127/gate_final/` ran the same tour on lane 5
  at 35e949f0 with pack 1cdd31e7 (93: land_cov 2.929, end +4.914, roi_l 0.072, roi_s 0.062). Every table below
  carries it beside B1132's (b2ad407c, pack 8199a8), which the card names.
- Zero-caller census: `scripts/refactor/check_zero_callers.py` listed `SpellVisualFX::removeBuffAura` /
  `removeAllForEntity` as known zero-caller (B494 group, owned by B195). `ParticleCommand::CreateSpellEffectAtPos`'s
  producer went in B1138 (comment in `particle_manager.h`).
- No harness README existed; `refactor/EXECUTOR_PROTOCOL.md` had B1138's one paragraph.

## 1. Spell 93 on this tree (`review/B1139/side_by_side_93.tsv`)

Installed pack sha 1cdd31e7... == `data/willeq.eqe1.sha256` (verified, not rebuilt). Pool 8192 (desktop key).

| quantity | lane B1132 | lane B1127 | harness gate (B1138 preset) | s60_228 (B1138's best) | B1139 defaults, seed 1 | defaults, seeds 1-5 |
|---|---|---|---|---|---|---|
| land_cov | 2.938 | 2.929 | 3.250 (+10.6 %) | 3.131 (+6.6 %) | 3.006 (+2.3 %) | 2.900..3.006, mean 2.950 (+0.4 % / +0.7 %) |
| end | +4.938 | +4.914 | +5.000 (+0.062) | +5.067 (+0.129) | +5.000 (+0.062) | +5.000..+5.033 |
| roi_l | 0.077 | 0.072 | 0.071 | 0.071 | 0.071 (-1.4 % vs B1127) | 0.064..0.071, mean 0.067 |
| land_t | +2.07 | +2.05 | +1.90 | +1.97 | +1.97 | +1.93..+1.97 |
| roi_s | 0.062 | 0.062 | 0.088 (+42 %) | 0.085 (+37 %) | 0.072 (+16 %) | 0.071..0.082, mean 0.075 (+21 %) |

"gate" is `--camera b1118` as B1138 shipped it, run on this tree before any change of this unit.

## 2. Landing coverage: the gap is the Tip of the Day window

**The lane client draws its Tip of the Day window (full frame x 920.., rows 243..542) OVER the particles**; the
harness had nothing there. Lane leg B has no lit pixel inside that rectangle in any frame of the cast (both
captures; `review/B1139/verify/landing_peak_lane_gate_defaults_with_tip_window.png`), while the harness's peak
frame had 5.4 % of its lit pixels there (`scripts/landcov_masked.py`: 3.250 -> 3.074 on the gate, lane 2.938 ->
2.935). That is a rig fact, so it is a harness default now (an opaque rectangle drawn after the particles).

Each remaining factor, measured on its own run (seed 1, pool 8192; `review/B1139/runs/<run>/`):

| run | change | land_cov | step | vs lane B1132 |
|---|---|---|---|---|
| gate | B1138 preset | 3.250 | | +10.6 % |
| gate_occ | + Tip of the Day occluder | 3.074 | -5.4 % | +4.6 % |
| s60_228 | gate + 60 Hz step (decimated) + 228 px + backdrop | 3.131 | | +6.6 % |
| occ | + occluder | 2.986 | -4.6 % | +1.6 % |
| occ_death | + target death (d05) at OP_Action | 2.994 | +0.3 % | +1.9 % |
| occ_death_lat | + worker output drawn one step late | 3.001 | +0.2 % | +2.1 % |
| occ_death_despawn | + target despawned at Act+1.027 (B1133) | 2.996 | +0.1 % | +2.0 % |
| app_s1 | occ_death + Testcaster's robe + IT72 orb | 3.005 | +0.4 % | +2.3 % |
| def_s1 | + p01 caster / o01 target idles, no backdrop = the new defaults | 3.006 | 0.0 % | +2.3 % |
| def_s1..5 | seeds 1-5 | 2.900..3.006 | | -1.3..+2.3 % |

- **The falling target**: the pcap gives OP_Death at Act+0.000 and OP_DeleteSpawn(259) at Act+1.027
  (`review/B1132/scripts/pcap_ops.py`); the coverage window starts at Act+1.0, so a before-the-fall window does not
  exist and the death is modelled instead (`--target-death 0`, ApplyCorpseDeathPose's witnessed branch: d05 once).
  It moves coverage by +0.3 %. B1132's client kept the corpse (pre-B1133); the despawn variant moves it +0.1 %.
- **One-frame worker latency** (`--worker-latency 1`): +0.2 %. Not a default.
- **The robe and the orb**: Testcaster's Spawn_Struct (OP_ZoneEntry S->C, offset 197) is
  {0,10,1,23,1,1,1,0,72}: chest material 10 and IT72 in her left hand. +0.4 % on coverage.
- **Zone lighting / background**: below the horizon, 2.2 % of the lane's peak lit count lies on tail-baseline
  pixels whose red channel exceeds 175, the only place an additive orange sprite can rise less than on the
  harness clear colour (`review/B1139/lane/bright_background_bound.txt`); the leg's own backdrop above the
  horizon moves coverage by +0.03 %.
- **The lane's encoder** (libx264 ultrafast crf 18 yuv420p, `fable_leg_remote.sh`) instead of ffv1:
  -0.9 .. +1.3 % (`FX_CODEC=lane`, `runs/*/measure_x264/`).

With the defaults, all five seeds lie inside the +-3 % band against both lane captures. **No fable rendering defect
remains in landing coverage**; the gap was rig. Applied to the harness defaults and written into
`tools/model_viewer/README.md`.

Also corrected as rig while measuring (each a harness default now): the client's idles. The local player stands
in **p01** (`renderer_player_physics.cpp`), an idle NPC in **o01** (`entity_updates.cpp`); the viewer played p01
on both, and the lane `#spawn` visibly stands feet apart (`verify/target_pose_lane_precast_vs_harness_o01.png`).
The 60 Hz step is `--sim-hz 60` (default under b1118): the frames equal the decimated 60 fps run except ~50 px
per frame inside the two coincident figures, which z-fight differently between any two runs of a distance-0
plan (`review/B1139/tests/determinism_simhz.txt`; the same count between two identical runs).

## 3. Source ROI p90: partly attributed, residual stays lane-only

Per blob (`scripts/source_roi_sprites.py`, `source_spread.py`, `source_free_sprites.py`;
`review/B1139/source_roi_decomposition.txt`):

- **Sprite size matches.** Free single sprites (away from the hands, BC+1.0..1.45): median diameter 12.6 / 11.9 px
  on the two lane captures, 11.6..12.8 px on seeds 1-5. Not a size defect.
- **Lane's p90 is diluted by non-sprite blobs**: 611-693 of its 1106-1190 blobs are not sprite-coloured
  (median 0.010), 471-474 of them on the colour-cycling target outline the tour clears at BC+0.3, after the
  pre-cast baseline. Sprite-coloured blobs alone read 0.071-0.073 on lane. The B1138 harness had 73-105 such blobs; with
  the client's idles (p01 caster, o01 target), the robe and the orb it has 393-418, and roi_s fell 0.088 -> 0.072.
- **Residual**: sprite-coloured p90 0.082..0.101 on the harness against 0.071-0.073 on lane. The lit sprite area
  agrees once the spray is established (BC+1.0..1.45: harness 42.7k-51.0k px, lane 42.5k-50.3k) but not early
  (BC+0.5..0.85: harness 9.3k-12.8k, lane 4.6k-5.9k): the harness's early spray at the hands is about twice as
  dense, and its overlapping sprites merge into larger blobs. Cross-correlating the warm-pixel series puts the lane
  0.05 s late at best (rms minimum), which does not account for the density. The screen-right (orb) hand emits
  far less on lane (early 493 / 1027 px vs 3.0k-4.7k on the harness, with the orb modelled).
  **Not attributed to a measured cause.** No measured fable-side defect: the harness runs the same Source code
  the lane client ran. Needed to close it: a lane leg of 93 with `--dump-frames-dir` at 60 fps (per-frame Source
  without the 30 fps grab or x264) and one with the target outline off (no `/target` before the cast), so the
  early spray and the outline residue can be separated on lane pixels.

## 4. Dead worker arms deleted (ZeroCallers + grep)

`ParticleCommand::CreateSpellEffectAtPos` (its `sim_particles.cpp` arm, `ParticleCommandData::worldPos`),
`SpellAttach::GROUND_TARGET` (+ `SpellEffectInstance::groundTarget`), `SpellVisualFX::removeBuffAura` and
`removeAllForEntity` (B195) with the two worker commands only they produced
(`SpellVFXCommand::RemoveBuffAura` / `RemoveAllForEntity` and their arms). Census over src/ include/ tests/ tools/
benchmarks/ docker/ scripts/: no caller, no test caller. Their two `check_zero_callers.py` rows removed; BOARD B195
closed, B161 and B494 text updated.

## 5. What the harness may gate

Written into `refactor/EXECUTOR_PROTOCOL.md` and `tools/model_viewer/README.md`:
fable regressions, harness against harness at a fixed plan + seed: landing coverage, end, ROI p90, peak time;
parity with lane 5: landing coverage (closed by item 2) and end (+0.06..+0.12 s); lane-only: Source ROI p90 and
colour. Landing ROI p90 parity carries its seed spread (0.064..0.071 against the lane's 0.072).

## Code

- `tools/model_viewer/`: `--ui-occluder`/`--no-ui-occluder`, `--target-death`, `--target-despawn`,
  `--worker-latency`, `--caster-appearance`, `--sim-hz`; the b1118 preset's new defaults; p01/o01 idles;
  `shutdownHarness()` frees the held items' equipment cache before the device (the orb made the viewer SIGSEGV at
  exit in `ConstrainedTextureCache::destroyTextures`).
- `tests/test_fx_harness.cpp` unchanged; it passes with the new defaults.

## Builds and tests (`review/B1139/tests/`)

- `build-ci` configured `-DCMAKE_BUILD_TYPE=Release -DEQT_BUILD_TESTS=ON -DEQT_GRAPHICS=ON -DEQT_GLES2=ON
  -DEQT_GLES2_IRRLICHT_DIR=$PWD/third_party/irrlicht-gles2-x86`; named `model_viewer` builds while iterating; the
  final whole tree `cmake --build build-ci -j 4 -- -k`: BUILD_EXIT=0, 0 `error:` lines (`build_ci_whole.log`).
- `ctest -L hygiene`: 50/50 (ZeroCallers, DocCitations, FileSizeCeiling among them); CLAUDE.md 59,994 bytes, unchanged.
- `Spell*|Bridge*|Particle*|FxHarness|ConfigKeyReachability`: 406/406 passed (64 skip on absent EQ client files).
- `Sim|Landing|Renderer|golden|DataPath|Reachability|SpellEffect|CasterOwn`: 316/316.

## FOLLOW-UP

- Source ROI p90 residual (sec. 3): needs the two lane legs named there.
- The harness's colour reading (hue 20 vs lane 35-37) is fx_measure's frame mean over the scene, not the sprite;
  B1127's `added_colour.py` is the scene-free measure if a colour gate is wanted.
