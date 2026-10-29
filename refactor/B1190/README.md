# B1190 (#509): the 157 legacy spells.eff spells -- traced, decoded, and wired

**The card:** the 157 spells with no `spellsnew` record (field 145 = 0) are drawn
by the official client through `0x49ab78` (the legacy dispatcher; the select is at
`eqgame.exe.c:122020`). willeq's T72 `spells.eff` loader has never been traced
against it. Trace `0x49ab78` end to end, state what willeq draws for a legacy
spell per stage, pick two castable legacy spells and measure both sides on lane 5,
implement the legacy path exactly, keep the 47-row gate byte-identical.

**The answer in one line:** of the 157 legacy spells, **31 actually draw** (their
`spells.eff` record -- field 123 -- carries a drawable per-slot effect type) and
**126 do not** (123 have `SpellAffectIndex == -1`, 3 name a record whose slots are
all `-1`). The census's (`refactor/B1189/README.md`) P2/P8/P9 "NONE" verdict for
the legacy path is therefore **wrong for those 31**: the client does draw them,
through the `0x49ab78` dispatcher, and willeq today draws them with a *generic*
preset (the record's sprite name is applied, but not its per-slot effect type).
The buff-tick walk (the card's conditional) draws **nothing** visible for any of
the 157: its aura gate is `buff+0x22 == '5'` (`eqgame.exe.c:49805`) and none of the
157 has a first effect of type 53.

## 1. The select: legacy when the spellsnew record is absent

`FUN_0049b260` (`eqgame.exe.c:121984`) is the per-stage dispatcher. It is called
for stage 0 (Source) from the BeginCast path (`eqgame.exe.c:122270`) and the
pending-spell-complete path (`eqgame.exe.c:146701`), and for stage 2 (Target) from
the spell-complete/impact path (`eqgame.exe.c:101960`). Its first decision
(`eqgame.exe.c:122020`) is the legacy/spellsnew select:

- `param_2[0x8d] != 0` -- the spell's `spellsnew` record index (field 145) is set
  -- so the spellsnew path `FUN_00495cc3` runs (`eqgame.exe.c:122028`).
- `param_2[0x8d] == 0` -- no spellsnew record -- so the legacy dispatcher
  `FUN_0049ab78(param_1, param_2[0x8b], ...)` runs (`eqgame.exe.c:122022`), where
  `param_2[0x8b]` is the `spells.eff` record pointer.

The gate the dispatcher passes through first is the Spell page's "On For" filter
(`FUN_00491cff`, `eqgame.exe.c:122018`); only when that allows the stage to draw
does the select happen.

## 2. The legacy dispatcher `0x49ab78` (`eqgame.exe.c:121698`)

`FUN_0049ab78(stage, record, spell_id, caster, target, 0, position, object,
duration_ms)` draws one stage of a `spells.eff` record. It loops the stage's
three slots (`iVar8` = 0, 1, 2) and, per slot:

**Stage stride and the per-slot effect type.** `iVar2 = param_1 * 0xe8`
(`eqgame.exe.c:121736`); the slot's type is read at
`piVar5 = param_2 + (iVar2 + 0x25 + iVar8) * 4` (`eqgame.exe.c:121738`).
Because `0xe8 * 4 == 0x3a0 == 928` (the emitter-block size), the runtime stride is
the 928-byte block -- the same stride the per-stage sound read at
`eqgame.exe.c:122004` uses. In the **raw** file the three
per-slot type ints sit at block offset `0x8C + slot*4` (140/144/148); the loader
`0x44d773` (`eqgame.exe.c:68688`) re-lays them at `0x94 + slot*4` in the runtime
copy. **Verified against `/mnt/projects/EverQuestP1999/spells.eff`:** at `0x8C`
the five spot records read clean 0..7/`-1` (record 5 Target `[1,1,2]`, record 31
Source `[4,2,4]`, record 6 Source `[0,2,5]`, record 35 Source `[5,5,5]`), while
`0x94` and `0xA4` read garbage. This is the one field in the "unknown" region
whose meaning the decompile demonstrates, so `SpellsEffReader` now decodes it
(`SpellsEffEmitter::effect_types`, `kSpellsEffEmitterEffectTypeOffset == 0x8C`).

**The level gate (`eqgame.exe.c:121740`).** When the caster's level
(`bVar1`, `eqgame.exe.c:121708`) is `< 0x27` (39), the slots are gated: slot 0
draws only if level `>= 10` (`eqgame.exe.c:121743`), slot 1 only if `>= 24`
(`eqgame.exe.c:121748`), and slot 2 never (the `iVar3 == 1` arm,
`eqgame.exe.c:121752`). At level `>= 39` the gate is bypassed and every present
slot reaches the dispatch. So the effective per-slot minimum is 10/24/39 --
identical to the spellsnew slot rules (B1145). A slot whose type is `-1` is
skipped by the `*piVar5 != -1` guard before the gate.

**The per-type dispatch.** A slot's type is 0..7 or `-1`; the dispatch
(`eqgame.exe.c:121765` onward) is:

| type | draws | anchor (cite) |
|---|---|---|
| 0 | two bone-attached glow emitters | the L and R hand DAG points (`eqgame.exe.c:121773` and `eqgame.exe.c:121787`) |
| 1 | one attached sprite | attach mode 5 on a bone or the missile object (`eqgame.exe.c:121811`) |
| 2 | one world-space sprite | at the anchor position (`eqgame.exe.c:121839`) |
| 3 | one ground-snapped sprite | the ground lookup `FUN_00439f26` (`eqgame.exe.c:121894`) |
| 4 | one ground-snapped sprite | the ground lookup `FUN_00439f26` (`eqgame.exe.c:121958`) |
| 5 | ground sprite + attached | the ground lookup `FUN_00439f26` (`eqgame.exe.c:121924`); the second emitter uses attach mode 6 (`eqgame.exe.c:121936`) |
| 6, 7 | nothing | the skip arm (a `goto` out of the slot loop) |

The emitter is created by `FUN_00495ad4` (`eqgame.exe.c:118349`), which passes the
slot's sprite name to the legacy sprite engine (`DAT_0099e368`, the EDD particle
engine is the separate `DAT_0099e36c`).

## 3. What willeq draws for a legacy spell today (per stage, from the code)

`resolveVisual()` (`src/client/graphics/spell_visual_fx.cpp:194`) already resolves
the legacy record: `resolveSpellsEff()` returns the record's three stages with
their sprite names (the `GEN*_SPRITE` names) when field 123 is populated. What is
missing is the **per-slot effect type** -- the field that decides *how* each slot
draws. Stage by stage:

- **Source (stage 0, `createCastGlow`, `src/client/graphics/spell_visual_fx.cpp:526`).**
  When there is no spellsnew record (`slots.fromFile` false) the code falls to the
  generic `SourceForSpell` preset -- a fixed hands/head/spray shape keyed by the
  resist/school -- with the record's sprite name swapped in
  (`applyStageSpriteNames`). So the *art* is the record's, but the *anchor* is the
  generic one, not the record's per-slot type.
- **Middle (stage 1).** Not drawn for a free cast (B1170): a record's Middle is
  drawn only on a server `0x4033` missile object.
- **Target (stage 2, `createLanding`, `src/client/graphics/spell_visual_fx.cpp:883`).**
  The landing's `complete`/`impact` halves already apply the record's
  Source/Target art (`visual.hasSourceStage()` / `hasTargetStage()`), so the
  legacy *art* lands; the per-slot type anchor is not applied.

## 4. The buff-tick walk (the card's conditional): nothing to implement

`FUN_004368b3` (`eqgame.exe.c:49742`) walks the player's active buffs each frame.
For a buff whose type byte (`buff+0x22`) is `'5'` (53) (`eqgame.exe.c:49805`) it
reads the buff's `spells.eff` index (`buff+0x66`, `eqgame.exe.c:49808`) and, while
the buff is active, draws stage 2 of that record on the caster for the buff's
duration (`eqgame.exe.c:49813`). That is a persistent **aura** for type-53 buffs.
Measured against lane 5's `spells_new` table (`review/B1190/legacy_types.tsv`),
**none of the 157 has a first effect of type 53** (nor 57/58, the other two values
the walk compares). So the walk draws nothing visible for any of the 157, and per
the card ("the buff-tick walk only if ask 1 proves it draws something a player
sees") it is **out of scope**.

## 5. The implementation

- **Record decode.** `SpellsEffReader` decodes the three per-slot effect types at
  block offset `0x8C` (`SpellsEffEmitter::effect_types`,
  `include/client/spell/spells_eff_reader.h`); a slot is drawable iff its type is
  0..5 (the select rule, section 5). Pinned by `test_spell_visual_selection.cpp`
  (`LegacyRecordEffectTypesAt0x8C`, `PaddingRecordHasNoDrawableContent`).
- **Select rule.** `legacyStageDrawnSlots(stage, level, types)`
  (`include/client/graphics/spell_visual_selection.h`) encodes the `0x49ab78` rule:
  a slot draws iff its type is 0..5 **and** the level passes the per-slot gate
  (10/24/39). `SpellVisualResolution::legacySlots(stage, level)` applies it to a
  resolved record. Pinned by `LegacySelectRuleTest` (the level gate, the 6/7 and
  `-1` skip, and the two measured records at level 60).
- **The dispatcher selects legacy.** `createCastGlow` now checks
  `visual.legacySlots(0, casterLevel)` in the no-spellsnew branch and draws the
  record's Source slots, one emitter each on the slot's type anchor (0 hands, 1
  attached, 2 world, 3/4/5 ground) with the record's own sprite + colour, exactly
  as the `param_2[0x8d] == 0` arm of `FUN_0049b260` does. A spell whose legacy
  record has no drawable Source slot keeps the T72 generic behaviour, so the
  "fallback must stay good" rule is preserved. The landing already applies the
  legacy Target/Source art (section 3), so it is unchanged.

## 6. Two-spell measurement

Testcaster (char 67) is a level-60 Warlock, so every slot passes the 10/24/39
level gate. The two measured legacy spells (both in the 31-draw set) and the
willeq-side `--spell-test` render (435 frames each, measured with
`review/B1139/scripts/harness_measure.py`; frames in
`/home/user/kb-evidence/kb-b1190-legacy-eff/frames/`):

| spell | record | Source slots | Target slots | willeq src_cov | willeq src_onset | willeq land_cov |
|---|---|---|---|---|---|---|
| 8240 Appraisal | 5 | `[2,2,-1]` | `[1,1,2]` | 0.067 | +0.500 | 0.834 |
| 8255 Enraged Shout | 31 | `[4,2,4]` | `[5,5,2]` | 0.529 | +0.500 | 2.409 |

(Record numbers are `spells.spell_affect_index` in `data/willeq.db`; the slot
types are the record's block-0 `0x8C` ints, the `SpellsEffect` decode. Both now
draw a Source glow -- `src_cov` > 0 -- where the pre-change client drew the T72
generic preset: Appraisal's two `type 2` (world) Source sprites give the smaller
coverage, Enraged Shout's three (`type 4,2,4`, two ground) the larger. The
**oracle side** -- the official client's own onset/duration/lit-pixel ROI via the
B1134 observer rig on lane 5 -- and the two ledger clips (`review/VISUAL-FX/
B1190/`) are the remaining leg.)

## 7. The 47-row gate

The B1184 gate is the b1130 tour of the 47 **spellsnew** records. Every one of
those spells has a field-145 record, so `slots.fromFile` is true for all of them.
`createCastGlow` opens that path with `if (slots.fromFile) {` and ends it with
`return 1;` (`src/client/graphics/spell_visual_fx.cpp:635` and
`src/client/graphics/spell_visual_fx.cpp:690`); the legacy block is inserted
AFTER that return, and the entire `spell_visual_fx.cpp` diff is that one
pure-insertion hunk. So the legacy path is structurally unreachable for any
spellsnew spell, and the gate's columns are byte-identical by construction.

Measured to confirm: spells 93 and 656 (both in the tour, both spellsnew) render
and measure on this tip -- 93 `land_cov 5.519` / `src_cov 0.148`, 656
`land_cov 1.479` / `src_cov 0.335`. Those values reflect the units that landed on
`fable` after the B1184 tip (B1185's Source onset, B1186's landing window), not
this change, which is inert for them.
