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

(executor fills: the column-shape decision and why; the marker handling
and where interpretation happens; regeneration proof — row counts, spot
checks, file size delta; **the consumer-behaviour report, which is the
most important part**; what the restored `test_spell_database.cpp` tests
now assert; deviations)

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
