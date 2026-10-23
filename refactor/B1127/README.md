# B1127 (#447) FX-SECONDARY-LIST + B1126 (#446) FX-MISSING-SPRITE

Evidence: `/mnt/projects/fable/review/B1127/` (index: its `README.md`: proof captures, field table, gate).

## 0. Result

**The official client draws `spellsnew.eff`, not `spells.eff`.** `spells_us.txt` field 145 (eqemu
`spellanim`) names a `spellsnew.eff` record; its Source / Middle / Target stages list `spellsnew.edd` emitters;
each emitter names a texture file under `SpellEffects/`, and that file is the art. Proven on lane 5 by
same-field-123 / different-field-145 pairs (1656 Thunderbold vs 14 Strike: fire vs red crosses on spells.eff
record 25; 1605 Frost vs 372 Blast of Cold: blue crosses vs violet squiggles on record 23) and by 1899 Imbue
Fire Opal, which has no spells.eff record and draws field 145's green stars.

- **B1127.** The spells.eff secondary list and per-slot arrays are decoded structurally
  (review `eff_decode.tsv`) but are NOT what the oracle draws: no field of that file is used. 54/656 now draw
  record 82's `spelua.png` (violet squiggles), `spelub.png` (crosses) and `spelab.tga`.
- **B1126.** 458's art is `gena20.bmp` (its record 121 names nothing else); the GENI00 records' art is
  `flare_blsp50a.png`, never `speli1.tga`'s skulls. No GEN-name rule changed: the GEN rule now serves only the 60
  NPC spells with no usable field 145.
- **Note 7097** (the 93/38 white flash): consistent with `spelcm.png` (record 80's Target emitter), from an
  unsaturated capture (`SpellParticleOpacity=0.25`); fable draws it now as one of record 80's sprites, size
  unchanged.

## 1. Code

- `include/client/spell/spells_new_eff_reader.h` + `src/client/spell/spells_new_eff_reader.cpp`: the two files.
  Only the emitter index per stage slot, the EDD texture and its frame count are used; every other field is
  exposed raw or not decoded (field table in the review README).
- Field 145 plumbing: `SpellField::SpellAnim`, `spells.spell_anim` (willeq.db version **8**),
  `SpellInfo::spellAnim`, `SpellData::spell_anim`, `SpellVisualVocabulary::spellAnim` (the struct keeps 8 bytes:
  `spellAffectIndex` became int16).
- `SpellVisualSelector::resolve(affect, spellAnim)`: when field 145 names a populated record, each stage's names
  ARE that stage's texture files (up to four) and its presence is that stage's; field 123 still keys the
  B1131/B1132 anchors and the spells.eff attach tag is kept. A field-145 record resolves a spell with no
  spells.eff record (1899).
- `SpellEffectsConfig::getTextureIndex()` takes a texture file name first (any case).
- Atlas (`spell_sprite_atlas.h`, `tools/spell_sprite_atlas_builder.cpp`, new `tools/sprite_image_reader.h`):
  60 sprites = the non-DDS spellsnew.edd textures of every player spell + the spells.eff files only
  field-145-less spells reach; 2x2 grids (the 128x128 flares) and PNG / 8-bit BMP / colour-mapped TGA decode;
  case-insensitive file lookup. 960 of 1024 cells. Same pack size, new bytes.
- Sizes (35e949f0): 54/656/253/458 re-fitted to the SAME oracle ROI p90 bands by B1123's sizes-first rule
  (their new sprites fill the quad differently); size keys only, three lane-5 iterations.
- Data in one commit: `spell_sprite_atlas.bmp`, `data/textures.sha256`, `data/willeq.eqe1.sha256`,
  `data/willeq.db` + `.sha256`. **The pack is NOT installed** (`build_willeq_pack.sh` install owed at merge).

## 2. Tests (red on e91342c6: the reader, field and selector do not exist there)

- `tests/test_spells_new_eff_reader.cpp` (new): file shape, EDD texture + frames, the gate spells' stage
  textures, the selector routing (54, 458, 1656 vs 14, 1899, field 145 = 0 unchanged).
- `test_spell_database` `SpellAnimFieldRoundTrips`; `test_spell_visual_vocabulary` carries `spellAnim`;
  `test_spell_target_stage_textures` file-name lookup + 54's Target regions; atlas tests re-derived for the
  60-file sheet (2x2 grids, PNG/BMP sources; spelqa/spelw1 classified by eye, census gap).

## 3. Not done

- DDS textures (NPC and discipline records, 128 files) are not packed: those emitters draw the procedural circle.
- No spellsnew.edd timing / size / count / colour field is decoded or used. The 18 player spells with field
  123 = -1 now resolve (their sprites draw with the plain style).
- Card saturation gates not met (54/656 landing sat 0.21/0.20 < 0.40, 458 0.42 < 0.45); hues met (244/243,
  200). Background-removed, fable adds the oracle's hue at lower saturation (review README sec. 5; leads: scene
  background, ETC1's luminance-only modifiers). 253's Source ROI fell short (point clamp).
- `plans/legB2_mag.tsv` (1770 Rage of Zomm, a fourth A/B) not run.
