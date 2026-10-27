# model_viewer `--spell-test`: the fable-side spell FX harness

`model_viewer --spell-test --plan <tsv>` drives the client's own cast sequence
(`src/client/bridge/spell_cast_fx.cpp`) through the production
SpellVisualFX -> ParticleManager -> SimulationWorker chain, surfaceless, on a
scripted clock, and writes `frame_%05d.png`, `timing.tsv`, `scene.tsv`,
`slots.tsv` and, since B1157 (#476), `emitter_origin.tsv` (the open emitters'
world origin + screen position per rendered frame, through the same pinhole as
`scene.tsv`'s boxes).
Options and code: `tools/model_viewer/model_viewer_fx_harness.h`. Reports:
`refactor/B1138/README.md` (the harness), `refactor/B1139/README.md` (its
calibration against lane 5).

```
model_viewer --spell-test --client /mnt/projects/EverQuestP1999 \
    --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1 --plan plan.tsv \
    --frames-dir out/ --fps 30 --seconds 13.2 --headless --camera b1118
```

Plan rows: `t_begin_s  spell_id  self|other  distance_units  [cast_ms
[caster_level [begincast|action]]]` (`-` = the default). B1144 (#464):
`caster_level` is the caster's spellsnew.eff slot-gate level (default
`--caster-level`, 60: the `#level 60` of every oracle capture the rows come
from); `action` publishes OP_Action alone at `t_begin_s + cast_ms`, the `/disc`
path (no OP_BeginCast, so no Source). `slots.tsv` lists the slots each packet
draws (`SpellVisualFX::stageSlots`).
Measure a run with `review/B1139/scripts/harness_measure.py <frames> <out>` (the
lane-5 measurement scripts of `review/B1132/scripts/`, unchanged; cast times
from `timing.tsv`).

## The b1118 preset is the lane-5 tour rig

`--camera b1118` reproduces the B1118/B1123 tour as lane 5 records it. Every
default below is a measured fact of that rig (B1132 tour leg B: the capture,
pcap and fable.log in `review/B1132/gate/fable_B/`), and each one is a
**rig** correction, not a fable change:

| default | source | override |
|---|---|---|
| 1280x800, camera az 180 pitch 12, 62.8 deg FOV, caster feet row 605 | B1118 tour, `/camera set` | -- |
| caster 228 px head top to feet | leg B tail frame rows 377..605 (B1138) | `--caster-px` |
| caster race 6 gender 1, face 3, TextureProfile {0,10,1,23,1,1,1,0,72}: the robe (chest 10) and IT72, the orb in her left hand | char 67 Testcaster; OP_ZoneEntry S->C Spawn_Struct offsets 6/197/339 | `--caster`, `--caster-appearance f,t:e0..e8` |
| target race 1 gender 0 (the `#spawn`) | leg chat | `--target` |
| caster p01 when idle, and during a cast the spell's own CastingAnim track (B1157) through the client's `spell_cast_animation.h`; target idle o01 | the client: the local player stands in p01 (`renderer_player_physics.cpp`), an idle NPC in o01 (`entity_updates.cpp`); pre-B1157 the caster stayed in p01 (its t04 castbar literal was timing-only, see `refactor/B1157/README.md`) | -- |
| simulation at 60 Hz, every 2nd step written at `--fps 30` | the lane client runs 60 fps ("FPS: 60" on its frames), x11grab keeps 30 | `--sim-hz` |
| no UI over the FX field (B1142) | the lane rig starts the client with `[Tips] ShowTOTD=0` (`review/B1142/scripts/fable_leg_remote.sh`); before B1142 every lane-5 leg had fable's Tip of the Day open at 920,243..1280,543, OVER the particles | `--ui-occluder x0,y0,x1,y1` (repeatable; `920,243,1280,543` reproduces a pre-B1142 capture), `--no-ui-occluder` |

Plan-specific lane facts are options, not defaults: `--target-death <s>` (the
target plays its death animation `s` seconds after OP_Action: leg B's pcap has
OP_Death 0.000 s after it, so `--target-death 0`), `--target-despawn <s>` (its
OP_DeleteSpawn, 1.027 s, which a B1133 client obeys), `--backdrop <png>` (the
leg's own tail frame above the horizon), `--worker-latency 1` (draw the worker
output of the previous step, as the client does).

## The b1130 / b1130-self presets are B1130's fixed-geometry stills (B1155)

`--camera b1130` (a caster + a stunned idle target to her right) and
`--camera b1130-self` (the caster alone) reproduce the framing B1130 used for
every record in its 40-cast survey, so a sweep on them bands against
`review/B1130/record_table.tsv` instead of against the tour. Both are the
B1118 opening pose (same FOV, pitch 12, the char-67 caster); only the fitted
numbers and the target differ, all from `review/B1130/README.md` "Geometry":

| default | source | override |
|---|---|---|
| 1280x800, caster 209 px, feet row 598 at column 640 | the settle stills `oracle/cal_r35/15_time_settle.png` (the B1118 opening pose, `#stun 120000`, camera F9 x3) | `--caster-px` |
| caster = the b1118 char-67 appearance | the same char 67 Testcaster | `--caster`, `--caster-appearance` |
| b1130: target race 1 (human), gender 0 (male), size 6 -- eqemu's `#spawn [name][race][level][texture][health][gender]...`: `#spawn TrollF 1 1 0 30000` is race=1, level=1, texture=0, hp=30000 with the GENDER arg absent -> `Mob::GetDefaultGender(1)` = MALE; "TrollF" is only a label | the same command; caster `#goto -67.6,434.4` puts it ~1.4 caster heights to her right | `--target` |
| b1130: target 325 px, feet (940,652) | the same settle still; the 325 is that human's NEARER depth (~1.55x closer to the camera than the caster), not a race scale -- a consistency check, not an input (the FABLE rest-pose fit renders it as a 50 px strip; `refactor/B1155/README.md`) | -- |

The camera is fitted to the caster exactly as b1118's is (`fitFaceOnCamera`,
209 px / row 598); the target's world offset (x, z) is then SOLVED from its
two feet pixels (Newton on the rendered silhouette, `fitFixedNpcTarget`), and
the solve is printed like the b1118 block. A preset that does not solve within
1 px exits 2. Pinned by `tests/test_fx_harness.cpp`
(`B1130PresetSolvesTheFixedNpcGeometry`, `B1130SelfPresetSolvesTheCasterAlone`).
The target idles (no death unless `--target-death`). Measure a run with
`refactor/B1155/scripts/harness_measure_b1130.py <frames> <out> <fixed_npc|fixed_self>`
and band the sweep with `refactor/B1155/scripts/band_table.py`
(`refactor/B1155/README.md`).

## What each factor moves (spell 93, leg B, pool 8192; `refactor/B1139/README.md`)

Landing coverage (lane 5: 2.938 on B1132's capture, 2.929 on B1127's, which
draws this tree's art):

| change | land_cov | vs lane B1132 |
|---|---|---|
| B1138 preset (235 px, 30 Hz, no occluder, no appearance, p01 idles) | 3.250 | +10.6 % |
| + 60 Hz step, 228 px, backdrop (B1138's best) | 3.131 | +6.6 % |
| + the Tip of the Day occluder | 2.986 | +1.6 % |
| + target death at OP_Action | 2.994 | +1.9 % |
| + Testcaster's robe and orb | 3.005 | +2.3 % |
| defaults (+ p01/o01 idles, no backdrop), seed 1 | 3.006 | +2.3 % |
| defaults, seeds 1-5 | 2.900 .. 3.006, mean 2.950 | -1.3 .. +2.3 %, mean +0.4 % |

Measured to move it by less than 0.5 %: the target's death (+0.3 %), the
worker's one-frame latency (+0.2 %), the B1133 despawn at +1.027 s (+0.1 %),
the backdrop (+0.03 %), the lane's libx264 crf 18 yuv420p encode instead of
ffv1 (-0.9 .. +1.3 %, `FX_CODEC=lane`). Background and lighting are bounded by
the lane tail baseline: 2.2 % of the peak lit count lies on pixels whose red
channel exceeds 175 (where an additive sprite's rise can stay under 80).

## What the harness may gate

- **Fable regressions, harness against harness** (same plan, `--fx-seed`, preset):
  landing coverage, landing end, landing ROI p90, landing peak time. Since
  B1169 the frames are deterministic bit for bit ("Determinism" below), so
  "before/after identical" is a frame diff, not a judgement.
- **Parity with lane 5** (B1142's unoccluded rig, `refactor/B1142/README.md`):
  landing coverage -- seeds 1-5 with `--target-death 0 --target-despawn 1.027`
  read 3.111 .. 3.191 (mean 3.151) against four lane captures of 93 at
  2.990 .. 3.140 (mean 3.079, the lane's own spread 5 %): mean +2.3 %, each seed
  -0.9 .. +6.7 % of each capture; with the old window drawn back in
  (`--ui-occluder 920,243,1280,543`) seed 1 reads 2.963 against the same
  code's window-open capture 2.942 (+0.7 %). Landing end: harness
  +4.967 .. +5.033 s against the lane's +4.898 .. +5.047 s.
  Seed spread is part of any parity claim: landing ROI p90 varies
  0.065 .. 0.073 over seeds 1-5 (lane 0.069 .. 0.072), so one seed does not prove it.
- **Lane-only**: Source ROI p90 (harness 0.069 .. 0.074 against 0.064 .. 0.066
  on the lane legs without a target outline; B1142 traced most of B1139's
  "2x denser early hand spray" to the lane's target outline sitting in the
  pre-cast baseline), and colour as fx_measure reads it (its
  frame-mean hue mixes in the scene: the harness's dark clear colour reads hue
  20, the lane's grass and sky 35-37).
- **Colour, scene-free** (B1146): `review/B1146/scripts/added_hue.py` reads the
  ADDED colour (frame minus the pre-cast median) of the risen pixels, the same
  arithmetic on an oracle capture, a fable lane leg and a harness frames dir.
  93's landing reads hue 25 on both the harness and lane leg B; the discipline
  records 326/329/328 read the oracle's hue within 20 deg
  (`refactor/B1146/README.md`).
- **Slot rules** (B1144, `tests/test_fx_harness.cpp`): a `/disc` plan
  (4499, `action`) draws zero lit px against a no-cast run, and a level-1
  93 draws only record 80's level-0 slots (`slots.tsv`) and fewer Source
  pixels than a level-60 one.
- **Caster animation** (B1157, #476): the caster plays the spell's own
  CastingAnim track -- the same `spell_cast_animation.h` function the client
  calls on OP_BeginCast (no harness-side mapping) -- held for the channel and
  ended at OP_Action (a `/disc` plan starts no track and ends nothing, like
  the client). The `--target` model (b1130's stunned NPC) stays idle, as the
  oracle's does. `emitter_origin.tsv` is the hand-anchor strip's source
  (`refactor/B1157/scripts/hand_strip.py`). The re-gate against the pre-B1157
  idle caster: `refactor/B1157/README.md`.
- Oracle legs (the official client) are lane 5 only.

## Determinism (B1169, #488)

Two runs of one plan with the same binary, options and render path write
byte-identical frames on the measured paths (ThinkPad NVIDIA: GBM 16-bit depth
and surfaceless 24-bit depth; `refactor/B1169/README.md`: 3 consecutive tour-7
runs, 0 differing frames on every spell). Not measured, not claimed: llvmpipe. What holds it:

| source | rule | where |
|---|---|---|
| particle RNG | `--fx-seed N` (alias `--seed`), default **1**, fixed in every `--spell-test` run; `0` draws from `std::random_device` (the client's path) and is NOT reproducible -- the run log's `determinism (B1169)` line says so | `harnessOpts.seed` -> `SimulationInput::particleInput.rngSeed` (`stepSpellParticles`) |
| time | the scripted clock: step `1/--sim-hz` s, the device timer stopped and set per step; no wall clock reaches the scene, the FX or the worker | `runScriptedHarness` |
| worker | `SimulationWorker::computeOnce` on the main thread, no worker thread | `stepSpellParticles` |
| solid draw order | Irrlicht sorts solid nodes by their material-0 texture POINTER, which ASLR moves; the caster, her held item and a distance-0 target have depth-tied pixels, so the order decided them (the pre-B1169 differences, 20-125 px a frame on 93). `HarnessSolidOrderNode` draws ground, target, held items, caster in that fixed order -- a harness convention (the majority order, 13 of 20 pre-fix runs), NOT a client rule: the production client uses the same Irrlicht texture-pointer sort (`CSceneManager.cpp:1485`, patched Irrlicht) and no oracle draw order is established; harness-only, compiled into the `model_viewer` target only (`CMakeLists.txt:1313`); the run log prints `solid draw order (fixed, B1169)` | `fixHarnessSolidOrder` |

How a card proves "identical": render the plan twice (before/after, or the
same binary twice) with the same options, then
`python3 -P scripts/tools/fx_frame_identity.py <framesA> <framesB>` -- it must
print `differing 0`; any other count names the first differing frame, its
pixel count and bounding box. A difference between two runs of ONE binary is
a determinism defect (`WILLEQ_FX_MESH_HASH=1` logs the posed-mesh hashes and
the scene tree per run to split CPU geometry from draw order). The render path
is part of the options: GBM (16-bit depth) and surfaceless
(`WILLEQ_DRM_DEVICE=/dev/dri/no-such-card`, 24-bit depth, the B1150
`run_scene.sh` path) are different renders, so compare like with like.
Particle tests seed the same way (`tests/particle_test_seed.h`, guarded by
`SpellParticleSeedWiring`).
