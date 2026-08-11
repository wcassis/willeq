# T72 — Spell visuals driven by `spells.eff`, not by resist type

**Slot: t2. x86 only. No Pi. NO EQEMU** — sibling T71 owns the server.

**Territory: `src/client/graphics/spell_visual_fx.{h,cpp}`,
`config/spell_effects.json`, the particle presets it feeds, and tests.
Read-only consumption of `include/client/spell/spells_eff_reader.h` (T70)
and `SpellData` (T68).**

Do not modify the reader — T70 validated it and it is the contract. Do not
touch the spell database path. Do not touch `bone_attachment.h` or
equipment rendering (T71/T65 ground). `refactor/deferred.md` is
append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then **`refactor/T70_spells_eff_reader.md`'s
Review in full** — it tells you exactly what the reader gives you and,
more importantly, what it deliberately does not.

## Why this unit exists

Step 3 of the spell-effects rewrite Will scoped. He has declared the
current visuals broken.

Today the entire visual vocabulary is **10 resist types × 5 schools**
(`spell_visual_fx.cpp:190-219` for texture, `:879-918` for colour). Every
fire spell in the game — a nuke, a DoT, a damage shield, a pet buff —
renders identically. `config/spell_effects.json`'s `spellOverrides` tier
exists and is `{}`.

T68 put the spell mechanics into `SpellData::effects`. T70 landed a
validated reader for the client's own visual definitions. This unit makes
the renderer use them.

## What you actually have — and what you do not

T70 proved the structure and **deliberately refused to invent semantics**
for the numeric parameters. Read that Review before designing anything.

**Available and trustworthy:**
- 256 records, indexed directly by `SpellAffectIndex` (carried into
  `SpellData` by T68).
- Per record, **three staged blocks**: Source (caster), middle/travel,
  Target.
- Per block, **up to three sprite layer names** plus an attach-point tag.
- A verified `GEN[A-Z][1-9]0_SPRITE → spel[a-z][1-9].tga` mapping. Note
  the `'0'`-digit form **never** resolves to a real file and returns empty
  by design — do not treat that as a lookup failure to work around.

**NOT available:** timing, scale, colour, particle rate, lifetime,
velocity. T70 tested the hypothesis that the ints at block+128 are
per-layer indices, found it **did not hold**, and exposed that 800-byte
region as raw `unknown_bytes`. **Do not decode it in this unit and do not
guess at it.** The parked branch `48ae032b` invented exactly these
parameters and hid the invention behind clamps that silently defaulted;
that is the failure mode to avoid.

So the honest scope is: **`spells.eff` drives *which textures* and *what
staging*. The existing presets keep driving timing and motion** until
someone decodes the rest. That is still a large improvement — per-spell
texture identity and source/travel/target staging, versus one colour per
resist type.

## The fallback population is large — design for it first, not last

T70 established, against the shipped database:
- **798 spells** carry `spell_affect_index == -1` (no visual by design).
- **5 referenced indices (70, 71, 80, 84, 88) resolve to empty padding
  records**, affecting **148 more spells** — the Poison Breath family,
  Flame Jet, the Leviathan Tsunami family, the elemental Blade line, the
  Augury family.

**That is roughly 946 spells with no usable `spells.eff` definition.** The
resist/school switch you are replacing does not go away; it becomes the
fallback for about a thousand spells, and it must stay good. Treat the
fallback as a first-class path, not an afterthought — and make the two
cases distinguishable in logs so a missing visual is diagnosable rather
than silently ordinary.

## Approach

1. **Selection first, rendering second.** Build the mapping — spell →
   `spell_affect_index` → record → per-stage texture layers — and make it
   testable on its own, without rendering. A pure function from spell id
   to a resolved visual description is far easier to verify than pixels,
   and it is where the interesting logic lives.
2. **Then wire it into `SpellVisualFX`.** The existing `SpellFXType`
   vocabulary (`spell_visual_fx.h:38-47`: `CastGlow`, `ProjectileTravel`,
   `ImpactBurst`, `AuraPersistent`, …) maps naturally onto the
   Source/middle/Target staging — say explicitly how you mapped them, and
   where the fit is imperfect.
3. **`config/spell_effects.json`'s `spellOverrides` tier is the natural
   home** for anything derived per-spell, and its hot-reload plumbing
   already works. Use it rather than adding a parallel mechanism.

## Visual acceptance

**This changes what the player sees, so Will judges it.** Capture:
- several spells that differ only in `spell_affect_index`, to show the
  visuals now differ where they previously did not,
- at least one spell from the **empty-padding** set (e.g. Flame Jet) and
  one with `-1`, to show the fallback,
- a before/after pair for the same spell, captured identically.

**Spell effects are temporal — record video, not stills.** T64 landed the
capture path (`--dump-frames-dir` / `--dump-every` / `--dump-max-frames`
plus `scripts/ci/encode_capture.sh`); keep the measured-fps file with each
video, and remember capture fps is an artifact, never a performance claim.

Media to `/mnt/projects/fable/review/T72/` with a README. Describe what
each capture shows; **do not grade it.**

If casting a spell requires a live session and you cannot get there
headlessly, **say so and STOP rather than taking the server** — T71 owns
it. A selection-layer-only landing with the rendering wiring deferred is
an acceptable outcome and better than a rushed one.

## STOP conditions

- Any urge to decode `unknown_bytes` or to name a parameter you cannot
  evidence.
- Any change to the reader.
- The fallback path degrading — ~946 spells depend on it.
- Needing eqemu.

## Verification

Full protocol bar, plus: the selection layer's own tests (spell →
resolved visual, including both fallback classes); the count of spells
that now resolve to a real record versus fallback; and the captures.

## Review

(executor fills: the selection mapping and its tests; how the three
stages map onto the existing `SpellFXType` vocabulary and where it fits
badly; how fallbacks are distinguished in logs; what is still driven by
presets rather than data; capture paths; deviations)

Board: record step 3's landing and what remains — decoding
`unknown_bytes` for timing/scale/colour is the next natural step and the
last thing standing between this and fully data-driven spell visuals.
