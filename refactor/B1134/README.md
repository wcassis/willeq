# B1134 (#454) FX-SURVEY-OBSERVER: records 40 and 51, measured by a second player character

Evidence: `/mnt/projects/fable/review/B1134/README.md` (legs, tables, strips, the lane census).

## What changed

- **The observer rig** (`scripts/eqemu/oracle/`, documented in `docs/oracle_driver.md` "The observer"):
  - `oracle_remote.sh`: `# observer=<Character>` runs a second official client (the observer, `:99`, its own client
    copy and Wine prefix) beside the plan's login character (the caster, on a session Xvfb `:98`, recorded in
    `xvfb-caster.pid` and the only Xvfb stop kills); `obs <verb> ...` rows; `# obs-ini-set=`; `# fps=N`;
    `# caster-video=1`; several `# ini-set=` rows (B1130/B1137 rig copies folded in); `client_focus` bounded.
  - `oracle_session.sh`: the observer's inventory in the snapshot, its zone + `# observer-pose=`, its census before/after
    (must match), HP/mana/endurance/PvP of both characters restored with the world down; `# provision=<Character>`,
    the one-time leg that creates the observer and is the only leg allowed to leave its account and character.
  - `oracle_class_leg.sh`: skills, disciplines and persistent timers saved before the class switch and put back exactly
    (B1137's skills copy, made general); `--pre-sql-file`.
  - `oracle_login_patch.py`: `--display`, `--credentials` (the copy SOURCES the observer's credentials file; it never
    holds a secret). `oracle_report.py`: `obs` rows. `oracle_observer_setup.sh`: new.
  - Tests: `class_leg_safety.sh` (the save/restore SQL), `remote_stop_safety.sh` (the caster Xvfb), new
    `login_patch_test.sh` (ctest `OracleLoginPatch`, hygiene).
- **The rule** (`refactor/EXECUTOR_PROTOCOL.md`, `docs/oracle_driver.md`): an oracle FX leg's other target and its
  camera are a SECOND PLAYER CHARACTER, never a spawned NPC; "unmeasurable" is never a finding, a rig limit is.
- **`config/spell_effects.json`**: `sourceRecords` row 51 (measured, no longer record 13's stand-in), `targetRecords`
  rows 40 and 51 (measured, no longer `unmeasured`), `animRecords.rows` 326/328/329/330 -> 51. Regenerated with
  `review/B1131/scripts/gen_source_records.py`, `review/B1132/scripts/gen_target_records.py` and
  `review/B1137/scripts/gen_anim_rows.py 93=6 328=51 329=51 330=51`; every other row is byte-identical to the
  generators' output. Pinned by `tests/test_spell_source_anchors.cpp`, `test_spell_target_anchors.cpp`
  (`ThePortAndDisciplineRowsAreTheObserversNumbers`), `test_spell_anim_rows.cpp`
  (`TheMeasuredDisciplineRecordsTakeRowFiftyOne`), `tests/spell_anim_rows.h` ({51, 326}). No new config map, so
  ConfigKeyReachability is unchanged.

## Findings

1. **Ports (record 40)**: the landing is drawn round the CASTER with a light column over its head, from OP_Action, and
   it ends with the caster's model (Act+0.72-0.75; the caster zones 0.16 s after OP_Action, the observer's
   OP_DeleteSpawn arrives Act+0.36-0.40). Nothing stays at the spot.
2. **Disciplines (record 51)**: the real `/disc` path (OP_CastSpell slot 10 -> OP_Action, no OP_BeginCast) draws NO
   sprite on the observer; a 0-ms OP_BeginCast draws the Source (hand glows, then a flame curtain at the feet to
   ~Act+8.2). The record has no Target stage. The Titanium client keeps one reuse timer for all disciplines.
3. **Rig check (93 on the observer)**: Source onset, Source lateral extent and landing centroid in the B1131/B1132 bands;
   Source `hi`, landing `end` and coverage out, and out the same way in B1130 r51's own framing recorded from the
   caster's display (leg P2b), so not made by the observer rig.

## FOLLOW-UP (not done here)

1. **fable draws a landing half the oracle does not** for record 51's `/disc` path: `SpellVisualFX::createLanding`
   always creates the `spellComplete` half from the Source-stage art at OP_Action, whatever the Target stage holds
   (`src/client/graphics/spell_visual_fx.cpp`, the "impact half" gate covers only the impact). The oracle draws nothing
   for a `/disc` (no OP_BeginCast). Gate it in the B1138 harness.
2. Record 325 (Leechcurse Discipline, shadow knight, 3 spells: other textures and particle lifetimes than 326) was not
   cast and stays on the default rows; record 1's combat abilities (24 spells, row 6) were not cast through the
   combat-ability path.
3. 93's landing on a PLAYER (both views, P2/P2b) ends ~Act+4.7 with far lower coverage than on B1130's `#spawn`ed NPC
   (Act+6.7); and the Source `hi` reads 1.5-1.76 against r51's 1.25 on both renderers. One leg casting 93 on the
   observer AND on a spawned NPC from the same pose would separate target type from the rest.
4. The lane-5 accounts census: leg P2 ended 66 on one new row, `character_currency 103`, written by the provisioned
   character's first save (P1 had created it before any save). Expected once; every later leg restored byte-identically.
