# B1132 (#452): the Target (landing) stage per spells.eff record

Fix card from the B1130 survey (#450), after B1131 (#451, the Source). Evidence:
`/mnt/projects/fable/review/B1132/` (index: its `README.md`).

## 0. Result

Until now fable drew every landing one way: B1120's volume, with the impact half on the target and the
spellComplete half on the CASTER. An other-target cast lit the caster for ~5 s and gave the target half a landing.

- **Rows.** `config/spell_effects.json` carries a `targetRecords` row for each of the 41 records: whole body 35
  (29 on the target, 6 on the caster), head 1 (record 5), feet/ground 2 (16, 28), column above 1 (3), and the
  default row for 40 (zone change) and 51 (not castable).
- **Code.** `SpellVisualFX::createLanding(caster, target, spell)` draws both landing halves on the row's anchor.
  That is the target's entity, its head bone or its feet, or a column over it. The caster gets the landing only on a
  caster row or a cast with no target. The bridge's cast-animation arm calls it.
- **Lane-5 gate** (target = Grennet Stormhand, 1.4 caster heights away, as on B1130):
  - Every rep landed on its anchor. Record 5 follows Grennet's head bone; 16, 12 and 22 follow his entity.
  - Record 5 is in band on every measure. Its glow starts at the face, then a head-and-shoulders cluster, as the
    oracle draws it.
  - Record 16 draws rings at the feet climbing the legs. Its centroid, onset and width are in band; its bottom and
    top are not (sec. 4).
  - Records 12 and 22 are the style volume, now on the target. Their onset (22), centroid and width are in band;
    their bottom is low and 12's top is high (sec. 4).
  - The record-22 caster tail is the Source alone: lit to Act+2.6-2.8, which equals B1131's Source-isolated reading.
    B1131's shipped config read Act+4.8-5.0.
- **Tour.** The B1123 7-spell tour shows no landing regression beyond capture spread. Six rows are within +-3 %. 41
  reads -4.1 % on a self cast whose code path did not change.
- **Sprite sizes** are byte-identical.

## 1. Config (`targetRecords`)

| key | meaning |
|---|---|
| `records.<N>.anchor` | `body` (the style volume on the target), `caster` (that volume round the caster whatever the target), `head`, `feet`, `column` |
| `lo`, `hi`, `halfWidth`, `centroid` | the oracle landing RUN: h p5..p95, (dx p95 - p5)/2, centroid, in target heights above the target's feet |
| `coverage`, `onset`, `end` | `record_table.tsv` landing_cov / landing_onset_s (RISE) / landing_end_s |
| `population` | shaped rows only: min(1, coverage / record 0's 3.945). Body rows keep the B1123 populations |
| `faceOnset` (5) | Act+0.41, the frame the glow lights the target's face (e5 f8233) |
| `boneAt` (5) | 0.90: the height the head bone stands for (sec. 2) |
| `unmeasured` (40, 51) | the default row stands in |
| `capture` | the B1130 capture, spell, target_anchor text and the RUN line the row came from |

- **Generator.** The rows come from `review/B1132/scripts/gen_target_records.py`.
- **Caster rows.**
  - Record 27 is measured: its PB-AE landing is round the caster, not the NPC it hits.
  - 8, 9, 11, 19 and 43 are self-only spells, and their row reads "(caster".
  - 6 and 42 are group bard songs whose row reads "(caster" from a self cast only. They stay on the packet's target,
    which on that cast IS the caster. A group member's landing is unmeasured.
- **Record 3's extent** is the capture crop (lo 0, hi 1.82, half-width 1.52). Its wash saturates the NPC ROI (a
  1-frame RUN).
- **Existing overrides keep their geometry** (54/656 record 23, 41 record 20, 253 record 35, 458 record 33,
  93/38 record 0). All of them are body rows; no survey row contradicts them.

## 2. Code

- **`SpellEffectsConfig`:**
  - `TargetAnchor`, `TargetRecord`, `targetRecord(record)`, `targetRecordCount()`, `targetReferenceCoverage()`;
  - `loadTargetRecords()`.
- **`SpellPresets::TargetForRecord / TargetForSpell / TargetAnchorForSpell`** (`spell_emitter_presets.cpp`). Body and
  caster rows return the style unchanged. Shaped rows keep the style's sizes, lifetimes, alphas, onset delay and
  emission schedule, and set where sprites are born and how they move:
  - **head.** The impact half is a small glow AT the head bone (the face). The spellComplete half is a still cloud
    over lo..hi x +-halfWidth.
  - **feet.** The impact half orbits as a ring of radius halfWidth at lo. The spellComplete half is a ring at lo
    rising to hi over one life.
  - **column.** The spellComplete half falls from hi to lo over +-halfWidth; the impact half fills lo..hi.
- **The head row is placed relative to the head bone alone.** Its offsets are `(h - boneAt) x 6.5`. The first gate
  binary (b2ad407c) placed it from the bone's height above the entity's assumed feet. On Grennet, a taller model than
  the B1120 caster scale, it read centroid 0.73 against 0.87; bone-relative placement reads 0.80. boneAt 0.90 is the
  bottom of anchor.py's head band, where B1130 reads the face; fable's head bone is at the face (onset tiles).
- **`SpellVisualFX::createLanding`** chooses the entity: the caster for a caster row or target 0, else the target.
  For a head row it follows `headEntityId(target)`. B1131's head pseudo-entity already resolved any animated
  entity's head, so no renderer change was needed. It keeps T72's Target-stage gate on the impact half and T441's
  art routing:
  - the spellComplete half takes the Source stage's sprites;
  - the impact half takes the Target stage's sprites.
- **`irrlicht_bridge.cpp`**: `createSpellComplete(d.casterId) + createImpact(d.targetId)` became
  `createLanding(d.casterId, d.targetId, d.spellId)`. `createSpellComplete`/`createImpact` stay for the model-viewer
  harness.

## 3. Tests

- **`tests/test_spell_target_anchors.cpp`** (new; registered for graphics-on and headless in `tests/cmake/spells.cmake`).
  - Rows (headless):
    - 41 rows; class counts 35 whole body (6 caster), head 1, feet 2, column 1, unmeasured 2;
    - the oracle numbers of records 5, 16, 28, 3, 27, 12, 0 and 22;
    - 14 and 7 not head;
    - populations;
    - the default row.
  - Bridge census: the arm calls `createLanding`, never `createSpellComplete(d.casterId`.
  - Seam (SpellVisualFX -> ParticleManager -> SimulationWorker, caster and target 1.4 H apart):
    - each record lands on its anchor (head bone, target, caster for 27);
    - an other-target cast never has an emitter following the caster, and its landing is centred on the target;
    - a self cast or no-target cast lands on the caster;
    - records 5/16/28 are shaped like the oracle RUN;
    - record 5 starts at the face;
    - 16/28 rings sit at the feet;
    - record 3 falls from above the head.
- **`tests/test_caster_own_landing.cpp`**: a self-cast `createLanding` survives the success repeat's teardown (B400).
- **`test_config_key_reachability.cpp`**: `targetRecords.records` is a data-keyed map.
- **RED on 4a5246c1:**
  - 7 of 11 fail on its config (`review/B1132/tests/red_4a5246c1_config.txt`);
  - the TU does not compile on its headers: 63 errors, no `createLanding`/`targetRecord`
    (`red_4a5246c1_code_compile.txt`, which also quotes its bridge arm).
- **Local** (Release, named targets): 21 spell/particle/bridge binaries, 213 tests, 0 failures. Hygiene is green
  except the three `*Reproducible` tests, whose generator tools were not built (as on B1131).
- **Whole tree, CI flags** (`cmake --build -j 4 -- -k`): graphics-on GLES2 + tests, and `EQT_GRAPHICS=OFF` + tests.
  Logs are in `review/B1132/tests/whole_tree_*.log`.
- **CLAUDE.md:** the Source/landing sentence now names both record blocks (59,957 bytes; three narrative clauses were
  trimmed).

## 4. Not met, stated plainly

- **The style volume's bottom (records 12 and 22, every body row).** On Grennet it reads h p5 0.06-0.08 with a
  ground band of 0.11. The oracle reads 0.22-0.27 with ground 0.00; no NPC landing in the survey lights below 0.19.
  - Cause: B1120's column is fed at the feet, and until now that half was drawn on the caster.
  - Body rows keep the B1120 geometry because the tour is calibrated on it. Reshaping them per row is a follow-up.
- **Record 12's top** is 1.55 against 1.29: record 0's column (2.5 caster heights). Its coverage is 0.44-0.48
  against 0.90, and record 22's 0.44 against 1.18. Both are population questions (B1123 FOLLOW-UP 2).
- **Record 16's band** is 0.02..0.76 against 0.21..1.03.
  - Bottom: the ground rings sit at lo in world height, but their near side projects below the feet at pitch 12.
  - Top: the rising ring fades with age, the same low top B1131 measured on rising emitters.
- **Record 5** reads 0.07 below the oracle's centroid, and its head share is 0.25 against 0.45. Every measure is
  within band.
- **Record 5 rep 1 is void.** A zone NPC (spawn 87) cast spell 11 on Grennet at Act+2.48.
- **Not gated live:**
  - records 28 and 3 (their spells are AE: 73 Gravity Flux at level 39+, 377 Icestrike);
  - record 27's caster anchor.
  All three are pinned headlessly only.
- **Fixed scale.** Every row uses B1120's 6.5-unit scale, not the target's own height. Grennet is taller; the head
  row avoids this by following the bone, but the feet and column rows do not.
- **Art.** The spellComplete half still draws the Source stage's sprites (T441), now on the target. Whether the
  oracle's target landing uses Target-stage art for both halves is not settled here.
- **Tour.** 41 reads -4.1 % on an unchanged self-cast path (capture spread). 93's Source ROI is 0.062, short as on
  B1131.
- **Rig.** The DRU leg's capture has 23 single dropped frames (67 ms gaps).

## 5. FOLLOW-UP (orchestrator)

1. Per-row body landing shapes (the bottom and top above), and the target-height scale for feet and column rows.
2. The Target-stage art for the spellComplete half on another target.
3. The group bard songs (6, 42) with another group member, and the AE records 28 and 3, need an oracle and fable leg.
4. fable opens the Tip of the Day window on every gate leg. It sits inside B1118's tour crop (x 720-990); this
   gate's rep camera avoids it.
