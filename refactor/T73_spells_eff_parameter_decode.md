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

(executor fills: per-field verdict with the evidence for each; the
distributions; the padding check; the semantic correlation and what it
showed; what the 500-byte gap contains; what was added to the reader vs
left unknown; deviations)

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
