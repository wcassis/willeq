# B1137 (#457) FX-SPELLSNEW-DECODE

Evidence, captures and the full decode census: `/mnt/projects/fable/review/B1137/README.md`.

## What changed

- **The measured Source/Target row a spell draws is chosen by its spellsnew.eff record (spells_us.txt
  field 145), not its spells.eff record (field 123).** The B1130 survey captured one spell per field-123 record,
  and those 40 spells draw 40 different field-145 records, so every B1131 `sourceRecords` / B1132
  `targetRecords` row is a measurement of one field-145 record. `config/spell_effects.json` `animRecords.rows`
  maps field-145 record -> row. The rows keep their survey ids (the field-123 record of the capture), so
  the generated row blocks and their tests are unchanged.
  The generator is `review/B1137/scripts/gen_anim_rows.py` (the capture's spell id -> its field 145; it refuses a
  record two rows would claim). `93=6` is passed by hand: a decoded equivalence, proven on a self cast.
- `SpellEffectsConfig::rowForAnim(spellAnim)` (`-1` = default rows) and `animRowCount()`;
  `SpellVisualFX::sourceRecordOf()` returns `rowForAnim(vocab.spellAnim)`. That function is the one place every
  row lookup goes through (Source, landing, holds, pool scale). No field-123 fallback: a field-123 row was
  captured on a spell that draws a different field-145 record.
- Effect: 88 of the 1,368 player spells on captured field-145 records change row (e.g. 1656 25 -> 31,
  1605 23 -> 32, 1899 default -> 4). The 79 player spells on the 10 uncaptured field-145 records draw the
  default rows, except record 93's 16, which take row 6. No tour spell (38 54 656 458 253 93 41) and no
  B1131/B1132 rep spell changes row.
- `sourceRecords.overrideRecords` values are unchanged and now equal the spells' field-145 rows (pinned).
- CLAUDE.md: the "field 123 keys the per-record anchors" sentence now says that field 145 picks the row.

## Tests

- `tests/test_spell_anim_rows.cpp` (new; config half runs headless, the `sourceRecordOf` half needs graphics):
  every captured row is reached by the record it was captured on; the B1127 pairs 1656/14 and 1605/372 take
  different rows; 1899 takes row 4; uncaptured records -> -1; the 93 = 1 equivalence; override rows are the
  field-145 rows; `SpellVisualFX` keys by `spellAnim`, not by `spellAffectIndex`.
  It is RED on 22dce2d0: `rowForAnim` does not exist there. Run on 22dce2d0's config it fails 4 of 5 tests
  (`review/B1137/tests/red_22dce2d0_config.txt`).
- `tests/spell_anim_rows.h`: the row -> field-145 table, typed from the survey. The anchor tests
  (`test_spell_{source,target}_anchors.cpp`) now note both indices, as the bridge does. On 22dce2d0's config
  they go red (no `animRecords`).

## Not used (unproven) and why

No spellsnew.edd numeric field is proven on the oracle. The census refutes the old named offsets in
`tools/model_viewer/spell_effect_db.h`: sizes at +0x160/+0x164 and gravity at +0x140 are zero on all 2118
entries, and "lifetime" at +0xA0 takes 6 values. That header is a viewer tool; it was not edited. The
castable spells give no one-field pair (`review/B1137/pairs.txt`), and the controlled oracle A/B was denied
(below). So anchor, shape, rate, lifetime, velocity, size, gravity and colour still come from the measured rows
and the style/override keys. Colour stays white (T441): +0xD4..+0xE8 look like two RGB triples, but that is
structural evidence only.

## FOLLOW-UP

1. **One-field oracle A/Bs** (needs the permission this card was denied: install a modified
   `spellsnew.edd`/`.eff` into the oracle client for one leg and restore it by sha). With ENC (class 14) self
   spells as carriers (10 Augmentation, 39 Quickness, 46 Ultravision, 64 Resist Magic, 84 Shifting Sight,
   174 Clarity, 289 Taper Enchantment: 7 distinct field-145 records), make each record ONE Source emitter
   (base = EDD 554 `Tallon'sArrow1 3-2`, gena20.bmp, one picture) and vary one field per gem:
   slot `d` 1/2/3/4/5; +0xCC x4; +0x80 x4; +0x84 x4; +0xA4 x4; +0x94 x4; +0x88 x4; +0xAC x4; +0xB8 -3;
   +0xA8 0/1/3/4/9; +0xD4..+0xE8 = 255,0,0. Three legs cover it. Each field then gets a measured transfer
   function, and fable can read it per emitter.
2. Colour pair without file edits: 326 Discipline - Blue vs 329 Discipline - Red (identical but for the colour
   words), `/disc` on a warrior (B1134 #454's leg type).
3. DDS art: the discipline/NPC records name `.dds` textures, which the atlas does not pack (B1127 FOLLOW-UP).
4. **Reagent defect:** after `#summonitem 10031` + `/autoinventory`, `checkReagents` says "carrying 0" and the
   client refuses 1899 (Imbue Fire Opal); the oracle casts it (`review/B1137/gate2/fable_WIZ2/fable.log`).
5. Source extents grow with cast time (1656 3.0 s vs its row's 1.75 s capture: Source centroid 0.62 vs 0.41).
   A row holds ONE cast time. Scaling it needs the EDD velocity/lifetime fields proven (item 1).

## Gate (lane 5, 36ba97e6; `review/B1137/README.md` sec. 6)

- The tour (A/B/C) is unchanged within run-to-run noise against B1127's gate: landing coverage -5.2..+0.4 %,
  ends +-0.07 s, hues identical.
- Pairs (tour rig, new vs old key on one binary): 1605's Source moves into band (centroid 0.26 vs the oracle's
  0.36; old 0.63). 1656's Source is out of band with either key. Landing end and coverage are out of band with
  either key. On this rig the landing centroid cannot be separated from the caster. On the oracle's own
  captures (`row_check.tsv`), 1605's landing shape is in band with the new row and out with the old.
- Whole-tree Release build with the CI flags exit 0; `ctest -L hygiene` 50/50; the headless config builds and
  passes the new TU.
