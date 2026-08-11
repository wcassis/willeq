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

(executor fills: the confirmed layout as implemented; what was proven vs
left as unknown, with the evidence for each proven field; the
name→texture mapping verification against real filenames; record-content
census; the index-resolution result; what the tests actually assert;
deviations)

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
