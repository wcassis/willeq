# B1131 (#451): the Source (cast) stage per spells.eff record

Fix card from the B1130 survey (#450). Evidence: `/mnt/projects/fable/review/B1131/` (index: its `README.md`).

## 0. Result

B1122 drew every spells.eff record's Source as one body cloud from BeginCast+0.25 s, released 0.33 s after
OP_Action. Now each record draws what the official client draws:

- **Rows.** `config/spell_effects.json` carries a `sourceRecords` row for each of the 41 records the survey
  measured (`review/B1130/anchor_table.tsv`). The classes are hands 8, feet-ring 4, orbit 13, body 14 and head 1.
  Record 51 is unmeasured (disciplines, not gem-castable) and carries record 13's row: the only other record whose
  Source names GENK10.
- **What a row holds.** Anchor class; measured shape (lit extent lo..hi, half-width, centroid, and record 22's disc
  band); onset; hold when the target is another entity; hold when the target is the caster.
- **Lane-5 gate.** Seven representatives (records 2, 1, 4, 9, 0, 5, 22), self and other target.
  - Every anchor class draws on its own anchor.
  - Every other-target onset is within +0.06..+0.09 s of the oracle's.
  - The other-target hold matches the oracle on records 2, 0, 1 and 5 (lit to Act+4.6-6.0). b8c1719f stopped them
    all at Act+0.33.
  - Lateral extent is in band for ring, orbit, body and head.
  - Misses are listed in sec. 4.
- **Tour.** The 7-spell B1118 tour shows no landing regression beyond the ±2-3 % capture spread. Sprite sizes are
  byte-identical to !280. One Source ROI row (93) reads 0.004 under its band (sec. 4).

## 1. Config (`sourceRecords`)

| key | meaning |
|---|---|
| `scale` | B1120's caster height 6.5 and entity anchor 3.6 world units; orbit 3.0 rad/s (B1122's record-23 orbit) |
| `population` | a record's rate = 80/s x its oracle Source coverage / record 0's (0.170). Record 0 (93/38) keeps B1122/B1123's calibrated rate. |
| `default` | a record the survey did not reach, and a spell with no record: the style's own body cloud. Onset 0.41 (median of 40), holdOther 4.0 (median of 28), holdSelf 0.33. |
| `overrideRecords` | spells_us.txt field 123 of every spell with an override or exception row (93/38 0, 41 20, 54/656 23, 253 35, 458 33) |
| `records.<N>` | anchor, lo, hi, halfWidth, centroid, coverage, onset, holdOther (absent = self-only spell), holdSelf, and `capture` (the B1130 capture and the frames the hold rests on) |

- **Generator.** The rows come from `review/B1131/scripts/gen_source_records.py`.
- **Hold rule.** holdOther is the last oracle frame on which the caster's Source is still lit with another target. A
  lower bound is used as measured.
- **Record 35's hold.** It is 1.1 s (the arcs are seen apart only to Act+1.1). The survey counted 35 among its 16
  records at >= 4 s, but its later caster-ROI run is the NPC's frame-filling whirl, as on records 21/33/26. So 15
  rows hold >= 4 s.
- **holdSelf = 0.33 on every row.** No record separates Source from landing on a self cast. B1122 measured exactly
  that merged case.
- **Removed from both Source styles:** B1122's `anchor`, `triggerDelay` and `releaseDelay`. Sizes, lifetimes, art
  and alphas are untouched.

## 2. Code

- **`SpellEffectsConfig`** (`spell_effects_config.{h,cpp}`):
  - `SourceRecord`, `sourceRecord(record)`, `sourceHold(record, self)`, `overrideRecord(id)`.
  - Shape/rate-override queries.
  - The pool rule (sec. 3).
- **`SpellPresets::SourceForRecord / SourceForSpell / SourceAnchorForSpell`** (`spell_emitter_presets.cpp`) build
  the emitters over the castGlow/castSpray style. Per class:
  - **hands:** one emitter per hand bone, POINT-born at the hand. The spray's mean is the measured centroid and its
    p5..p95 spans the measured box. A product of two uniforms has p95 0.70, so the velocity spread is the half-span
    over 0.70 x life.
  - **feet-ring:** a RING of the measured radius at the feet, rising lo->hi over one life.
  - **orbit:** ORBITAL at the measured radius over lo..hi. Records 4 and 33 are spirals: born at lo, rising to hi.
  - **body:** B1122's BOX cloud over the measured box, with the style's rise.
  - **head:** a new `SpawnShape::DISC` (uniform over the disc's area) at the head bone, over discLo..discHi with
    discShare 0.52 of the sprites (the survey's onset head-band share), plus a column falling from it to lo.
  - **Heights.** Every height is placed relative to the followed point: the bone height is read at creation.
  - **Overrides.** A spell whose override sets its own `spawnShape` (B1122's 54/656/41/253/458) keeps that geometry
    on the entity, and one that sets `spawnRate` keeps that rate.
- **Head bone pseudo-entity** `SpellVisualFX::headEntityId()` (bit 14). It is resolved in `renderer_entities.cpp`
  through `EQAnimatedMeshSceneNode::findHeadBoneIndexForFx()`, which is first_person_eye.h's `<code>he` rule.
- **`createCastGlow`:**
  - takes the record from the noted vocabulary (`sourceRecordOf`);
  - every Source emits until OP_Action;
  - the cap is the cast plus the record's longest hold.
- **`releaseCastGlow(caster, spell, target)`:**
  - holdOther when `target` is another entity; holdSelf when it is the caster or 0;
  - releases the caster, both hands and the head;
  - the bridge passes `SpellCastVisualCompleteData::targetId`.
- **Interrupts** still free everything at once (`removeCastGlow`, now with the head id).

## 3. Pool (keys unchanged: 8192 desktop, 4096 sbc)

The rule is still "a landing plus its own held Source peaks at half the pool".

- **Per record.** The held Source is now its RECORD's: its rate, for its longest hold. A spell on the style landing
  is scaled for its own record (`getStyleSettings(style, id, record)`, `SpellPresets::Impact/SpellComplete(…, record)`).
- **New in the rule.** Where the landing's own schedule plus the held Source does not fit at all, the Source scales
  by that same factor first and the landing fills the rest (`sourcePoolScale`, `ScaleBothToFit`). Without this, 253's
  1950/s Source would have left its sbc landing ~230 sprites.

| landing peak (expected) | 93/38 | 54/656 | 41 | 253 | 458 |
|---|---|---|---|---|---|
| desktop | 4019 | 3070 (exception 3517; record 23 holds 2.0 s) | 2492 | 4096 | 373 |
| sbc | 1970 | 1229, Source x0.79 | 1774, Source x0.92 | 1539, Source x0.71 | 373 |

- **On the gate** (desktop) 54/656's landing coverage read +1.9 / +12.7 %, not lower. The held Source sits in the
  same pixels.
- **On sbc** 54/656/41/253 now draw a thinner Source than the B1122 calibration. That is the pool, not a
  measurement: Will's population call (B1123 FOLLOW-UP 1).

## 4. Not met, stated plainly

- **Onset, absolute.** Records 0 and 22 (and 1 and 4 on self casts) read BC+0.54-0.56 against the card's
  0.37-0.49. Every other-target row reads 0.06-0.09 after the oracle's.
  - The emitters start at the oracle's onset exactly.
  - The lag is the threshold rule on a cloud that builds from zero.
  - Shifting every onset 0.08 earlier would be a fable-side calibration, not done.
- **Record 4's centroid** is 0.57 against 0.82. The oracle's spiral rises over its 4 s cast, and fable's spiral is
  steady.
- **Rising emitters (ring, spiral) read their tops 0.3-0.5 low.** The sprite's alpha fades with age.
- **Hand sprays are symmetric and narrower than the oracle's.** 202's oracle plume is one-sided, and 93 reads
  ±0.4 against ±0.6.
- **Record 22's lit run reads to Act+2.65.** Its emitter stops at Act+0.31, and the strip is empty by Act+3.0. The
  oracle's end is hidden by its glare from Act+0.5.
- **Tour 93 Source ROI p90: 0.067, band 0.071-0.097.** The size is unchanged, but the hand spray on a 1.5 s druid
  cast stays below `source_all.py`'s threshold (Source coverage unreadable). 38, the same record, stays in band.
- **Source coverage** is 25-60 % of the oracle's on most records: the population question of B1123 FOLLOW-UP 2. The
  per-record rates scale record 0's calibration by the oracle's coverage ratio, and nothing more.

## 5. Tests (`tests/test_spell_source_anchors.cpp`, new; plus updated pins)

- **`SourceRecordRows`:**
  - all 41 rows, the class counts (8/4/13/14/1), 51 = 13, 15 long holds;
  - the seven reps' oracle numbers typed from the survey;
  - the default row.
- **`SourceAnchors`** (real SpellVisualFX -> ParticleManager -> SimulationWorker, with the caster, hand and head
  positions fed back each frame):
  - each class follows its anchor (hands = the two hand ids, head = the head id, the rest the caster);
  - each record is shaped like its row: centroid, bottom, top and width; ring/orbit radius; the halo share at head
    height and the column; sprites born at the hands;
  - each starts at its onset;
  - each holds holdOther / holdSelf past `releaseCastGlow` and then stops;
  - an interrupt frees every anchor.
- **Updated** (the B1122 single-rule pins they replace are recorded in
  `review/B1131/tests/baseline_b8c1719f_source_pins.txt`): `TheSourceIsABodyWideCloud…`,
  `TheSourceWaitsForTheOraclesOnset`, `ASourceAndItsOwnLandingFitHalfThePool`, `EverySourceCloudFitsBesideALanding`,
  `TheSourceCloudRatesAreTheCalibratedOnes`, `EveryLandingPeaksAtTheRuleForThisPool`,
  `OneLandingReachesTheOraclesPeakSpriteCount`, `TheSourceHoldsPastTheAction…`, plus a `ConfigKeyReachability` map
  entry for the two id-keyed maps.
- **RED on b8c1719f:**
  - 13 tests fail on its config (`review/B1131/tests/red_b8c1719f_config.txt`);
  - the new TU does not compile on its code (`red_b8c1719f_code_compile.txt`).
- **Local** (Release, named targets): 308/308 in the spell/sim/particle subset. Hygiene is 45/48; the 3 failures are
  the `*Reproducible` tests, whose generator tools were not built.
- **CLAUDE.md:** the Source sentence is rewritten (59,990 bytes).

## 6. FOLLOW-UP (orchestrator)

1. **fable draws `spellComplete` (half the landing) on the CASTER when the target is someone else.** The oracle draws
   the landing on the target only (B1130 "Target anchor"; record 27 is the one record whose landing really is on the
   caster). Seen on this gate: an other-target cast lights the caster to Act+~5 with the isolated Source off. That
   is why the hold was read on `cfg/source_isolated.json` legs.
2. **Rig: a `#spawn`ed NPC cannot be moved off the caster in fable.** `#goto` moves the player but not the camera,
   and `/warp` is undone by the server. The gate used Grennet Stormhand (zone NPC 85) as the other target.
3. **The lane-5 snapshot restore does not cover `character_memmed_spells`.** The gems were restored by hand inside
   the lock.
4. **Rising Source patterns over a long cast** (record 4's converging spiral, the centroid drifting 0.53 -> 1.0 by
   BC+2.9) are not modelled.
5. **Source populations** and the sbc Source factors in sec. 3: Will's population call.
