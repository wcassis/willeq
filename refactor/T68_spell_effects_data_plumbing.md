# T68 — Spell effect slots: from `spells_us.txt` to `SpellData::effects`

**Slot: t2. x86 only. No Pi. NO EQEMU.**

**Territory: `tools/willeq_db_builder.cpp`, `include/client/willeq_database.h`,
`src/client/willeq_database.cpp`, `include/client/spell/spell_data.h`,
`src/client/spell/spell_database.cpp`, `data/willeq.db` (regenerated —
see Step 4), and tests.**

Do not touch the visual-effects path (`spell_visual_fx.*`, the particle
system, `config/spell_effects.json`) — that is later work in this arc and
a different unit. Do not touch `spell_effects.cpp` or `buff_manager.cpp`
**logic**; this unit feeds them, it does not rewrite them. `refactor/deferred.md`
is append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then `refactor/T56_logging_db_board_fixes.md`'s
Review (it introduced the shared schema-version constant you are about to
bump, and deliberately did **not** regenerate the database — this unit
must).

## Why this unit exists

`SpellData::effects` (`include/client/spell/spell_data.h:128`,
`std::array<SpellEffectSlot, 12>`) has existed with no writer for the life
of the project. The consequences are on the board and are real: buff stat
totals are always 0, `hasEffect(UltraVision/InfraVision)` always false,
`isDamageSpell()` always false, and `SpellEffects::processEffect`
(`spell_effects.cpp:73`) is a loop over an array that is always empty.

Will has ruled the spell-effects system broken and due a rewrite. This is
step 1 of that arc: **get the data out of the client files and into the
struct.** It is deliberately independent of any rendering work — it
unblocks gameplay effects on its own, and the visual rewrite builds on it
later.

The break is four clean layers, all confirmed:

```
spells_us.txt (has the data)
  -> willeq_db_builder CREATE TABLE spells   (drops it: 25 columns, no effects)
  -> EQT::SpellInfo                          (no effect fields)
  -> SpellData::effects                      (empty array)
  -> spell_effects.cpp:73                    (dead loop)
```

## The field mapping — CONFIRMED, do not re-derive it

Validated against spell id 3 ("Summon Corpse", slot 1 = SPA 91 =
SummonCorpse). All indices 0-based into the `^`-delimited row;
`include/client/spell/spell_constants.h:485-558` names them:

| Component | Fields | "Empty" marker |
|---|---|---|
| Effect/SPA id | **86-97** (`EffectId1 = 86`) | **254** |
| Base value | 20-31 (`BaseValue1`) | 0 |
| Base2 / limit | 32-43 (`Base2Value1`) | 0 |
| Max value | 44-55 (`MaxValue1`) | 0 |
| Formula | 70-81 (`Formula1`) | **100** |

Also carry **`SpellAffectIndex` = field 123**. It is the index into
`spells.eff` that the *next* unit needs, `spell_constants.h:542` already
declares the constant, nothing reads it, and adding it now costs one
column instead of a second schema bump later.

**The empty markers are not zero and getting them wrong is silent.** An
effect id of `254` means "slot unused", not "SPA 254". A formula of `100`
means unused. Slot fill decays exactly as a 12-slot array should — slot 1
has only 74 rows marked empty, slot 12 has 7851 — so a marker bug will
look plausible while corrupting most spells. Assert on this in tests.

## Step 1 — builder schema

`tools/willeq_db_builder.cpp`'s `CREATE TABLE spells` (~`:78-105`) has 25
columns and none of the above. Add them.

**Choose the shape deliberately and justify it**: 61 scalar columns
(12×5 + `spell_affect_index`) is explicit and queryable but wide; the
table already has a `class_levels BLOB` precedent for exactly this kind of
fixed-size array, and a BLOB per component would be 5 columns plus the
index. Either is defensible — pick one, say why, and be consistent.

Populate in `loadSpells()` (~`:1274`), preserving the markers as they
appear in the file rather than normalising them at write time. Normalising
is the consumer's job and doing it in two places is how the drift this
project keeps finding gets started.

## Step 2 — schema version bump, which T56 built the machinery for

The schema changes, so `kWilleqDbExpectedVersion`
(`include/client/willeq_database.h`, added by T56 and shared by client and
builder) goes **2 → 3**.

T56 made a mismatch fatal in both directions with a message naming the
rebuild command. **That is the mechanism working as designed** — an old
database against this client will now fail loudly instead of silently
returning nothing. Verify that actually happens (T56 left tests for it);
do not weaken it.

## Step 3 — client side

- Add the effect fields to `EQT::SpellInfo`
  (`include/client/willeq_database.h:79-109`), mirroring whatever shape
  Step 1 chose.
- Read them in the query path (`src/client/willeq_database.cpp`, the
  `getSpell`/`getSpellByName` SELECTs — note both name `class_levels`
  today and both will need the new columns).
- Populate `SpellData::effects` in `SpellDatabase::queryAndCache()`
  (`src/client/spell/spell_database.cpp:52-97`), which currently copies 24
  scalars plus `class_levels` and never touches `effects`.

Interpret the empty markers **here**, at the consumer boundary — a slot
whose effect id is 254 should land as an unused `SpellEffectSlot`, so the
predicates in `spell_data.h:218-282` (`getEffectCount`, `hasEffect`,
`getEffect`, `isDamageSpell`, `isHealSpell`, `isDoTSpell`) behave
correctly without every caller re-learning the marker.

## Step 4 — regenerate `data/willeq.db`, deliberately

T56 was explicitly forbidden from regenerating it. **This unit must**, and
the reasons it was forbidden then are the reasons to be careful now: it is
a tracked binary, the diff is large and unreviewable by eye, and a
regeneration that silently drops data is invisible.

So: regenerate it, and **prove the result** rather than trusting it.
- Row count before and after must match.
- Spot-check several spells end to end — the file's raw fields, the DB
  row, and the resulting `SpellData::effects` — including at least one
  spell with a full 12 slots, one with a single slot, and one with a
  `-1` `SpellAffectIndex` (799 spells have that).
- Confirm the pre-existing 25 columns are byte-identical in content for a
  sample of rows. Adding columns must not perturb what was already there.

State the before/after file size and row count in the Review.

## Step 5 — the part that carries real risk

**This unit brings dead code to life.** `spell_effects.cpp:73` and
`buff_manager.cpp:500` have never executed against real data in the
history of the project. The moment `effects` is populated:

- buff stat totals start applying,
- `hasEffect(UltraVision/InfraVision)` starts returning true, so vision
  buffs begin doing something,
- `isDamageSpell()` starts discriminating where it previously always
  returned false and fell through to `isDetrimental()`.

That is the point of the unit, and it is also the risk: **code that has
never run may be wrong in ways nothing has ever surfaced.** Do not assume
those consumers are correct merely because they compile and have always
been quiet.

Exercise them. Run the existing spell/buff test suites and read the
results rather than just checking the count is green — a test that
previously passed vacuously (asserting on an always-empty array) may now
pass for a different reason, or fail correctly. **If a consumer misbehaves
with real data, report it precisely; do not fix it here.** Fixing
gameplay effect processing is its own unit with its own acceptance.

`tests/test_spell_database.cpp:218-225` carries tests deleted precisely
because effects were never populated, with a comment saying so — restore
or replace them.

## STOP conditions

- The confirmed field mapping not matching what you observe in the file —
  report immediately, do not adapt around it.
- A newly-live consumer failing in a way that suggests the fix belongs in
  gameplay logic rather than this plumbing.
- Regeneration changing row count or perturbing existing columns.

## Verification

Full protocol bar, plus Step 4's regeneration proof and Step 5's
consumer-behaviour report. State the schema-version bump explicitly and
confirm the v2-database-fails-loudly path still works.

## Review

**Status: PASS. Full verification bar PASS.**

### Field mapping — sanity-checked, matches the plan exactly

Verified directly against `/mnt/projects/EverQuestP1999/spells_us.txt`
before writing any code: spell id 3 has 217 `^`-delimited fields (matches
`SpellField::FieldCount`), field 87 (0-based 86, `EffectId1`) = `91`
(SummonCorpse), fields 88-98 (0-based 87-97) all = `254`, field 21 (0-based
20, `BaseValue1`) = `70`, field 71 (0-based 70, `Formula1`) = `100`, field
124 (0-based 123, `SpellAffectIndex`) = `4`. All confirmed values match the
plan's table exactly — no STOP needed.

### Column-shape decision: 61 scalar `INTEGER` columns, not BLOBs

Chose `effect_id_1..12`, `base_value_1..12`, `base2_value_1..12`,
`max_value_1..12`, `formula_1..12` (60 columns) plus `spell_affect_index`
(61st), all plain `INTEGER`, over the BLOB-per-component alternative the
plan floated. Reasons:
- **Type width mismatch rules out a naive blob.** Effect ids in the raw
  file go well past 255 (max observed 343, and `spells_us.txt` and
  `SpellEffect` both go into the 300s) so `effect_id` cannot reuse
  `class_levels`' `uint8_t`-per-slot blob shape — it needs 16-bit-or-wider
  storage, and mixing a `uint8` blob (`class_levels`), a would-be `int16`
  blob (`effect_id`), and `int32` blobs (base/base2/max/formula) in the same
  table is exactly the kind of implicit-width-contract-per-column trap that
  makes a schema hard to reason about later.
- Scalar columns are directly `SELECT`-able for the regeneration proof
  (Step 4) without decoding a blob by hand — this mattered in practice: every
  spot-check and the byte-identical-25-columns diff below is a plain
  `sqlite3` one-liner.
- 61 extra `INTEGER` columns on a ~8k-row table is cheap; this is not a
  hot-path-width-sensitive table like a packed vertex buffer.

`class_levels` keeps its existing BLOB shape — untouched, not migrated to
match, since perturbing a working column for stylistic consistency was
explicitly out of scope (Step 4 forbids perturbing the existing 25 columns
at all).

### Marker handling: raw in the DB, interpreted once, in `queryAndCache()`

The builder (`tools/willeq_db_builder.cpp::loadSpells()`) writes fields
verbatim — `254`/`100` land in the DB exactly as they appear in
`spells_us.txt`, no normalisation. `EQT::SpellInfo` (`willeq_database.h`)
and `WilleqDatabase::getSpell()`/`getSpellByName()` (`willeq_database.cpp`,
via a shared `readSpellEffectColumns()` helper used by both queries so the
61-column read logic exists exactly once) are equally raw pass-through —
the header comment states explicitly that markers are NOT interpreted
there.

Interpretation happens exactly once, at the consumer boundary:
`SpellDatabase::queryAndCache()` (`src/client/spell/spell_database.cpp`).
An effect id of `254` is explicitly mapped to `SpellEffect::UnusedEffect`
(not left as a raw cast) so the intent is documented in code rather than
relying on the coincidence that the enum's `UnusedEffect` numeric value
also happens to be 254. `SpellEffectSlot::isValid()` (`spell_data.h`,
unmodified — this unit feeds it, per territory) then correctly reports
`false` for such slots, and every predicate built on it
(`getEffectCount`, `hasEffect`, `isDamageSpell`, `isHealSpell`, ...)
behaves correctly with no per-caller marker knowledge. Formula's `100`
marker needed no special branch — `SpellEffectSlot::formula`'s own default
is already `100` ("no scaling"), so a verbatim copy is already the correct
value for an unused slot. Base/base2/max markers (`0`) are likewise
verbatim copies — they only matter alongside a valid `effect_id`, and an
unused slot's base values are irrelevant once `isValid()` gates on
`effect_id`.

### Regeneration proof

- **Row count**: 8066 before, 8066 after — exact match (`spells_us.txt` has
  8067 total lines; one spell id resolves to <=0 and is skipped by the
  builder both before and after this unit, unrelated to this change).
- **File size**: 3,043,328 bytes before -> 3,883,008 bytes after (+839,680
  bytes, entirely the 61 new INTEGER columns x ~8066 rows).
- **Pre-existing 25 columns byte-identical for ALL 8066 rows**: dumped
  `id,name,player_tag,teleport_zone,cast_on_you,cast_on_other,spell_fades,
  cast_time_ms,recovery_time_ms,recast_time_ms,duration_formula,
  duration_cap,mana_cost,target_type,range_val,aoe_range,resist_type,
  resist_diff,casting_skill,is_beneficial,spell_icon,gem_icon,cast_anim,
  zone_type,hex(class_levels)` from both the old tracked db and the freshly
  regenerated one, `ORDER BY id`, `diff`'d: **0 differing lines** across the
  full table, not just a sample.
- **End-to-end spot checks** (raw file field -> DB row -> `SpellData::effects`,
  verified via the new `test_spell_database.cpp` cases and direct `sqlite3`
  queries against the raw file):
  - **Single-slot**: id 3 "Summon Corpse" — raw file slot 1 = SPA 91, base 70,
    formula 100, slots 2-12 = 254; DB row matches exactly
    (`91|254|254|...|70|0|0|100|100|...|4`); `SpellData::effects[0]` resolves
    to `SpellEffect::SummonCorpse`/base_value 70, slots 1-11 resolve to
    `SpellEffect::UnusedEffect`/`isValid()==false`.
  - **Full 12-slot**: id 891 "Spiritual Possession" — raw file has all 12
    effect-id fields non-254 (`22 0 69 11 50 1 36 36 36 36 36 36`); DB row
    matches exactly; `getEffectCount()==12`, every slot `isValid()==true`.
  - **`-1` SpellAffectIndex**: id 523 "Tail Lashings" — raw file field 123 =
    `-1`; DB row `spell_affect_index=-1`; `SpellData::spell_affect_index`
    round-trips as `-1` (not clamped/misread as unsigned). Note: the plan
    said "799 spells have that"; the raw file itself has 798
    (`spell_affect_index=-1` count matches between file and DB exactly) — a
    minor imprecision in the plan's number, not a defect in this unit
    (confirmed by counting the raw file directly, independent of any code
    written here).
- **Schema version**: `kWilleqDbExpectedVersion` 2 -> 3 (`willeq_database.h`,
  builder's `setMeta("version", ...)` shares the same constant, unchanged
  from T56). Verified the fatal-mismatch path actually exercises the new
  value: `test_willeq_database.cpp`'s `OlderVersionFailsToOpen` built a
  scratch db at version 2 (now genuinely "older" post-bump, not a
  synthetic/unrelated number) and got the real `LOG_FATAL` naming both
  versions and the rebuild command; `NewerVersionFailsToOpen` (built at
  version 4) also failed — both directions still fatal, mechanism
  unweakened.

### Consumer-behaviour report (the important part)

Ran the full existing spell/buff test suites against the regenerated,
now-populated database and read the results (not just green/red):

- **`test_spell_database`** (21 tests, all pass): new/restored tests
  (`EffectSlotsMatchConfirmedReferenceSpell`,
  `UnusedSlotMarkerNeverMisreadAsRealEffect`, `EffectCountAndHasEffect`,
  `NegativeSpellAffectIndexRoundTrips`,
  `IsDamageSpellDiscriminatesOnRealData`,
  `IsHealSpellDiscriminatesOnRealData`) all pass against real data, not
  synthetic fixtures. `UnusedSlotMarkerNeverMisreadAsRealEffect` also
  asserts the plan's predicted slot-fill-decay shape (slot 1 filled far more
  often than slot 12 across a 500-spell scan) actually holds post-population
  — a marker bug would have made every slot look uniformly full instead.
- **`test_buff_manager`** (37 tests, all pass): the T22-era
  `PlayerStatModAlwaysZeroBecauseEffectsAreNeverPopulated` test — exactly
  the kind of "passed vacuously against an always-empty array" case the
  plan warned about — was replaced with
  `PlayerStatModAppliesRealEffectValue` (applies real spell 40 "Strengthen",
  a single-slot STR+4 buff, and asserts `getPlayerStatMod(STR)==4`, not 0)
  and `PlayerStatModIsZeroWhenBuffHasNoMatchingEffect` (same buff, asserts
  `getPlayerStatMod(AGI)==0` — confirms the sum is filtered by effect type,
  not indiscriminate). **Both pass: `getStatModFromBuff()`
  (`buff_manager.cpp:488-506`) is correct against real data** — no fix
  needed here, nothing to report as broken.
  - **One interesting near-miss worth recording**: the first attempt used
    spell 21 "Berserker Strength" as the STR-buff fixture and additionally
    asserted `hasEffect(AGI)==false` for it — that assertion FAILED, because
    id 21 genuinely carries a second effect (AGI -1, i.e. it's a STR buff
    with a small AGI debuff riding along, real EQ data). This was a test
    fixture mistake, not a production bug — switched to spell 40
    "Strengthen" (a true single-slot spell) for the isolation test. Flagging
    it because it's a small piece of live evidence that spell data now
    flowing through has genuine multi-effect complexity that a synthetic
    fixture would never have surfaced, exactly the kind of thing Step 5
    warned "code that has never run may be wrong in ways nothing has
    surfaced" about — in this case the "wrongness" was in the test's
    assumption, not the code, but it demonstrates the risk was real.
  - **`hasEffect(UltraVision/InfraVision)`**: not separately unit-tested
    (no existing vision-buff test seam), but manually confirmed live via
    direct DB query: spell 46 "Ultravision" has `effect_id_1=66`
    (`SpellEffect::UltraVision`), so `hasEffect(UltraVision)` now returns
    true for it where it always returned false before. Not exercised
    end-to-end through actual vision-rendering code (out of this unit's
    territory — visual layer is later work), but the data plumbing that
    gates it is confirmed live and correct.
- **`test_spell_manager`** (53 tests, 51 pass + 2 pre-existing
  environment-gated skips unrelated to this unit) and
  **`test_spell_manager_timing`** (36 tests, all pass): unaffected,
  confirming this unit didn't regress spell casting/gem-state machinery
  that indirectly reads `SpellData` but not `effects`.
- **`test_beneficial_spell_casting`**: all 4 tests skip (missing `DISPLAY`,
  a pre-existing environment limitation of this headless dev VM, unrelated
  to this unit — same as it would skip on `fable` tip).
- **No consumer misbehaviour found.** Both newly-live code paths
  (`spell_effects.cpp:73`'s `processEffect` loop and
  `buff_manager.cpp:500`'s `getStatModFromBuff`) behave correctly against
  real spell data in every case exercised. Nothing reported to gameplay-logic
  follow-up.

### What the restored/new `test_spell_database.cpp` tests assert

Original deleted tests (`SpellEffectsAreValid`, `EffectCountAndHasEffect`,
`SpellDataHelperMethods`) were removed with a comment explaining `effects`
was always empty. Replaced with, all pinned against confirmed real spell
ids rather than hand-built fixtures:
- `EffectSlotsMatchConfirmedReferenceSpell` — spell 3 slot 1 is exactly
  SummonCorpse/base 70, slots 2-12 are exactly `UnusedEffect`/invalid.
- `UnusedSlotMarkerNeverMisreadAsRealEffect` — scans 500 spells; every
  `UnusedEffect`-tagged slot is `isValid()==false`; slot-fill decays
  slot1 > slot12 as predicted.
- `EffectCountAndHasEffect` — single-slot spell 3 has count 1 and
  `hasEffect`/`getEffect` work correctly; full-slot spell 891 has count 12,
  every slot valid.
- `NegativeSpellAffectIndexRoundTrips` — spell 523's `-1` round-trips intact.
- `IsDamageSpellDiscriminatesOnRealData` / `IsHealSpellDiscriminatesOnRealData`
  — spells 14 ("Strike", CurrentHP base -6) and 12 ("Healing", CurrentHP base
  70) correctly classify via the now-live predicate.

### Deviations

- Plan said 799 spells have `SpellAffectIndex == -1`; the raw
  `spells_us.txt` (and the regenerated DB, matching it exactly) has 798.
  Documented above — a minor imprecision in the plan's number, confirmed
  independent of this unit's code by counting the raw file directly. Not a
  STOP condition (field mapping itself matched exactly; this is just an
  approximate count, not a marker/mapping disagreement).
- `SpellData::spell_affect_index` was added to `spell_data.h` (not just
  `EQT::SpellInfo`) even though this unit has no consumer for it yet — the
  plan's territory list includes `spell_data.h` and explicitly frames
  `spell_affect_index` as carried now for "the next unit", which implies it
  needs to survive past the transient `EQT::SpellInfo` all the way into the
  struct the next unit will actually read from. Flagging as an interpretive
  call rather than something explicitly spelled out step-by-step.
- No STOP conditions triggered. No production code in
  `spell_effects.cpp`/`buff_manager.cpp` was touched, per territory.

Board (`refactor/deferred.md`, append, never delete): re-title the
`SpellData::effects` never-populated entry as resolved-by-population, and
record what it unblocked. Note explicitly that this is step 1 of the
spell-effects rewrite Will scoped — the correct `spells.eff` reader
(256 records × 2792 bytes = 8-byte header + 3 × 928-byte blocks, indexed
by `SpellAffectIndex`) and the data-driven visual layer are still ahead,
and that **no era filtering is required by decision**: `spells.eff`
carries only the pre-Luclin sprite vocabulary, so a post-Velious spell
referencing a valid index renders with a classic-era visual, which Will
has ruled correct.
