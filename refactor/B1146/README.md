# B1146 (#466) FX-WIRE-COLOUR -- the spellsnew.edd colour words reach the particle

Branch `task/B1146-fx-colour`, cut from fable e57e1f85, run in parallel with B1144 (`wt/b1144`). Evidence:
`/mnt/projects/fable/review/B1146/` (README, `progress.log`, `scripts/`, `runs/`, hue tables, strip). No lane used:
every oracle number is from captures already in `review/` (B1134 P4/P5, B1130 r51, B1132 gate leg B).

## Prior work found

- `git log` of the particle files: T441 cd616292 ("a spell's colour is its sprite, not its resist type") added the
  white reset in `SpellPresets::applyStageSpriteNames()` (`colorStart/colorEnd = white, alpha kept`) and the
  Source/Middle routing in `spell_visual_fx.cpp`. **No tint path was removed by T441**: the colour path itself
  (`EmitterConfig::colorStart/colorEnd` -> `UnifiedParticle::color` = mix over the life -> `POINT_SPRITE_FS`
  `texColor * vColor`) was intact; T441 only fed it white. The resist / school colour (`resistColors`,
  `colorForVocabulary()`) is the colour it replaced, and it stays the fallback for spells with no record.
- T441's "no colour field" was a finding about **spells.eff** (field 123; T73 refuted its only candidate). The client
  draws **spellsnew.eff** (B1127), whose EDD entries carry colour words, read from the client's code by B1141
  (`docs/spellsnew_format.md`, `review/B1141/edd_fields_from_code.tsv`).
- B1137 census: 686 of the 1,655 EDD entries a record names are non-white (B1146 recount over the shipped files).

## The client, from its code (EQGraphicsDX9.dll, B1141's Ghidra dump)

- `0x100605b0` (CParticleSystem::Render), the per-particle vertex: `fade = 1; if (age < D90) fade = age/D90;
  if (remaining < D94) fade *= remaining/D94; if (DA4 < fade) fade = DA4;` alpha = `fade * opacity-arg *
  actor-alpha * 255`; RGB = `D4 + (E0 - D4) * age / L` per channel (skipped when start == end);
  colour `A<<24|R<<16|G<<8|B`.
- `0x1005f400` (the particle draw): `SetTextureStageState(0, ALPHAOP, MODULATE)`; COLOROP is left at the device's
  MODULATE (TEXTURE x DIFFUSE): the two SELECTARG1 sites (`0x10074a80`, `0x10088e80`) restore MODULATE after.
  So the vertex colour multiplies the texel in RGB and in alpha. SRCBLEND SRCALPHA; `+0x68 == 1` sets DESTBLEND
  ONE, else INVSRCALPHA.
- The opacity argument is the `SpellParticleOpacity` option, default 1.0 (eqgame, `"Defaults"` read with
  `0x3f800000`); the oracle's eqclient.ini does not set it.

## What changed

- `include/client/spell/spells_new_edd_colour.h` / `src/client/spell/spells_new_edd_colour.cpp` (new):
  `SpellsNewEddColourTable` reads the 416-byte entries directly (+0x68, +0x90, +0x94, +0xA4, +0xD4..+0xE8) and
  lists a stage's colours in exactly `SpellsNewEffReader::stageTextures()` order (pinned for every record);
  `eddParticleAlpha()` is the client's fade x peak, one inline body shared with the particle. Kept out of
  `spells_new_eff_reader` (B1144's file); the orchestrator folds it into the shared reader.
- `EmitterConfig::regionColours[4]` (texture region k <- the emitter that named texture k) and
  `SpellPresets::applyStageEmitterColours(def, colours, clientAlpha = false)`; `applyStageSpriteNames()` clears
  the previous stage's words. A white emitter (both triples 255) stays inactive.
- `SimulationWorker::spawnSpellParticle()`: an active region's particle takes the start -> end RGB (interpolated by
  the existing `mix` over the particle's life); the region is chosen by the same single random draw as before, so
  the RNG stream is unchanged. With `clientAlpha` the particle also takes the client's alpha (per-frame
  `eddAlpha()`) and +0x68 blend; `UnifiedParticleRenderer` draws such additive particles in their own batches with
  `glBlendFunc(GL_SRC_ALPHA, GL_ONE)` (`UnifiedBlendMode::ADDITIVE_SRC_ALPHA`), and the Options opacity multiplies
  their alpha only (the client's SpellParticleOpacity). `UnifiedParticle` grew 120 -> 132 bytes (three floats).
- `SpellVisualFX::applyStageEddColours()` and one call per stage-art site (cast glow, projectile trail, impact,
  both landing halves, spellComplete). **This touches `spell_visual_fx.{h,cpp}`, which the dispatch reserved for
  B1144**: the stage index exists only at those call sites, and identifying it from the texture names is not
  exact (12 shipped records have Source and Target stages with the same texture list and different words). The
  edits are one line per site plus the loader and the helper, for the orchestrator to merge.

## Shipped: the RGB words; the client's alpha is behind `clientAlpha`, off

Measured on the harness (`--camera b1118 --seed 1`, `#castspell <id> 0` mirrored as a 0-ms BeginCast on self) and
on the oracle observer captures, with ONE scene-free instrument (`review/B1146/scripts/added_hue.py`: added colour
= frame - pre-cast median, over pixels risen > 40, Act/BC+1..7 s):

| record (spell) | words | oracle (B1134) hue / sat / lit px | before (e57e1f85) | clientAlpha on | **shipped** |
|---|---|---|---|---|---|
| 326 (4499) | 128,128,255 | 237 / 0.53 / 3.35 M | 71 / 0.12 / 2.22 M | 240 / 0.59 / 0.19 M | **238 / 0.26 / 2.22 M** |
| 329 (4498) | 255,0,0 | 360 / 0.95 / 2.94 M | 68 / 0.12 / 2.23 M | 358 / 0.85 / 0.10 M | **360 / 1.00 / 2.11 M** |
| 328 (4721) | 113,240,74 | 121 / 0.43 / 2.95 M | 68 / 0.12 / 2.20 M | 111 / 0.45 / 0.09 M | **101 / 0.59 / 2.09 M** |
| 330 (4670) | white | 243 / 0.20 / 3.44 M | 68 / 0.12 / 2.19 M | (white: unchanged) | **68 / 0.12 / 2.19 M** |

(The oracle crop is 300x300 around the caster, the harness crop 500x400; compare lit px within a row's siblings.)

- The colour words put 326 / 329 / 328 on the oracle's hue (within 3 / 0 / 20 deg); before, all four drew the
  same white (hue 68).
- The client's alpha on fable's emitters left the three coloured records at **1/20 of the oracle's lit pixels**
  while their white sibling 330 kept its brightness (strip, row 4): the oracle draws the four siblings equally bright.
  The client's alpha belongs to the client's own particle counts, sizes and lifetimes (+0x80..+0x8C, +0xCC.., +0x7C);
  fable's emitters are measured rows (B1131/B1132) fitted to the style's alpha and ONE, ONE. With the RGB alone the
  four siblings keep the oracle's equal brightness. Applying the client's alpha to EVERY emitter (an experiment, not
  shipped) dropped 93's landing coverage 3.006 -> 0.699 caster-heights^2 (lane 2.938).
- So production passes `clientAlpha = false`; `tests/test_spell_edd_colour.cpp` pins both settings.
- Shape is not this card: the harness draws the discipline Source scattered because base e57e1f85 has no row 51
  (B1134's feet-ring row is on B1144's branch).

## 93 Burst of Flame (the FxHarness spell) is unchanged

Record 80 is white in every slot, so its draw is identical: `harness_measure.py` summary before == after on every
column (land_cov 3.006, end +5.033, roi_l 0.071, roi_s 0.071, src_cov 0.116), and the scene-free hue is the same.

| 93 | oracle B1130 r51 | lane 5 fable leg B (B1132) | harness before | harness after |
|---|---|---|---|---|
| landing, Act+0.5..3.0 | 41 / 0.92 | 25 / 0.87 | 25 / 0.90 | 25 / 0.90 |
| Source, BC+0.5..1.5 | 23 / 0.93 | 14 / 0.82 | 20 / 0.86 | 20 / 0.86 |

The harness sits on the lane's hue (landing equal; Source +6 deg). The 16 deg landing gap to the oracle is not a colour
word (all white) and is not changed here.

## Tests and builds

- `tests/test_spell_edd_colour.cpp` (9 cases): decode offsets, the alpha formula, one colour per stage texture over
  every shipped record, the discipline records' words (326 and 329 share textures), record 80 white, the preset
  (white inactive, count mismatch, reset), the particle path through `SimulationWorker::computeOnce()` (shipped:
  RGB over the style's alpha; clientAlpha: fade x peak and the blend; white: particle-for-particle identical).
- Builds, ctest and hygiene: `review/B1146/README.md`.

## FOLLOW-UP

- Turn `clientAlpha` on when fable's spell emitters are the client's (per-slot EDD emitters with their own counts,
  sizes, lifetimes: B1144 plus the counts/sizes card), and then for white emitters too, so all siblings share the
  client's alpha. Until then white and coloured emitters both draw on the style's alpha and ONE, ONE.
- 328 Green reads hue 101 against the oracle's 121 with the RGB alone (the globe texture's blue weighs more under the
  client's alpha, 111 with it).
