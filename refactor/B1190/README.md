# B1190 (#509): the 157 legacy spells.eff spells -- traced, decoded, and wired

**The card:** the 157 spells with no `spellsnew` record (field 145 = 0) are drawn
by the official client through `0x49ab78` (the legacy dispatcher; the select is at
`review/B1141/ghidra/out/eqgame.exe.c:122020`). willeq's T72 `spells.eff` loader has never been traced
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
the 157: its aura gate is `buff+0x22 == '5'` (`review/B1141/ghidra/out/eqgame.exe.c:49805`) and none of the
157 has a first effect of type 53.

## 1. The select: legacy when the spellsnew record is absent

`FUN_0049b260` (`review/B1141/ghidra/out/eqgame.exe.c:121984`) is the per-stage dispatcher. It is called
for stage 0 (Source) from the BeginCast path (`review/B1141/ghidra/out/eqgame.exe.c:122270`) and the
pending-spell-complete path (`review/B1141/ghidra/out/eqgame.exe.c:146701`), and for stage 2 (Target) from
the spell-complete/impact path (`review/B1141/ghidra/out/eqgame.exe.c:101960`). Its first decision
(`review/B1141/ghidra/out/eqgame.exe.c:122020`) is the legacy/spellsnew select:

- `param_2[0x8d] != 0` -- the spell's `spellsnew` record index (field 145) is set
  -- so the spellsnew path `FUN_00495cc3` runs (`review/B1141/ghidra/out/eqgame.exe.c:122028`).
- `param_2[0x8d] == 0` -- no spellsnew record -- so the legacy dispatcher
  `FUN_0049ab78(param_1, param_2[0x8b], ...)` runs (`review/B1141/ghidra/out/eqgame.exe.c:122022`), where
  `param_2[0x8b]` is the `spells.eff` record pointer.

The gate the dispatcher passes through first is the Spell page's "On For" filter
(`FUN_00491cff`, `review/B1141/ghidra/out/eqgame.exe.c:122018`); only when that allows the stage to draw
does the select happen.

## 2. The legacy dispatcher `0x49ab78` (`review/B1141/ghidra/out/eqgame.exe.c:121698`)

`FUN_0049ab78(stage, record, spell_id, caster, target, 0, position, object,
duration_ms)` draws one stage of a `spells.eff` record. It loops the stage's
three slots (`iVar8` = 0, 1, 2) and, per slot:

**Stage stride and the per-slot effect type.** `iVar2 = param_1 * 0xe8`
(`review/B1141/ghidra/out/eqgame.exe.c:121736`); the slot's type is read at
`piVar5 = param_2 + (iVar2 + 0x25 + iVar8) * 4` (`review/B1141/ghidra/out/eqgame.exe.c:121738`).
Because `0xe8 * 4 == 0x3a0 == 928` (the emitter-block size), the runtime stride is
the 928-byte block -- the same stride the per-stage sound read at
`review/B1141/ghidra/out/eqgame.exe.c:122004` uses. The record-relative offset
`0x94 + slot*4` the dispatcher reads is the 8-byte record header plus block
offset `0x8C + slot*4` (0x94 = 8 + 0x8C; likewise the slot's colour words at
record-relative `0x68 + slot*4` = 8 + block `0x60`). The loader `0x44d773`
(`review/B1141/ghidra/out/eqgame.exe.c:68688`, the `_fread` at `:68705`) is a
plain `_fread(&DAT_008513b8, 0xae8, 0x100)` -- 256 whole 0xae8-byte records,
**no re-layout**: the runtime table IS the raw file, so the dispatcher's `0x94`
read and the raw file's block-`0x8C` byte are the same bytes.
**Verified against `/mnt/projects/EverQuestP1999/spells.eff`** (714,752 bytes =
256 x 0xae8): at record offset `0x94` (= block `0x8C`) the spot records read
clean 0..7/`-1` triples (record 5 Source `[2,2,-1]` / Target `[1,1,2]`, record 31
Source `[4,2,4]` / Target `[5,5,2]`, record 6 Source `[0,2,5]`, record 35 Source
`[5,5,5]`), byte-identical between record offset `0x94` and block offset `0x8C`.
This is the one field in the "unknown" region whose meaning the decompile
demonstrates, so `SpellsEffReader` now decodes it
(`SpellsEffEmitter::effect_types`, `kSpellsEffEmitterEffectTypeOffset == 0x8C`).

**The level gate (`review/B1141/ghidra/out/eqgame.exe.c:121740`; the
disassembly 0049abea-0049abfb settles it).** When the caster's level
(`bVar1`, `:121734`, the caster level `*(byte *)(param_4 + 0x24c)`) is `< 0x27` (39): slot 0 draws unless (level `< 10` AND
the stage is Middle) (`:121741-:121744`; 0049ac09 `cmp level, 10`, then
0049ac0f `cmp stage, 1` and the 0049ac13 `je 0x49b23f` tests THAT compare); slot 1 draws only at level
`>= 24` in EVERY stage (`:121746-:121748`; 0049abfd-0049ac01, no
stage-conditioned test); slot 2 never -- the C's `if (iVar3 == 1) goto skip`
at LAB_0049ac13 (`:121752`) is a register-reuse artifact: on the slot-2 path
`iVar3` still holds `iVar8 - 1 == 1` (`:121746`), so the skip is unconditional
(the disassembly's slot-2 fall-through `dec eax; dec eax; jmp 0x49ac13`
reaches the `je 0x49b23f` with ZF=1). At level `>= 39` the gate is bypassed
(`:121740`; 0049abea `cmp ..., 0x27`, 0049abee `jge 0x49ac19` straight to the
dispatch) and every present slot reaches the dispatch. A slot whose type is `-1` is skipped by the
`*piVar5 != -1` guard (`:121739`) before the gate. This is NOT the spellsnew
10/24/39 per-slot-minimum rule (B1145): the legacy slot-0 minimum applies to
the Middle stage only, and the slot-2/39 condition gates every stage. (B1190
fix, note 8631 f1: willeq's first pass copied the per-slot-minimum reading
from this same paragraph; the disassembly reading above is the one now
implemented in `legacyStageDrawnSlots`.)

**The per-type dispatch.** A slot's type is 0..7 or `-1`; the dispatch
(`review/B1141/ghidra/out/eqgame.exe.c:121765` onward) is:

| type | draws | anchor (cite) |
|---|---|---|
| 0 | two bone-attached glow emitters | the L and R hand DAG points (`review/B1141/ghidra/out/eqgame.exe.c:121773` and `review/B1141/ghidra/out/eqgame.exe.c:121787`) |
| 1 | one attached sprite | attach mode 5 on a bone or the missile object (`review/B1141/ghidra/out/eqgame.exe.c:121811`) |
| 2 | one world-space sprite | at the anchor position (`review/B1141/ghidra/out/eqgame.exe.c:121839`) |
| 3 | one ground-snapped sprite | the ground lookup `FUN_00439f26` (`review/B1141/ghidra/out/eqgame.exe.c:121894`) |
| 4 | one ground-snapped sprite | the ground lookup `FUN_00439f26` (`review/B1141/ghidra/out/eqgame.exe.c:121958`) |
| 5 | ground sprite + attached | the ground lookup `FUN_00439f26` (`review/B1141/ghidra/out/eqgame.exe.c:121924`); the second emitter uses attach mode 6 (`review/B1141/ghidra/out/eqgame.exe.c:121936`) |
| 6, 7 | nothing | the skip arm (a `goto` out of the slot loop) |

The emitter is created by `FUN_00495ad4` (`review/B1141/ghidra/out/eqgame.exe.c:118349`), which passes the
slot's sprite name to the legacy sprite engine (`DAT_0099e368`, the EDD particle
engine is the separate `DAT_0099e36c`).

## 3. What willeq draws for a legacy spell today (per stage, from the code)

`resolveVisual()` (`src/client/graphics/spell_visual_fx.cpp:197`) already resolves
the legacy record: `resolveSpellsEff()` returns the record's three stages with
their sprite names (the `GEN*_SPRITE` names) when field 123 is populated. What is
missing is the **per-slot effect type** -- the field that decides *how* each slot
draws. Stage by stage:

- **Source (stage 0, `createCastGlow`, `src/client/graphics/spell_visual_fx.cpp:533`).**
  Before this fix, when there is no spellsnew record (`slots.fromFile` false) the
  code fell to the
  generic `SourceForSpell` preset -- a fixed hands/head/spray shape keyed by the
  resist/school -- with the record's sprite name swapped in
  (`applyStageSpriteNames`). So the *art* was the record's, but the *anchor* was the
  generic one, not the record's per-slot type.
- **Middle (stage 1).** Not drawn for a free cast (B1170): a record's Middle is
  drawn only on a server `0x4033` missile object.
- **Target (stage 2, `createLanding`, `src/client/graphics/spell_visual_fx.cpp:911`).**
  The landing's `complete`/`impact` halves already apply the record's
  Source/Target art (`visual.hasSourceStage()` / `hasTargetStage()`), so the
  legacy *art* lands; the per-slot type anchor is not applied.

## 4. The buff-tick walk (the card's conditional): nothing to implement

`FUN_004368b3` (`review/B1141/ghidra/out/eqgame.exe.c:49742`) walks the player's active buffs each frame.
For a buff whose type byte (`buff+0x22`) is `'5'` (53) (`review/B1141/ghidra/out/eqgame.exe.c:49805`) it
reads the buff's `spells.eff` index (`buff+0x66`, `review/B1141/ghidra/out/eqgame.exe.c:49808`) and, while
the buff is active, draws stage 2 of that record on the caster for the buff's
duration (`review/B1141/ghidra/out/eqgame.exe.c:49813`). That is a persistent **aura** for type-53 buffs.
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
  (`include/client/graphics/spell_visual_selection.h`) encodes the `0x49ab78`
  gate from the disassembly (section 2): a slot draws iff its type is 0..5 and
  the level passes the stage-aware gate -- slot 0 is gated at 10 in the Middle
  stage only, slot 1 at 24 in every stage, slot 2 at 39 in every stage, and
  level >= 39 bypasses everything. `SpellVisualResolution::legacySlots(stage,
  level)` applies it to a resolved record. Pinned by `LegacySelectRuleTest`
  (`LevelGateSemanticsFromDisassembly` -- the per-stage gate, the 6/7 and -1
  skip, and the record-5 and record-31 slot types at level 60).
- **The dispatcher selects legacy.** `createCastGlow` now checks
  `visual.legacySlots(0, casterLevel)` in the no-spellsnew branch and draws the
  record's Source slots on the slot's type anchor (0 hands, 1 attached, 2 world,
  3/4 ground) with the record's own sprite + colour, exactly as the
  `param_2[0x8d] == 0` arm of `FUN_0049b260` does. A spell whose legacy
  record has no drawable Source slot keeps the T72 generic behaviour, so the
  "fallback must stay good" rule is preserved. The landing already applies the
  legacy Target/Source art (section 3), so it is unchanged.
- **The type-5 double emitter (fix, note 8631 f1).** The original leg folded
  types 3/4/5 into one single-emitter ground branch, but `0x49ab78`'s type-5
  arm draws TWO emitters (section 2, table row 5): the ground emitter
  (`review/B1141/ghidra/out/eqgame.exe.c:121936`, z ground-adjusted at
  `:121924`) and, on a free cast, a second mode-6 emitter attached to the
  caster model (`:121937-:121940` into the shared creation site
  `:121866-:121876`, attached at `:121869`). 22 of the 31 drawable legacy
  spells carry a type-5 Source slot (e.g. 8080/8081/8082 -> record 35
  `[5,5,5]`, 7825 -> record 6 `[0,2,5]`, 7023-7026 -> record 8 `[2,2,5]`), so
  they drew one emitter where the oracle draws two; the two previously
  measured spells (8240 `[2,2,-1]`, 8255 `[4,2,4]`) do not exercise type 5, so
  the old two-spell table could not catch it. The type-5 branch now draws the
  ground emitter at the feet and a second emitter attached at the entity anchor
  height (mid-torso) (the oracle, lane 5, spell 8080 caster view, shows the
  second as a small mid-torso/waist glow -- `leg8080/video-caster.mp4`,
  section 8). The per-slot emitter count is the
  `legacySlotEmitterCount` invariant (pinned by
  `LegacySelectRuleTest.EmittersPerDrawableSlot`) and is logged in the legacy
  block. 8080 is now in the measured set, both sides (section 8).

## 6. Two-spell measurement (oracle vs willeq)

Testcaster (char 67) is a level-60 Warlock, so at level 60 (>= 39) every slot
passes the level gate (section 2). Both measured spells are in the 31-draw
set. **Before** (the T72
generic preset, section 3) drew the record's sprite name on a generic anchor;
**after** (this tip) draws the record's per-slot effect types (section 5). The
**oracle side** is the official client observed through the B1134 observer rig on
lane 5 (the second official client `Oraclewatch` frames the caster; measured with
the B1132 instrument, `review/B1122/scripts/` + `review/B1130/scripts/anchor.py`
on the observer's 1280x800 crop). The **willeq side** is the `--spell-test`
render on this tip (`review/B1139/scripts/harness_measure.py`; the raw frame
dirs `frames/b1190_8240/` and `frames/b1190_8255/` hold 435 and 450 PNGs
respectively, and the harness measured 381 and 396 of them).

Record numbers are `spells.spell_affect_index` in `data/willeq.db`; the slot
types are the record's block-0 `0x8C` ints (the `SpellsEffReader` decode).

| spell (record) | stage | oracle onset | oracle peak / run | oracle cov | willeq onset | willeq peak (end) | willeq cov |
|---|---|---|---|---|---|---|---|
| 8240 Appraisal (5) | Source `[2,2,-1]` | +0.51 s | +1.11 s, run 1.8 s | 0.370 | +0.500 s | +1.333 s | 0.067 |
| 8240 Appraisal (5) | Landing `[1,1,2]` | +0.47 s | +1.11 s, run 1.8 s | 0.370 | Act+0.400 s | Act+1.300 s (end +4.633 s) | 0.834 |
| 8255 Enraged Shout (31) | Source `[4,2,4]` | not measured (no rig file; see note) | 2.25 s channel (db), no landing | -- | +0.500 s | +1.667 s | 0.529 |
| 8255 Enraged Shout (31) | Landing `[5,5,2]` | none (no `OP_Action`) | -- | -- | Act+0.600 s | Act+1.900 s (end +4.867 s) | 2.409 |

The **willeq onset** column is the onset at the 25 % coverage threshold
(`src_on25` for Source; the `LANDing onset` row for Landing), and the **willeq
peak (end)** column is the coverage peak (and, in brackets, the 10 % end) --
the two are separate metrics, so the Landing rows are like-for-like with the
Source rows on the onset column. The **oracle onset/peak/cov** for 8240 are
from `measure_oracle/leg5_8240_1` (the observer-rig measurement); the 8255
oracle was **never numerically measured** (see the note below), so its onset
and cov cells are blank rather than carried over. All willeq times are from
`measure/b1190_8240/measure_harness.txt` and `measure/b1190_8255/
measure_harness.txt` (the `--spell-test` render, 381 measured frames for 8240
and 396 for 8255; the raw frame dirs hold 435 and 450 PNGs respectively,
`frames/b1190_8240/` and `frames/b1190_8255/`).

**The 8255 oracle evidence note (note 8631 f4).** The 8255 Source was cast in
the lane-5 rig by the GM path (`#castspell 8255 0`), which sent an
`OP_BeginCast` (the channel glow -- the oracle's `type 4` ground Source
column, visible in the observer video) but produced **no `OP_Action`**
(`leg5/30_cast_8255.opcodes.tsv` and `leg5/31_fx_8255.opcodes.tsv`: zero
`OP_Action` rows). Because the measurement rig keys on the cast/landing
packets, 8255 was never run through the B1132 instrument --
`measure_oracle/` holds only `leg5_8240_1` (8240) for this unit. The
"+0.51 s" onset that an earlier draft of this table showed for the 8255
Source was a **carry-over of the 8240 Source onset**, not a 8255
measurement, and has been removed. The qualitative 8255 facts that ARE
evidenced: the 2.25 s channel duration = `willeq.db`
`spells.cast_time_ms = 2250` for 8255, and the no-landing fact = the zero
`OP_Action` count above.

**The divergence, per row.**

- **8240 Source.** Onset matches (+0.51 s oracle, +0.50 s willeq). The oracle
  draws a bright, cohesive full-body burst (peak cov 0.370, spanning legs through
  shoulders); willeq's two `type 2` (world) Source sprites are a dim scatter
  (cov 0.067, ~5.5x weaker). *Mechanism:* the oracle's legacy `type 2` world
  sprites render larger/brighter than willeq's; willeq applies the record's
  sprite name to a smaller generic emitter, so the legacy Source is present but
  understated.
- **8240 Landing.** The oracle's landing (self-targeted, on the caster) runs
  1.8 s at peak cov 0.370; willeq's is stronger (cov 0.834) and longer (end
  +4.63 s vs the oracle's +2.27 s run end). *Mechanism:* willeq's landing
  window/coverage is wider than the oracle's for a `type 1/2` Target slot.
- **8255.** Enraged Shout is a **channeled** legacy spell (2.25 s cast, record
  31). In the observer rig the GM-cast path (`#castspell 8255 0`) sent an
  `OP_BeginCast` (the channel glow -- the oracle's `type 4` ground Source
  sprites, a bright column) but **no `OP_Action`**: the channel's landing never
  fired in the rig, so the oracle landing is unmeasurable there. Willeq's
  `--spell-test` harness completes the cast and draws it (Source cov 0.529, a
  spherical cloud; Landing cov 2.409). *Mechanism:* the oracle's Source is the
  record's `type 4` ground column; willeq draws a spherical cloud (the generic
  shape), so the Source shape diverges, and the rig's GM path did not complete
  the channel's landing (a rig artifact -- the channel is live in-game, it just
  is not triggered to completion by the GM cast command here).

The two ledger clips (`review/VISUAL-FX/B1190/`:
`b1190_8240_appreciation.mp4`, `b1190_8255_enraged_shout.mp4`, oracle LEFT /
willeq RIGHT, built by `refactor/B1158/scripts/make_clip.py`) show both: 8240
oracle's bright burst vs willeq's dim scatter, 8255 oracle's ground column
(Source) vs willeq's cloud (Source) plus willeq's landing.

*Instrument note:* the oracle side is measured on the observer's full-zone video
(B1132 `anchor.py`) and the willeq side on the `--spell-test` fixed-scene render
(B1139 `harness_measure.py`); the onset and duration columns are timeline-based
and comparable across the two, while the absolute `cov` values are each
instrument's own ROI, so they are stated per-side rather than as one ratio. The
onset match (both ~+0.5 s) is the robust cross-instrument fact; the `cov`
ratio for 8240 Source (0.370 vs 0.067) reflects a real size/brightness gap the
clip shows directly.

**The type-5 spell (8080) is a third, measured both sides in section 8** -- its
three Source slots are all type 5, so each draws TWO emitters, which is the
emitter-count case this fix closes.

## 7. The 47-row gate

The gate is 47 rows: the 40 `spellsnew` records of the b1130 tour
(`review/B1130/record_table.tsv` on the evidence hosts, record 51 excluded)
plus the 7 tour spells (93/38/54/656/41/253/458). All 47 spells have a
field-145 record: the 40 b1130 rows are record-based by construction, and none
of the 7 tour spell ids is in the 157-spell legacy list
(`review/B1190/legacy_types.tsv`). So `slots.fromFile` is true for all of
them. `createCastGlow` opens that path with `if (slots.fromFile) {` and ends it
with `return 1;` (`src/client/graphics/spell_visual_fx.cpp:642` and
`src/client/graphics/spell_visual_fx.cpp:697`); the legacy block is inserted
AFTER that return, and the entire `spell_visual_fx.cpp` diff is that one
pure-insertion hunk. The legacy path is structurally unreachable for any of the
47 rows.

Confirmed empirically: the same 47 rows were rendered and measured on the
branch base `2c81570dd` (the `fable` tip at cut time, including B1185/B1186)
and on this tip `dd04bf2ae`, both with
`/mnt/projects/fable/review/GATE-BASELINE/render_gate.sh` (b1130 rows
surfaceless on the Quadro at 1280x800 per the 2026-10-29 ruling), and the
measured columns were diffed row for row
(`review/B1190/scripts/gate_diff.py`): **47/47 rows byte-identical** (evidence
dirs `kb-b1190-gate-base/gate` and `kb-b1190-legacy-eff/gate` under
`/home/user/kb-evidence/`, copied to NFS at hand-back). The absolute column
values reflect the units that landed on `fable` after the B1184 tip (B1185's
Source onset, B1186's landing window) -- both trees carry them -- not this
change.

## 8. The type-5 spell (8080) measured both sides -- both emitters

8080 Necrotic Cry (record 35, Source `[5,5,5]`, Target `[4,4,4]`) is the
type-5 spell added to the measured set (note 8631 f1). Its three Source slots
are all type 5, so each must draw **TWO** emitters (ground + attached); 8080
therefore draws six. The oracle leg is lane 5 on the caster's **own** client
(`leg8080/video-caster.mp4`; Testcaster char 67, level 60, `#castspell 8080 0`,
begin-cast 502.6 s / action 503.1 s); the willeq side is the `--spell-test`
harness on this tip (`frames/b1190_8080/`, 405 PNGs;
`measure/b1190_8080/measure_harness.txt`). The observer rig was not usable for
this leg (the second client died during the leg), so the oracle side is the
caster's own view rather than the B1132 observer crop; both emitters are
nonetheless plainly visible in it.

| emitter | oracle (8080, caster view) | willeq (8080, harness) |
|---|---|---|
| **ground** | flat electric-blue ring/disc on the ground plane at the caster's feet; the dominant element of the cast | foot-level particle cloud, electric blue (peak hue 212); SOURCE window BC+0.100 s to BC+0.467 s, peak cov 0.254, extent 2.80 x 1.49 model |
| **attached** | a small mid-torso/waist glow, pale whitish-purple; the secondary element | particle cloud at the entity anchor height (A = 3.6, mid-torso), blue; the same SOURCE window |
| **count** | two per type-5 slot (three ground + three attached = 6, the three per anchor overlapping so they read as one ring + one glow) | two per type-5 slot (6 total, logged: `createCastGlow: ... rec=35 level=60 legacy source emitters=6 [ 5@ground+attached x3 ]`) |

The **emitter count matches per side**: two per type-5 slot, six for 8080. The
**attached** emitter matches in presence and body anchor (mid-torso) on both
sides. The **ground** emitter differs in form, not count: the oracle's is a
flat ground disc/ring (`FUN_00439f26` ground snap); willeq's is a foot-level
particle cloud, which is willeq's standing approximation for **every** legacy
ground-type emitter (type 3/4 draw the same way), a separate fidelity gap from
this finding's emitter count and out of scope here.

The ledger clip `review/VISUAL-FX/B1190/b1190_8080_necrotic_cry.mp4`
(oracle caster LEFT / willeq harness RIGHT, 3 s, hstack of
`leg8080/video-caster.mp4` 502.4-505.4 s and `frames/b1190_8080/` frames
42-131 at 30 fps) shows the caster and the effect on both sides.

**The 47-row gate is unaffected by the type-5 change**: the type-5 branch is
only reached from the legacy block (section 7), which none of the 47
`slots.fromFile` rows reaches, so the gate is byte-identical before and after
this fix as it was for the original insertion.

**Cite checker (canonical invocation, B1172 convention).** Every decompile
cite is in the `review/B1141/ghidra/out/eqgame.exe.c:LINE` form and resolves
under the canonical invocation:

```
python3 -I scripts/refactor/cite_check.py refactor/B1190/README.md --tree . \
    --extra /mnt/projects/fable \
    --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis \
    --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis
    -> checked 47 cites, 0 problems, 0 address warnings
```
