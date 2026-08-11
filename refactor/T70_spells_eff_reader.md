# T70 — A correct `spells.eff` reader, indexed by `SpellAffectIndex`

**Slot: t2. x86 only. No Pi. NO EQEMU.**

**Territory: a new reader under `src/client/spell/` +
`include/client/spell/`, and its tests.**

**Do NOT wire this into rendering.** No changes to `spell_visual_fx.*`,
the particle system, or `config/spell_effects.json`. This unit produces a
validated reader and nothing else; consuming it is the next unit and
mixing the two would make both unreviewable. Do not touch the spell
database path — T68 just landed it. `refactor/deferred.md` is append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then `refactor/T68_spell_effects_data_plumbing.md`'s
Review (it carried `spell_affect_index` through the schema specifically so
this unit could index by it).

## Why this unit exists

Step 2 of the spell-effects rewrite. T68 got the spell *mechanics* data
into `SpellData::effects`. This unit reads the client's *visual* effect
definitions so a later unit can render them.

Today the entire visual vocabulary is 10 resist types × 5 schools
(`spell_visual_fx.cpp:190-219` and `:879-918`) — every fire spell in the
game renders identically. `spells.eff` is what the real client used
instead, and nothing in this codebase reads it.

## The format — established by direct parse, not inference

**`/mnt/projects/EverQuestP1999/spells.eff` is 714,752 bytes = 2792 × 256
exactly. 256 fixed-size records. No file header. No index.**

Per record (2792 = 8 + 3 × 928):

| Offset | Size | Content |
|---|---|---|
| 0 | 8 | two int32 — record 0 is `(1,0)`, all others sampled `(0,0)` |
| 8 | 928 | emitter block 0 — **Source** (caster) |
| 936 | 928 | emitter block 1 — middle / travel |
| 1864 | 928 | emitter block 2 — **Target** |

Each 928-byte block opens with **four 32-byte fixed strings**: three
sprite names plus an attach-point tag. Record 0 reads:

```
blk 0: ['GENC10_SPRITE', 'GENC30_SPRITE', 'GENC10_SPRITE', 'Source']
blk 1: ['GENC30_SPRITE', '',              '',              '']
blk 2: ['GENC20_SPRITE', 'GENC10_SPRITE', 'GENG00_SPRITE', 'Target']
```

Ints at block+128 appear to be per-layer indices, with `-1` meaning
unused.

**The record index IS the effect id.** It is not stored in the file — it
is the position, and it is what `SpellAffectIndex` (field 123, now carried
through the DB and `SpellData` by T68) references. Field 123 holds 58
distinct values, all within `[-1, 88]`; **798 spells use `-1`** (no
visual). Record 88 has content, record 255 is all-zero padding — so the
file is roughly 89 real effects padded out to 256.

## What the parked branch got wrong — do not repeat it

`task/anim-video-capture`'s sibling, branch `fix-spell-effects` commit
`48ae032b`, contains a `spell_effect_loader.cpp` that parses this file.
**Do not port it wholesale.** It gets 928 right and everything around it
wrong:

- It missed the **8-byte per-record header** and the fact that blocks come
  in **groups of three**. A flat 928-byte walk drifts 8 bytes per record
  and desynchronises immediately — which is precisely why it needed a
  **32-byte sliding scan** hunting for `"Source"`/`"Target"` marker
  strings to keep re-finding its place. With the real layout, no scanning
  is needed at all: seek to `index * 2792`.
- It **synthesises effect ids from an incrementing counter**. That is the
  most damaging error: the ids it produces are an artifact of scan order
  and cannot be indexed by `SpellAffectIndex`. The record position is the
  id.
- Its parameter parsing is self-described guesswork —
  `// Parameter layout (based on reverse engineering)`, "approximately at
  offset+500", `+520`, `+760` — with sanity clamps
  (`duration > 0 && duration < 60000`) that **silently fall back to
  defaults when a guess is wrong**. Nothing detects that. See the next
  section.

**Worth salvaging from it:** the `effectNameToTexture()` mapping
(`GEN[A-Z][0-9]0_SPRITE` → `spel[a-z][0-9].tga`), which is small, pure,
and carries the only tests on that branch that actually run. Verify the
mapping against the real filenames in
`/mnt/projects/EverQuestP1999/SpellEffects/` rather than trusting it.

## The discipline that matters most: structure vs semantics

The **structure** above is proven — record size, block offsets, string
layout, and the index-is-the-id relationship all fall out of arithmetic
that divides exactly and content that reads cleanly.

The **semantics of the numeric parameters are not.** Nobody has
established what the ints beyond the name block mean. The parked branch
invented meanings for them and hid the invention behind clamps.

So: **parse what you can prove, and expose the rest as raw bytes or
explicitly-named unknowns.** A field you cannot demonstrate the meaning of
should not be given a confident name like `particles_per_second` — that
name will be believed by the next reader and by the rendering unit that
consumes this.

Where you *can* establish meaning, show your working: cross-reference
several records, correlate against the spells that reference them, and say
what evidence supports each interpretation. Where you cannot, say so
plainly. **"Bytes 128-928 of each block are not yet understood" is a
perfectly good outcome for this unit** and is far more useful than
plausible fiction.

## Tests — the branch's were tautological, yours must not be

`48ae032b`'s tests assert things like "textures start with `spel` and end
with `.tga`" — trivially true because the function under test constructs
them that way — and skip entirely when the data file is absent (5 of 7
skipped in this environment because the test hardcoded an unreachable
path). **Nothing anywhere asserted that a specific spell maps to its
correct effect.**

Do better:

- **Record 0 is a known fixture.** Its nine sprite-name slots and two
  attach tags are written out above. Assert them exactly.
- Assert the file's shape: 256 records, `size % 2792 == 0`, record 255 is
  all-zero padding, record 88 has content.
- **Assert the index relationship end-to-end**: take real spells whose
  `spell_affect_index` T68 now stores, look up that record, and assert the
  reader returns a populated effect for a valid index and nothing for
  `-1`.
- Gate on the data file's presence the way other asset tests do, but make
  the gate **honest** — if it skips, the test output must say so clearly
  rather than passing silently. Use the real path
  (`/mnt/projects/EverQuestP1999`), not a hardcoded one from another
  machine.

## STOP conditions

- The file failing to divide as described — report immediately; the whole
  design rests on it.
- Any urge to wire this into rendering.
- Any urge to name a parameter whose meaning you cannot evidence.

## Verification

Full protocol bar. Additionally: state how many of the 256 records contain
real content, the distinct `SpellAffectIndex` values actually referenced
by spells in the shipped `data/willeq.db`, and whether every referenced
index resolves to a populated record — **a referenced index that lands on
empty padding would be a genuine finding**.

## Review

**Status: DONE.** Reader implemented and tested, not wired into rendering.
`include/client/spell/spells_eff_reader.h` +
`src/client/spell/spells_eff_reader.cpp` + `tests/test_spells_eff_reader.cpp`
(18 tests, all passing). Commit: see branch `task/T70-spells-eff-reader`.

### Confirmed layout, as implemented

Matches the plan exactly, verified by direct parse of
`/mnt/projects/EverQuestP1999/spells.eff` (714,752 bytes):

- 256 records x 2792 bytes, no file header, no index. `size % 2792 == 0`,
  `size / 2792 == 256` — divides exactly, as claimed.
- Per record: 8-byte header (two int32, `header_a`/`header_b`) + 3 x
  928-byte emitter blocks (`[0]`=Source, `[1]`=middle/travel, `[2]`=Target).
- Per emitter block: four 32-byte fixed strings (3 sprite-name slots +
  1 attach tag) at offset 0, then 800 bytes (offset 128..928) of unknown
  content.
- **The record index is the id, not stored in the file** — `getRecord(int)`
  seeks by `index * 2792`, no scanning, matching the plan's core claim
  about `48ae032b`'s error.
- Record 0 fixture matches the plan's stated content **exactly** (asserted
  byte-for-byte in `Record0MatchesKnownFixtureExactly`): header `(1, 0)`;
  Source = `[GENC10_SPRITE, GENC30_SPRITE, GENC10_SPRITE, "Source"]`;
  middle = `[GENC30_SPRITE, "", "", ""]`; Target =
  `[GENC20_SPRITE, GENC10_SPRITE, GENG00_SPRITE, "Target"]`.

### Proven vs. left unknown

**Proven** (each backed by direct inspection of the real file, not
inference):

- Record size/count/offsets (arithmetic divides exactly; see above).
- The three sprite-name slots + one attach-tag string per emitter block
  (32 bytes each, NUL-terminated) — exposed as
  `SpellsEffEmitter::sprite_names[3]` / `attach_tag`.
- **The attach tag has THREE distinct non-empty values across the whole
  file, not two**: `"Source"`, `"Target"`, and `"Sprite"` (found in 5
  emitter blocks across records 23, 26, 44, 45, 46 — always in the
  middle/travel emitter, never Source or Target). The plan's format
  section only mentions Source/Target; this is a genuine addition to the
  documented layout. Exposed as a raw `std::string`, not an enum, for
  exactly this reason.
- The record-index == SpellAffectIndex relationship, confirmed end-to-end
  against `data/willeq.db` (see Index resolution below).

**Left explicitly unknown** (bytes 128..928 of each emitter, 800 bytes):
No semantics assigned. The plan's own hypothesis — "ints at block+128
appear to be per-layer indices, with -1 meaning unused" — was tested
directly (dumped the first 10 int32s at that offset across several
records, e.g. record 0/block 0 has all 3 name slots populated but reads
`(-1, -1, -1, 0, 2, 2, 0, 0, 0, 0)`, not a clean per-slot -1/non-`-1`
split) and **did not hold up**, so it is not implemented or named. The
region is exposed as raw `unknown_bytes` (800-byte array) on
`SpellsEffEmitter`. It is demonstrably not constant/padding (see the
records-36/60 finding below), so a future unit with more leverage
(correlating against known-good client behavior, e.g. capturing a real
cast) may be able to narrow it — nothing here should be read as "this
region is unimportant."

### `effectNameToTexture` verification against real filenames

Salvaged from `48ae032b`, then verified (not trusted) against
`/mnt/projects/EverQuestP1999/SpellEffects/` (324 files). All 41 distinct
name tokens observed across all 256 records were checked:

- **`GEN[A-Z][1-9]0_SPRITE` → `spel[a-z][1-9].tga`: 12/16 nonzero-digit
  cases resolved to a real file** (e.g. `GENC10_SPRITE`→`spelc1.tga`,
  `GENU10_SPRITE`→`spelu1.tga` both exist). Two confirmed
  counterexamples: `GENA30_SPRITE`→`spela3.tga` and
  `GENY10_SPRITE`→`spely1.tga` — neither file exists (only `spela1/2`,
  `spely2/3/4/5` do). The reader still constructs these names (texture
  *existence* is a rendering-unit concern), but the mapping is now known
  to be ~75% verified for nonzero digits, not 100%.
- **The "00"-digit form NEVER resolves: 0 of 16 observed `GENx00_SPRITE`
  names matched any real file** (`spela0.tga`, `spelc0.tga`, etc. all
  absent). `effectNameToTexture` deliberately returns `""` for digit `'0'`
  rather than constructing a name known not to exist — a correction to
  the parked branch, which would have silently returned a dead filename.
- Four non-`"GEN..."` tokens found in the file (`GEN_Z_SPB`,
  `I_SNOWFLAKESPRITE`, `I_TFIRE_SPB`, `I_TSMOKE_SPB`) have no evidenced
  mapping; `effectNameToTexture` returns `""` for these too.

### Record-content census

Two different, both-legitimate definitions of "has content" were checked
against each other and diverge by exactly 2 records:

- **By sprite-name/attach-tag content** (what `hasContent()` implements):
  **90 of 256 records** have at least one populated name/tag slot.
- **By raw-byte difference from the canonical padding template** (byte 8
  onward of record 88, which is byte-identical to record 255 and 162
  other records): **92 of 256 records** differ from that template.
- The 2-record gap is real, not a bug: **records 36 and 60** have empty
  name/tag slots in all three emitters, but differ from the padding
  template in the unknown byte region (8 bytes differing at record 36,
  133 bytes at record 60) — i.e. they carry *some* non-template numeric
  content with zero visible sprite content. This is exactly the kind of
  finding the "unknown region is not simply padding" claim above rests on.
- `SpellsEffReader::populatedRecordCount()` implements the name-based (90)
  definition, since it is the one directly provable from understood
  fields; the byte-template comparison is not exposed as API (it depends
  on an implicit "record 88/255 is the template" assumption that is
  itself only evidence, not a documented file feature) but is recorded
  here since it changes the record-88 finding below.
- Correcting the plan text: **"record 255 is all-zero padding" is false.**
  No record in the file is literally all-zero — every record, including
  255, carries the same non-zero filler pattern (starting at byte 136,
  0xFFFFFFFF repeated in the observed samples) past the name-slot region.
  Tests assert the provable claim (no sprite/tag content) instead.

### Index resolution — the finding the plan asked this unit to look for

`data/willeq.db`: **58 distinct `spell_affect_index` values** referenced
across all spells (including `-1`), **798 spells use `-1`** (no visual) —
matches the plan's numbers exactly.

**Correcting the plan text: record 88 does NOT have content.** The plan's
format section states "record 88 has content, record 255 is all-zero
padding" as an already-established fact. Direct inspection shows record 88
is **byte-identical to record 255** past the 8-byte header — both are the
same padding template. This was checked because 88 is the highest
`spell_affect_index` value referenced by any spell in the shipped
database.

Cross-referencing all 58 distinct referenced indices against real vs.
padding records: **every referenced index resolves to a record slot in
range** (no out-of-bounds references — `EveryReferencedIndexResolvesToARecordSlot`
passes), but **5 of the 58 referenced indices land on empty padding
records**, affecting **148 spells total**:

| SpellAffectIndex | Record content | Spells affected |
|---|---|---|
| 70 | empty padding | 109 (e.g. "Poison Breath", "Black Plague", "Trakanon's Poisonous Breath") |
| 71 | empty padding | 1 ("Flame Jet") |
| 80 | empty padding | 6 (e.g. "Leviathan Tsunami", "Engulfing Whirlpool") |
| 84 | empty padding | 5 ("Heated Blade", "Burning Blade", "Blazing Blade", "Flaming Blade", "Inferno Blade") |
| 88 | empty padding | 27 (the Xalirilan's/Iony's/Reebo's Augury/Discombobulation/Exorcism/Extrication/Cleansing families) |

This is a genuine finding, not a reader bug: `KnownEmptyPaddingIndicesAreConfirmedEmpty`
and the positive-control `KnownRealIndicesAreConfirmedPopulated` (indices
0,1,2,3,10,20,30,50, all populated) together rule out `hasContent()` being
broken — real indices show content, these five specifically do not. Any
later rendering unit consuming this reader must decide what "no visual
data for a referenced spell" means (likely: fall back to the existing
resist/school defaults) rather than treating a null/empty record as an
error.

### What the tests actually assert (18 tests, `test_spells_eff_reader.cpp`)

- File shape: exact size, exact division into 256 x 2792 (not "close to").
- Record 0's full fixture (all 9 sprite-name slots + both non-empty
  attach tags), asserted with `EXPECT_EQ` against literal strings, not
  loose pattern checks.
- Record 255 has no sprite content (the provable claim; NOT "all zero").
- Record 88 has no sprite content despite being the highest referenced
  index — the genuine finding above, pinned so a future spells.eff drop
  (or a reader regression) that changes it is caught either way.
- Out-of-range and `-1` (`SpellAffectIndex`'s own marker) both return
  `nullptr` from `getRecord`.
- A truncated file is rejected with a load failure and non-empty error
  message (`LoadFailsLoudlyOnWrongSize` — actually truncates a real copy
  and exercises the failure path, not merely asserted-never-to-trigger).
- Missing file fails gracefully with an error string, `getRecord` returns
  null.
- `effectNameToTexture`: verified positive cases (5 real files), the two
  confirmed counterexamples (documented, not silently treated as bugs),
  the always-empty "00" digit form, and non-GEN tokens all return `""`.
  None of these assert "starts with 'spel'" — every case names a specific
  filename that was independently checked to exist or not exist.
- Index resolution end-to-end against `data/willeq.db`: spell 523
  (`spell_affect_index = -1`, confirmed by T68's own test) resolves to
  null; spell 68 ("Bolt of Flame", `spell_affect_index = 0`) resolves to
  the record-0 fixture; spell 3 ("Summon Corpse", `spell_affect_index =
  4`, NOT 0 — see deviation below) resolves to a record independently
  confirmed populated; every one of the 58 distinct referenced indices
  resolves to an in-range record slot; the 5 empty-padding indices and 8
  known-real indices are asserted by content presence/absence.
- All file/DB-dependent tests gate via `GTEST_SKIP()` with a message
  naming the exact path checked (`EQ_CLIENT_PATH`/`WILLEQ_DB_PATH`
  overridable, same pattern as `test_combat_music.cpp` /
  `test_spell_database.cpp`) — never a silent pass. In this environment
  both `data/eq_client` (symlinked to
  `/mnt/projects/EverQuestP1999`) and `data/willeq.db` are present, so all
  18 tests ran for real, none skipped.

### Deviations from the plan

1. **Plan text error, corrected above**: "record 88 has content, record
   255 is all-zero padding" is wrong on both halves — 88 is empty padding
   (byte-identical to 255), and no record is literally all-zero. Not a
   deviation in what was implemented, but the plan's stated evidence was
   re-derived rather than trusted, per the plan's own instruction to
   verify rather than assume.
2. **Test used spell id 3 with `spell_affect_index = 4`, not 0.** The
   plan implies spell id 3 ("Summon Corpse") is a natural test subject
   (it's T68's reference spell for the effects-slot mapping), but its
   actual `spell_affect_index` in `data/willeq.db` is 4, not 0. Queried
   directly rather than assumed; spell 68 ("Bolt of Flame") was used
   instead for the index-0 case, and spell 3 for the index-4 case (its
   record independently verified populated).
3. Exposed the "Sprite" attach-tag value and the byte-template
   record-content discrepancy (records 36/60) as additional findings not
   called out in the plan's format section — both are within scope
   (structural facts about the file), neither invents new field
   semantics.
4. `effectNameToTexture` deliberately diverges from `48ae032b`'s version:
   it excludes digit `'0'` (returns `""`) since zero verified real files
   exist for that form, where the original would have constructed a name
   unconditionally.

None of these required a STOP: the file divided exactly as described (the
one hard STOP condition), and no parameter was given an invented name.

Board (`refactor/deferred.md`, append, never delete): record this as step
2 of the arc, with what remains — the data-driven visual layer, which is
the bulk. Note separately, as its own small item, that
`tools/model_viewer_spell_bar.h:274` has `ENTRY_SIZE = 256` for
**`spellsnew.eff`** where the correct value is **268**
(`112024 / 268 = 418` exactly; 256 gives 437.59 and drifts), which is why
that tool carries "skip entries that look like fragments" hacks — the
fragments are drift artifacts. Deliberately **out of scope here**: it is a
different file, a different format and a different consumer, and folding
it in would muddy this unit's evidence.
