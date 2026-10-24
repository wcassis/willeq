# B1148 (#468) CORPSE-FADE: the corpse model's fade, effect 0x5e, the per-caster Source term

Branch `task/B1148-corpse-fade` (wt/b1148), cut from fable 50af6150. Evidence: `/mnt/projects/fable/review/B1148/`
(`README.md`, `progress.log`, strips, `oracle/c1_fade/`, `scripts/`).

## 0. Result

- **Corpse model fade (code, wired).** At OP_DeleteSpawn a corpse (race not 216/472) goes on the client's
  fade list; its model is untouched to 1300 ms, then `alpha = clamp(1 - (t - 1300) x 0.0005, 0, 1)` (linear,
  0 at 3300 ms), and it is deleted on the first walk past 3499 ms. fable: `EntityRenderer::beginCorpseFade()`
  (from `IrrlichtRenderer::removeEntity`, the EntityDespawned path) + the walk in
  `updateMainThreadEntityState()`; one copy of the schedule in `include/client/graphics/corpse_fade.h`. A live
  spawn's (or a 216/472 corpse's) model still goes at the packet. Before this, fable's own "corpse decay"
  (3.0 s linear from 0, additive blend) never showed: EntityDespawned removed the entity in the same drain.
- **Oracle (lane 5 observer leg C1): the shape matches, the onset does not.** A `#kill`ed TrollF, seen by
  Oraclewatch with nothing in front of it: the corpse is gone between DEL+3.49 and +3.59 (the 3499 ms delete);
  its contrast falls linearly at the code's slope (0.5/s, fit rms 0.040 of a 2.52 plateau) but the fit puts the
  start 0.61 s LATER than DEL+1.3, so at +3.3 (code alpha 0) the model is still ~25 % visible and it is cut,
  not faded out, at 3.5 s. Not explained by the code read here (sec. 3); fable draws the code's curve.
- **Effect 0x5e is not drawn by the oracle.** It is ActorEmittersNew.edd entry 94 "Death_Effect01", whose
  texture field is EMPTY; the DLL fails the load ("D3DXCreateTextureFromFileEx() failed to create texture  for
  particle system." in both lane clients' dbg.txt at the first corpse delete + 1.0 s), disables the definition
  (+4 = 0 at 0x1005f725) and draws it only if a 0x1011 resource named "" exists. Leg C1's frames at
  DEL+0.9..+2.8 show no sprite at the hands, chest or feet. Not implemented.
- **Per-caster Source term (wired, oracle-confirmed).** `0x47ddd4(spell+0x186)` = the caster actor's track for
  the CastingAnim id; eqgame's id -> code table (0x8ffbc8 + id x 20) names 42/43/44 = T04/T05/T06. fable reads
  the length off the caster's skeleton (`CastAnimationTrackMs`), falls back to B1145's 4.35 s when it cannot.
  Dark Elf female (ELF donor) T04..T06 = 4100 ms, the male (ELM donor) T06 = 1500. Leg C1 cast 93 twice from
  the same character: as the Dark Elf female the Source ends Act+4.764, after `#set gender 0` (Dark Elf male)
  Act+2.216: 2.55 s apart, fable's tracks say 2.60.
- **Tour gate (B1145's open item) now met**: dying-target 93 landing end +4.967 / +4.933 / +4.900 (seeds 1-3)
  against lane +4.938 (B1145: +5.167..+5.200). The cause is the Source term: the harness caster is char 67's
  Dark Elf female, 4.10 s instead of 4.35.

## 1. Prior work found

- `refactor/B1145/README.md` sec. 5a (the trace this card starts from) and `SpellVisualFX::onSpawnDeleted`
  (the 3.5 s emitter removal; untouched here).
- `refactor/B1125` (DeleteSpawn(0) ignored), `refactor/B1133` (every real-id corpse delete honoured; server owns
  corpse lifetime). Both rules hold: `beginCorpseFade` runs only on the EntityDespawned a real-id delete makes.
- fable already had a corpse fade: `startCorpseDecay` (CorpseDecayStarted, bridge_component.cpp) with
  `EntityVisual::FADE_DURATION = 3.0f`, linear from 0, `EMT_TRANSPARENT_ADD_COLOR` + darkened colours, in
  `entity_updates.cpp` and a second copy in the zero-caller `updateInterpolation()` (B494). It was unreachable:
  the bridge pushes CorpseDecayStarted and EntityDespawned together and the latter removed the entity at once
  (B1133's stills show the corpse gone at +0.5 s in fable). Both copies replaced by the client's schedule.
- `git log` of `renderer_entities.cpp`, `spell_visual_fx.cpp`, entity TUs: B1145 b42402e9/884795d9, B1133
  22425839, B1125 7a9834c8; no other fade/removal-animation work.

## 2. The code (eqgame.exe / EQGraphicsDX9.dll, `review/B1141/ghidra/out/`)

| address | what |
|---|---|
| 0x401127 | the per-corpse walk: t <= 1000 ms nothing; t > 1300 alpha = 0x63f4d0 (1.0) - (t - 1300) x 0x63f4d4 (0.0005), clamped to [0x63f4cc (0.0), 1.0], to actor vtbl +0x1b0; one-shot effect 0x5e via vtbl +0x1a4 at RHAND+LHAND (first walk past 1000), CHEST else DEFAULT (1150), RFOOT+LFOOT (1300) |
| 0x4012bb / 0x401110 | deletes (0x439a46) once `last walk - start > 0xdab` |
| DLL 0x1003d880 (actor vtbl +0x1b0) | stores the alpha at actor +0x48 and passes it to the model instance (+0x64 -> +0x9c, vtbl +0x94); the render use of it is not traced |
| DLL 0x100375d0 (vtbl +0x1a4) | particle manager +0x1c (0x1005e270) on its list at +0x80 = ActorEmittersNew.edd (load order 0x1005d4b0: SpellsNew +0x14, EnvironmentEmittersNew +0x5c, ActorEmittersNew +0x80); arg 1 = ground-projected position (ray to -1000), no actor (survives the corpse) |
| DLL 0x1005f400 | emitter texture: tries SpellEffects\, EnvEmitterEffects\, ActorEffects\ + the name; on failure +4 = 0, logs, then resource 0x1011 by name; not found -> never drawn |
| eqgame 0x47ddd4 | actor table (actor +0x1a8 -> +300 -> +0x2c)[CastingAnim] -> track -> length (ftol) |
| eqgame 0x44e7ec.. | the id -> code strings at 0x8ffbc8 + id x 20 (`review/B1148/anim_code_table.tsv`, `scripts/anim_code_table.py`) |

ActorEmittersNew.edd entry 94 (`/mnt/projects/EverQuestP1999`, = the oracle client's): name "Death_Effect01",
texture "", +0x78 -1 (1 s fixed), L 1.5, burst 100, 35/s. Nine entries have no texture (70, 77-81, 94, 95, 153).

## 3. Lane leg C1 (lane 5, observer rig; `review/B1148/plans/c1_fade.tsv`, `oracle/c1_fade/`)

DRU (class 6, the helper switched and restored), caster char 67, observer char 103. A: `#spawn TrollF`, caster
`#goto` away, `#kill`. The server sent OP_Death (victim 261, killer 261) and OP_DeleteSpawn(261) 32 ms later
(no 1.03 s corpse decay for a `#kill`). B: 93 on a second TrollF behind the camera, as Dark Elf female, then
after `#set gender 0` (illusion; `#set race 0` after). Events: `c1_events.tsv` (`scripts/c1_events.py`, the
observer's stream, video time).

- Corpse: `corpse_curve_oracle.tsv` (`scripts/corpse_curve.py`, box 740,515,930,598), strip
  `strip_oracle_corpse_fade.png`. The troll stands to DEL+0.5 and is still falling at +1.6 in the video (the
  death animation's own timing). Contrast peaks +1.29 (2.74), falls linearly: 0.601 at +3.39 (24 % of the 2.52 fit plateau),
  0.441 at +3.49, 0.197 (residual) from +3.59, 0 from +4.29. Fit (fixed slope 0.5/s): onset shifted +0.61 s; free slope: 0.45/s, +0.43.
  The illusion packet's model swap shows +0.37 s after its packet in the same video, so some of the shift may
  be the client's/capture's latency, but the delete at 3.5 is not shifted; the alpha's render path (DLL
  +0x94) is the open item.
- fable (harness, `b1_sep_4499_die`: a /disc with no landing, target dead at Act, despawn +1.027): contrast 1.000
  to +1.30, 0.724 at +2.3, 0.000 from +3.30, removed at +3.50 (`corpse_curve_harness.tsv`,
  `strip_fable_corpse_fade.png`). Its curve is convex in alpha (the model draws without depth write, so
  overlapping parts stack).
- 0x5e: `strip_oracle_effect_0x5e_window.png` (DEL+0.9..+2.0, 0.1 s): no sprite.
- Source: `scripts/caster_box_series.py` (B1145's mask), box 838,279,1028,660: DAF end Act+4.764 (t50 +4.564),
  DAM end Act+2.216 (t50 +1.816). `c1_caster_models.png` shows the two models.
- Lane left as found (`lane_before.txt`, `lane_after.txt`): lock FREE, no containers, no clients;
  db-before == db-after; char 67 and 103 censuses identical.

## 4. Harness (`review/B1148/runs/`, `--camera b1118`)

| run | result | oracle |
|---|---|---|
| 93 tour, die at Act, despawn +1.027, seeds 1-3 | landing end +4.967 / +4.933 / +4.900 | lane +4.938 (B1145: +5.167..+5.200) |
| 93 tour, live | +6.733 | the file's end |
| sep 14 u, caster box Source end: DAF / DAM / HUM / DWM | Act+4.267 / +2.100 / +2.200 / +2.000 | DAF +4.764, DAM +2.216 (leg C1) |

`strip_93_die_vs_oracle.png`: fable vs `review/B1145/oracle_frames/` at the same offsets (the corpse is under
the landing in the oracle tour; leg C1 is the unoccluded view). The remaining Source gap for the female (-0.5 s)
is the slot counts (B1147/B1149), not the term.

## 5. Code

| file | what |
|---|---|
| `include/client/graphics/corpse_fade.h`, `src/client/graphics/corpse_fade.cpp` (new) | the schedule, the race rule, `ApplyCorpseFadeAlpha` (vertex alpha + blended material twins) |
| `entity_lifecycle.cpp`, `entity_updates.cpp`, `entity_renderer.h` | `beginCorpseFade`, the walk, name hidden; `FADE_DURATION` gone |
| `entity_interpolation.cpp` | the zero-caller copy of the old fade removed |
| `entity_mesh_build.cpp` | a new spawn of a fading id ends that fade (fable keys actor and spawn by one id) |
| `overlays_component.cpp` | a fading corpse cannot be click-targeted |
| `renderer_entities.cpp`, `irrlicht_renderer.cpp` | removeEntity -> beginCorpseFade; fade materials; the cast-track callback |
| `zone_shader.{h,cpp}` | three blended twins of the entity materials |
| `spell_fx_timing.h` | `SpellCastAnimationCode`, `SpellFxCallerDurationMs(..., castAnimMs)` |
| `spell_visual_fx.{h,cpp}`, `spell_cast_fx.cpp`, `event_payloads_ui.h`, `spell_packet_handlers.cpp` | CastingAnim (field 120) carried on SpellCastVisualStarted; `createCastGlow(..., castAnim)`; `castAnimationMs` |
| `spell_fx_bone_anchor.h` | `CastAnimationTrackMs` (renderer + harness) |
| `tools/model_viewer/model_viewer_fx_harness.cpp` | despawned corpse fades on the same schedule; cast-track callback |
| `tools/b1022_anim_probe.cpp` | also prints t04..t06 |

Not changed: `onSpawnDeleted` and every particle population/size/alpha path (B1149's); held items keep full
alpha while the body fades (their meshes are shared; the client's +0x1b0 reach into child actors not traced);
a static (non-skinned) corpse mesh stays opaque until removed; the pool rule's held Source
(`spell_effects_config.cpp`) still uses 4.35.

## 6. Tests

`tests/test_corpse_fade.cpp` (new): curve, delete threshold, race rule, id table, term arithmetic; EDT_NULL:
a real corpse through `beginCorpseFade` + the walk (untouched at 1.25 s, alpha 128 at 2.3 s, present at 3.45,
gone at 3.55), live spawn not listed, id reuse, Dark Elf female T04/T05/T06 within 0.3 s of B1145's per-anim
medians (4.077/4.349/4.287), per-race tracks differ; `WILLEQ_CAST_TRACK_TSV` writes `cast_tracks.tsv`.
`test_spell_slot_rules.cpp`: `TheSourceRunsTheCastersOwnCastAnimationTrack`. `test_spell_impact_visual.cpp`:
the source-scan string carries `d.castAnim`. Whole tree with the CI flags (`cmake --build build-ci -j 4 -- -k`):
BUILD_EXIT=0, 0 `error:` lines (`review/B1148/build_ci_whole.log`); `ctest -R "Spell|Bridge|Particle|FxHarness|
Entities|DeleteSpawn|Oracle|SlotRules|CorpseFade|CastAnimationTerm|CorpsePose|..."` 957/957 with
`EQ_CLIENT_PATH=/mnt/projects/EverQuestP1999` (`ctest_targeted.log`); `ctest -L hygiene` 51/51 (`ctest_hygiene.log`).
CLAUDE.md 59,957 bytes.

## FOLLOW-UP

1. The oracle's fade starts ~0.6 s later than the code's register says (sec. 3): trace DLL actor +0x48 / model
   +0x94 to the render state, or measure the client's packet-to-screen latency on a second event, before
   moving fable's onset.
2. Source term for other races: only Dark Elf female/male are measured (leg C1); `cast_tracks.tsv` lists the
   rest from model data.
