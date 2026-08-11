# T73 — Validate (or refute) the recovered `spells.eff` parameter table

**Slot: t3. x86 only. No Pi. NO EQEMU.**

**Territory: `include/client/spell/spells_eff_reader.h`,
`src/client/spell/spells_eff_reader.cpp`, `tests/test_spells_eff_reader.cpp`,
and a throwaway analysis script under a scratch path.**

**Changes to the reader must be purely ADDITIVE.** Sibling T72 is live in
slot2 and consumes this reader read-only for the visual layer; do not
change existing field names, types, or behaviour. Do not touch
`spell_visual_fx.*` or anything under `src/client/graphics/` — T72 owns
it. `refactor/deferred.md` is append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then **`refactor/T70_spells_eff_reader.md`'s
Review in full** — it established the record model you must use and
explains precisely why it refused to name these fields.

## What this unit is

T70 proved the structure of `spells.eff` and deliberately left an
800-byte region per block as raw `unknown_bytes`, because nobody could
demonstrate what those bytes mean and the parked branch's invented names
were hidden behind clamps that silently defaulted.

A reverse-engineering document has since been recovered from a deleted
branch's worktree. It claims to decode exactly that region:

```
git show archive/fix-spell-effects-20260811:.agent/spells_eff_format.md
```

| Claimed offset | Type | Claimed meaning |
|---|---|---|
| `0x274` | int32 | duration (ms) |
| `0x278` | int32 | fade-in (ms) |
| `0x27C` | int32 | fade-out (ms) |
| `0x280` | float | min scale |
| `0x288` | float | max scale |
| `0x294` | int32 | particles per second |
| `0x298` | float | particle lifetime (s) |
| `0x370` | float[12] | colour keyframes |

**Your job is to validate or refute this, not to adopt it.** A confirmed
table unblocks fully data-driven spell visuals. A refuted one is equally
valuable — it closes off a plausible-looking dead end that would otherwise
be adopted later by someone with less context.

## Why the document deserves scepticism

- **Its structural model is wrong.** It describes a flat sequence of
  928-byte blocks, paired Source/Target, with the effect id **incrementing
  per pair**. T70 proved the file is **256 fixed 2792-byte records** —
  8-byte record header plus **three** 928-byte blocks — and that the
  **record index is the id** (`714752 = 2792 × 256` exactly). The document
  also has no account of T70's middle/travel block at all.
- **It ships "validation ranges"** (duration 1–60000, scale 0.01–100,
  particles 1–1000…). That is the same sanity-clamp pattern that let the
  companion branch code silently fall back to defaults whenever an offset
  guess was wrong, with nothing detecting it. Ranges that "validate" a
  guess are how a wrong offset survives review.

## Why it nonetheless deserves testing

Two independent signals, neither claimed by the document itself:

- **Its block-internal layout agrees with T70** — layer names at
  `0x00`/`0x20`/`0x40`, block-type marker at `0x60`. Block-internal offsets
  are independent of how blocks are grouped into records, so the parameter
  table can be right even though the record model is wrong.
- **The colour keyframes terminate exactly at the block boundary**:
  `0x370 + 48 = 928`. A 12-float array ending precisely at the block edge
  is unlikely to be coincidence.

Note also what it does **not** explain: `0x80`–`0x274` is a **500-byte
gap** between the end of the marker string and the first claimed
parameter. Whatever lives there is unaccounted for by both T70 and this
document.

## Method — the one rule that matters

**Parse raw and report distributions. Never clamp during validation.**

Clamping is precisely what hid the original errors: a wrong offset that
gets clamped into a plausible range looks correct forever. Read the claimed
fields across **all populated records** and report what is actually there.

Signals that a claimed offset is right:
- **Type coherence.** Claimed floats should decode as sane finite floats,
  not denormals, NaNs, or absurd magnitudes. Claimed int32 durations
  should be small non-negative numbers, not random 32-bit noise.
- **Distribution shape.** Real durations cluster; random bytes do not.
  Report min/max/median and the distinct-value count per field.
- **Padding behaves as padding.** T70 established that records 88 and 255
  are the empty template. Their parameter region should read as
  zeros/defaults. If it reads as plausible durations, something is wrong.
- **Semantic correlation — the strongest test available.** Cross-reference
  against the spells that reference each record, using the
  `spell_affect_index` T68 carried into the database. If "duration"
  genuinely means duration, records referenced by long-lived buffs should
  differ systematically from those referenced by instant nukes. **Do this
  one properly; it is the difference between "the bytes look plausible"
  and "the field means what it claims."**

Also test the **500-byte gap** the document ignores: is it zeros, is it
structured, does it vary between records? Even a negative result narrows
the problem for whoever comes next.

## Landing the result

**If validated**: add the fields to the reader additively, with names that
match what you proved and comments citing the evidence. Leave anything
unproven as `unknown_bytes` — a partially decoded block is an honest
outcome. Do **not** add validation clamps; if a value is out of range that
is information the caller should see, not something to hide.

**If refuted**: say so, keep `unknown_bytes` intact, and record precisely
what you tested and what the data actually showed. Include the
distributions — the next attempt should start from your evidence rather
than re-deriving it.

Either way the test suite must pin the conclusion so it cannot silently
regress.

## STOP conditions

- Any non-additive change to the reader while T72 is live.
- Any urge to clamp, normalise, or "sanity check" values during
  validation.
- Any change to `spell_visual_fx.*` or the graphics tree.

## Verification

Full protocol bar, plus the analysis output: per-field distributions
across populated records, the padding-record check, and the
semantic-correlation result. **State the verdict per field, not just
overall** — it is entirely possible that some offsets are right and others
are not.

## Review

**Status: DONE. Mostly REFUTED, remainder UNDETERMINED. Nothing adopted.**

Analysis performed by direct raw parse of `/mnt/projects/EverQuestP1999/spells.eff`
(768 emitters = 256 records x 3 blocks; 151 name-populated, 617 padding) and
cross-reference against `data/willeq.db`, via a throwaway script at
`/tmp/claude-*/scratchpad/analyze_spells_eff.py` (not committed, per
"throwaway analysis script under a scratch path"). No clamping was applied
anywhere in the analysis — every number below is the raw decode.

### Per-field verdict

| Claimed field | Offset | Verdict | Evidence |
|---|---|---|---|
| `min_scale` | 0x280 | **REFUTED** | 102 of 151 populated emitters (68%) have `min_scale > max_scale` — the opposite of what a min/max bound pair should show in the majority case. Value set includes negatives down to -10.0, outside the document's own claimed 0.01-100.0 range. Pinned: `T73_ClaimedMinMaxScaleOrderingIsRefutedByMajorityViolation`. |
| `max_scale` | 0x288 | **REFUTED** | Same test; see above. |
| `color_keyframes` (float[12]) | 0x370 | **REFUTED as "colour"; array itself corroborated** | Real values reach 32.0 (record 10 Target, index 2 = 25.0), over 12x the document's own claimed [0.0, 2.0] range. BUT: the array's *location and size* are corroborated independently of the document — it terminates exactly at the 928-byte block boundary (`0x370+48=928`), padding records read a flat `(1,1,...,1)` template, and 39-43 distinct non-template patterns exist among populated records, always decaying from a burst value down to that same 1.0 baseline — a pattern far more consistent with a scale/intensity keyframe curve than an RGBA sequence. No field was named for this array (only "colour" is refuted, not its existence). Pinned: `T73_ClaimedColorKeyframeRangeIsRefutedByOutOfRangeValues`. |
| `duration_ms` | 0x274 | **UNDETERMINED (real, unnamed)** | No internal contradiction: always non-negative (0/151 negative), clusters at round ms values (0, 100, 250, 500, 750, 1000, 1500, 2000, 2500, 3000, 4000... — 21 distinct values across all 768 emitters), reads exactly 0 in every emitter of both confirmed padding records (88, 255). But the semantic-correlation test found **no relationship**: records referenced by long-lived buffs (`duration_formula != 0 AND is_beneficial=1`, 2762 spells, 38 distinct records) vs instant nukes (`duration_formula == 0 AND is_beneficial=0`, 1766 spells, 47 distinct records) show heavy record overlap (33 of 38 buff records are ALSO referenced by nukes) and no systematic separation (dedup-by-record median: buffs 750ms, nukes 1000ms — nukes numerically *higher*, the wrong direction if this were spell-buff duration). Also tested against `cast_time_ms` (a more scale-appropriate correlate, since both are ms): Pearson r = -0.088 across 57 distinct referenced records — no relationship. Magnitude alone (max 20000ms=20s across the whole file) rules out this being literal spell-buff duration, which in EQ runs to tens of minutes; it may be the particle effect's own animation length, but that is not established. |
| `fade_in_ms` | 0x278 | **UNDETERMINED (real, unnamed)** | Same structural profile: non-negative, 14 distinct values (0-4000ms), zero in both padding records. No independent semantic test available (no DB field to correlate against). |
| `fade_out_ms` | 0x27C | **UNDETERMINED (real, unnamed)** | Same profile: non-negative, 12 distinct values (0-3500ms), zero in padding. Weak internal consistency concern: `fade_in_ms + fade_out_ms > duration_ms` in 38 of 114 populated cases with `duration_ms > 0` (33%) — not decisive (fades could be independent/overlapping phases) but noted for the next attempt. |
| `particles_per_second` | 0x294 | **UNDETERMINED (real, unnamed)** | Non-negative, 9 distinct values (0-45), zero in padding. No semantic test available (would need a real particle-system capture). |
| `particle_lifetime` | 0x298 | **UNDETERMINED (real, unnamed)** | Non-negative float, 25 distinct values (0.0-50.0s), zero in padding. No semantic test available. |

### Distributions (all 768 emitters, no filtering, raw)

```
duration_ms          min=0 max=20000 median=0    distinct=21
fade_in_ms            min=0 max=4000  median=0    distinct=14
fade_out_ms           min=0 max=3500  median=0    distinct=12
min_scale             min=-10.0 max=32.0 median=0.0  distinct=24
max_scale             min=-10.0 max=20.0 median=0.0  distinct=15
particles_per_sec     min=0 max=45   median=0    distinct=9
particle_lifetime     min=0.0 max=50.0 median=0.0 distinct=25
```

Populated-only (151 emitters): same shape, non-zero medians (e.g.
`duration_ms` median 1000, `particle_lifetime` median 1.0) — consistent
with real, intentional data rather than noise, for all 7 scalar fields.

### Padding-record check

Records 88, 255, and 162 (T70's confirmed empty-name-content padding
template) all read **exactly zero** at every claimed scalar offset in every
one of their 3 emitters, and **exactly `(1,1,...,1)`** at the claimed
colour-keyframe offset. This is consistent with intentional default values
(additive fields default to 0, the multiplicative-looking field defaults to
1) rather than the padding template's ordinary filler pattern
(`0xFFFFFFFF`-repeated past byte 136, per the T70 Review) — supporting that
*something* real lives at these offsets, even where the specific claimed
name is refuted or unproven. Pinned: `T73_PaddingRecordsReadClaimedOffsetsAsZeroOrOneTemplate`.

### Semantic correlation (the strongest test, done properly)

See the `duration_ms` row above for the full method and numbers. Summary:
classified 2762 spells as long-lived buffs and 1766 as instant nukes via
`duration_formula`/`is_beneficial` from `data/willeq.db`; deduped to 38 and
47 distinct referenced records respectively (33 shared between both
classes); compared claimed Source-block `duration_ms` — no systematic
separation, wrong-direction median, and a Pearson r of essentially zero
(-0.088) against the more scale-appropriate `cast_time_ms` correlate
across 57 distinct records. **This is a negative result for the field
meaning "spell duration" as such; it does not rule out the bytes being a
real, different quantity** (e.g. the visual effect's own play length,
independent of the spell's mechanical duration — plausible given the ms
magnitude, but not established).

### The 500-byte gap (block offset 0x80-0x274)

Not simply zero: 0 of 768 emitters have this region all-zero. Nonzero-byte
count per emitter ranges 13-120 (median 32). Structured, not random noise:
the first 12-16 bytes are consistently `0xFF` repeated (matches T70's
"0xFFFFFFFF filler" finding for the padding template, extended into
populated records too), followed by small int32-looking values (e.g.
record 0 Source: `-1,-1,-1,2,2,-1,105,...`; record 4 Source:
`-1,-1,4,2,5,-1,103,16400,...`) that look like short index/flag fields
similar in shape to the block+128 "per-layer index" region T70 already
flagged as non-padding-but-unexplained. **Not decoded here** — the
gap remains `unknown_bytes`, but its first ~16-20 bytes are now known to be
a `0xFF`-filled run followed by small structured integers, not free-form
noise, which narrows the search space for a future attempt. Two additional
undocumented internal gaps were also found and are equally unclaimed by
both the document and this unit: block offset 0x284-0x288 (4 bytes,
between claimed min/max scale; mostly zero, 73/768 emitters nonzero) and
0x28C-0x294 (8 bytes, between claimed max_scale and particles_per_second;
mostly zero, 133/768 nonzero) and 0x29C-0x370 (212 bytes, between claimed
particle_lifetime and the colour/scale array; mostly zero, 108/768
nonzero, up to 47 bytes differing in one record).

### What was added to the reader vs left unknown

**Nothing was added as a named field.** `SpellsEffEmitter::unknown_bytes`
is unchanged (800 bytes, offset 128..928, as T70 left it) — none of the
eight claimed fields cleared the bar the plan set ("add fields additively
... with names that match what you proved"; the strongest test, semantic
correlation, was negative for the only field it could be run against, and
two fields have direct internal contradictions).

What *was* added, purely additively:
- `include/client/spell/spells_eff_reader.h`: a doc-comment addendum
  summarizing this unit's per-field verdict and evidence, so a future
  reader of the header does not have to reopen the plan to see why nothing
  was named. No type, field, or behaviour changed.
- `tests/test_spells_eff_reader.cpp`: 4 new tests (22 total, up from 18),
  reading directly out of the existing public `unknown_bytes` array (no new
  accessors), pinning: (1) record 0 Source's claimed-offset values, so a
  future spells.eff drop or reader change that shifts them is caught; (2)
  the min/max-scale majority-violation refutation; (3) the colour-range
  refutation; (4) the padding-record zero/one-template finding. All 22
  pass; sibling T72 sees zero API/behaviour change.

### Deviations

None from the plan's method. One scope note: the plan's semantic
correlation instruction names only `duration`; the other six fields have no
comparably direct DB-side correlate to test against (no "fade time" or
"particle count" field exists in `spells` table), so their verdict rests on
type coherence, distribution shape, and the padding check only — recorded
explicitly per-field above rather than silently treated as equivalent to
`duration_ms`'s fuller evidence.

### Verification

1. `cmake --build build -j24`: clean, no new warnings from this unit's changes.
2. `ctest --test-dir build --output-on-failure`: 2027/2028 (one known host
   exception, `AudioManagerTest.PreloadCommonSounds`, per protocol
   baseline — unrelated to this unit). `test_spells_eff_reader`: 22/22,
   including the 4 new T73 tests.
3. `scripts/build-gles2-x86.sh`: clean.
4. Visual gate: PASS, goldens untouched (`=== VISUAL GATE: PASS ===`).
5. `scripts/build-arm-noble.sh`: clean, all binaries produced.
6. `scripts/refactor/check_file_sizes.sh fable` (run from the slot on the
   task branch): 2/2 files ok (237 and 568 lines) — no new FAILs.

Board (`refactor/deferred.md`, append, never delete): record the per-field
verdict. If validated, note that fully data-driven spell visuals are now
possible and what remains. If refuted, record the offsets tested and the
observed distributions so the next attempt does not repeat the work.

Related, still open and deliberately out of scope: the recovered archive
also contains `.agent/spellsnew_edd_eff_format.md` covering the Luclin-era
`spellsnew.edd`/`.eff` pair — out of era for this client — and
`tools/model_viewer_spell_bar.h:274` still has `ENTRY_SIZE = 256` for
`spellsnew.eff` where `112024 / 268 = 418` exactly, which is why that tool
carries "skip fragments" hacks.
