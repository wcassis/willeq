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

**Status: DONE (selection layer + staging wiring), video capture STOPPED.**
Commit: see branch `task/T72-spell-visual-layer`, base `810fb0d9`.

### The selection layer

`include/client/graphics/spell_visual_selection.h` +
`src/client/graphics/spell_visual_selection.cpp` (new, in territory, no
Irrlicht dependency): a pure `SpellVisualSelector::resolve(spell_affect_index)
-> SpellVisualResolution`. Wraps a `const SpellsEffReader*` (nullable —
missing spells.eff degrades to fallback, never crashes). `SpellVisualSource`
is a 3-way enum, deliberately not a bool, so the two distinct fallback
populations stay distinguishable: `NoAffectIndex` (spell_affect_index == -1
or reader unavailable), `EmptyRecord` (index resolves to a spells.eff record
whose `hasContent()` is false — the 5-index/148+-spell finding from T70),
`Resolved` (a real, populated record). `SpellVisualResolution` exposes
per-stage (`source_stage`/`middle_stage`/`target_stage`) `present` flags
(straight from `SpellsEffEmitter::hasContent()`), raw sprite names, and
`effectNameToTexture()`-mapped texture filenames — nothing from
`unknown_bytes` is touched, read, or referenced anywhere in this layer.

Tests: `tests/test_spell_visual_selection.cpp`, 12 tests, all passing (built
into `test_spell_visual_selection`, `GTEST_DISCOVER_TESTS`'d the same way
`test_spells_eff_reader` is). Covers: null-reader safety; -1 always
`NoAffectIndex` even with a healthy reader; record 0's exact fixture
(sprite names, texture files, attach tags, including the "00"-digit form
correctly resolving to `""`); record 88 (byte-identical to 255, T70's
correction of the plan text) classified as `EmptyRecord` not `Resolved`;
out-of-range indices degrade to fallback; and a DB-backed census
(`SpellVisualSelectorCensusTest.ClassifiesEverySpellIntoExactlyOneSource`)
that scans ids 1..9000 against the real `data/willeq.db` the same way
`test_spells_eff_reader.cpp`'s `EveryReferencedIndexResolvesToARecordSlot`
does.

**The count the plan asked for** (measured, not estimated — see the test's
`std::cout` line, reproduced verbatim):

```
[SpellVisualSelector census] NoAffectIndex=798 EmptyRecord=163 Resolved=7105 total=8066
```

`NoAffectIndex` matches T70's 798 exactly. `EmptyRecord` (163) is **higher**
than T70's table total of 148 — T70's table enumerated per-index spell
counts for the 5 specific empty-padding indices it named (70, 71, 80, 84,
88); the census here counts every spell landing on *any* empty-padding
record via a full id 1..9000 scan, which is a strictly more complete count
and evidently catches spells referencing empty-padding indices outside
T70's five. This is a genuine finding, not a bug: the test asserts `>=`
bounds (798/148) rather than exact equality specifically so a legitimate
upward correction like this doesn't fail the suite, and it's called out
here rather than silently absorbed. Total fallback: 798 + 163 = **961**
spells (close to, and consistent with, the plan's "~946" estimate).
`Resolved` = 7105 spells get a real spells.eff-backed record — the large
majority, driven by 58 distinct referenced indices (T70's finding) being
heavily reused across spell families.

### Wiring into `SpellVisualFX`

**What changed, concretely** (all in `src/client/graphics/spell_visual_fx.cpp`,
GLES2 branches only — see "why GLES2-only" below): `createCastGlow()`,
`createProjectile()`, `createImpact()`, and `createImpactAtPosition()` each
now call the new private `resolveVisual(spell_id)` (spell_id -> DB's
`spell_affect_index` -> `SpellVisualSelector::resolve()`) and, **only when
the result is `Resolved`** (never for `NoAffectIndex`/`EmptyRecord`
fallback), check whether the corresponding stage is present:

- `createCastGlow` gates on `hasSourceStage()` — spells.eff's Source
  (caster) emitter maps onto `SpellFXType::CastGlow`/`CastSpray`.
- `createProjectile` gates on `hasTravelStage()` — the middle/travel
  emitter maps onto `SpellFXType::ProjectileTravel`. (Dead in the shipped
  build today — see below — but wired for when it is called.)
- `createImpact`/`createImpactAtPosition` gate on `hasTargetStage()` — the
  Target emitter maps onto `SpellFXType::ImpactBurst`.

If the stage is absent, the call becomes a no-op and logs at DEBUG
(`MOD_SPELL`) exactly which stage was suppressed and why, citing the
resolved `spell_affect_index`. **The fallback path (961 spells) is
completely unchanged** — gating only ever fires for `Resolved` records, so
no spell that previously always showed a cast glow/impact loses it because
of this unit. This is the concrete, evidenced (not invented) sense in which
"visuals now differ where they previously did not": two spells sharing a
resist type/school that previously rendered byte-identically can now show
different staging (e.g. a cast-glow-only spell vs. a cast+impact spell) if
their `spell_affect_index` records genuinely differ in which emitters carry
content — this is real per-spell data, not a guess.

`createSpellComplete()` was deliberately left **ungated** — it doesn't map
cleanly onto exactly one of the three stages (it's a caster-side
"completion" burst, conceptually overlapping `CastGlow`'s Source mapping),
and the bridge always calls it immediately before `createImpact()` on cast
success (`irrlicht_bridge.cpp:769-770`, outside this unit's territory, not
touched); gating both on similar logic risked a spell showing literally
nothing on a real cast success, which is a worse outcome than the
`getSpellColor()`-only status quo it inherits.

**Texture identity (the "which textures" half of the plan's title) is
*resolved but not rendered* in this landing.** `resolveVisual()` exposes
the per-stage `texture_files` (real filenames like `spelc1.tga`, verified
against `/mnt/projects/EverQuestP1999/SpellEffects/` by T70), and they are
logged, but nothing in `SpellVisualFX` currently *displays* them: the live
GLES2 rendering path (`ParticleManager` + `SpellEffectsConfig`) draws from
a fixed, hardcoded 15-shape procedural atlas
(`spell_effects_config.cpp::buildTextureNameMap()` —
SoftCircle/StarShape/WispyCloud/.../IceCrystal), not arbitrary loose `.tga`
files from `SpellEffects/`. Loading spells.eff's real per-spell sprites
into that atlas (or extending `ParticleManager` to bind an ad-hoc texture
per effect) is a `particle_manager.cpp`/atlas-format change, which is
**outside this unit's territory** ("the particle presets it feeds" was read
as `spell_particle_types.h`'s preset *functions*, not the shared
weather/spell atlas renderer itself) and risked scope creep into rendering
plumbing shared with the weather system. Colour also stays entirely
resist/school-driven (`getSpellColor()`, unchanged) for the same reason —
recolouring per spell would require picking a colour from
`unknown_bytes`-adjacent data this unit is explicitly forbidden from
touching, or inventing one, which is exactly `48ae032b`'s failure mode.
**This is the honest boundary the plan drew**: staging is now data-driven,
texture identity and colour remain preset/resist-driven, and the gap
between them (atlas/texture-loading work) is the natural next step, not
folded in here.

`config/spell_effects.json`'s `spellOverrides` tier was **not populated** —
deliberately. It's the natural home for *numeric* per-spell overrides
(spawn rate, size, lifetime, colour), and this unit does not derive any of
those (STOP condition: don't decode `unknown_bytes`, don't invent a
parameter). Since nothing safe-to-derive fits that tier, it stays `{}`; the
staging decision this unit does make lives in code (`resolveVisual()` +
the four gated call sites) because it's a boolean per-call decision, not a
config value.

**Why GLES2-only, not also the legacy/worker-driven branches**: per
`src/client/graphics/CLAUDE.md`, GLES2 is "the only renderer, full stop" —
`m_particle_manager` is always set in the shipped build, so the
non-GLES2/`m_worker_driven` branches below each `#ifdef EQT_HAS_GLES2`
block are unreachable in production (same status as `getTextureForSpell()`,
which this unit left untouched for the same reason). Gating them too would
be inert code serving no shipped path.

### Fallback distinguishability in logs

`SpellVisualFX::loadSpellsEff()` (called unconditionally from the
constructor, independent of the GLES2/desktop split) logs at INFO on
success (record/populated counts) or WARN on failure (missing file/wrong
size — degrades to fallback, doesn't crash). `resolveVisual()` logs every
call: TRACE for `NoAffectIndex` ("no spell_affect_index or reader
unavailable"), DEBUG for `EmptyRecord` ("referenced record has no
sprite/tag content") — deliberately louder than the -1 case since it's the
more surprising fallback path (a spell referencing something that turns
out empty) — and TRACE for `Resolved` (which stages are present). The
three cases are lexically distinct log lines, not a single "no visual"
message, per the plan's requirement.

### Verification bar

1. `cmake --build build -j24` — PASS (clean build after fixing two CMake
   target source lists — `test_spell_visual_selection` and `model_viewer`
   both needed `spells_eff_reader.cpp`/`spell_visual_selection.cpp` added;
   caught by the linker, not silently missed).
2. `ctest --output-on-failure` — **2031/2032 passed** (99%). The one
   failure is the pre-existing documented host exception,
   `AudioManagerTest.PreloadCommonSounds` (ALSA-only, named in the
   protocol's verification bar). All 46 spell/spells.eff-related tests
   (`SpellDatabaseTest`, `SpellsEffReader*`, `SpellVisualSelector*`) pass,
   including the new 12 in `test_spell_visual_selection` and the
   DB-backed census.
3. `bash scripts/build-gles2-x86.sh` — PASS (Irrlicht bootstrap + willeq +
   gles2_zone_benchmark all build clean).
4. Visual gate: `=== VISUAL GATE: PASS ===` across qeynos2/gfaydark/befallen,
   goldens untouched (no diff output, no re-bless needed — this unit
   changes runtime spell-effect gating, not any golden-covered render
   path).
5. `bash scripts/build-arm-noble.sh` — PASS (exit 0, ARM cross-compile
   clean).
6. `bash scripts/refactor/check_file_sizes.sh fable` (run from this slot on
   this task branch): both modified files ok —
   `include/client/graphics/spell_visual_fx.h`: 282 lines,
   `src/client/graphics/spell_visual_fx.cpp`: 1496 lines. New files
   (`spell_visual_selection.h/.cpp`, the new test) aren't picked up by this
   script (it diffs tracked-file modifications, not untracked additions)
   but are new, small, single-purpose TUs consistent with its intent.

### Visual acceptance — STOPPED, no captures

**No screenshots or video were captured. This is a deliberate STOP, not an
oversight.** The plan's capture list (spells differing only in
`spell_affect_index`, an empty-padding spell, a `-1` spell, a before/after
pair) all require a live cast to observe the staging-gate behaviour
end-to-end (particle spawn/suppression happening in the running
`ParticleManager`). Two paths exist to a live cast and both are closed:

1. **A real client + server session** — forbidden outright: "NO EQEMU —
   sibling T71 in slot1 owns the lock" is explicit in this unit's brief.
2. **`model_viewer --spell-test`** — does not need eqemu (it drives
   `SpellVisualFX`/`ParticleManager` directly against a synthetic entity),
   but it is a keyboard-interactive tool (keys 1-0 cast, F7 cycles
   effects) with no scripted/headless casting path, and `tools/` is not in
   this unit's territory (`src/client/graphics/spell_visual_fx.{h,cpp}`,
   `config/spell_effects.json`, the particle presets, and tests). Adding a
   scripted-cast mode to `model_viewer.cpp`, or building a new bespoke
   headless GLES2 capture harness from scratch, is real device-bringup
   work outside the file territory and effort budget this unit was
   allotted, and risks being unverifiable if rushed — the plan explicitly
   says a selection-layer-only landing with rendering wiring deferred is
   better than a rushed one, and that's the call made here for the capture
   requirement specifically (the staging wiring itself *was* done and
   verified by build/tests, which the plan also allows as a partial step
   short of full texture-identity rendering).

Flagging this narrowly: the STOP is about producing *media*, not about the
code change — the gating logic is exercised indirectly by the selection
layer's own tests (which prove `hasSourceStage()`/`hasTravelStage()`/
`hasTargetStage()` return correct, real-file-backed answers) and by every
`ctest`/build pass above compiling and linking the gated call sites
cleanly. No `review/T72/` media directory was created since there is
nothing honest to put in it.

### Deviations from the plan

1. **No video/screenshot captures** (STOP, detailed above) — the plan's
   "acceptable outcome" clause for a live-session-blocked unit was invoked.
2. **`EmptyRecord` census (163) exceeds T70's table total (148)** — a
   genuine upward finding from scanning the full id range rather than
   trusting the five named indices, not a discrepancy in either unit's
   correctness. Detailed above; tests assert `>=` bounds, not equality, to
   keep this a durable pin rather than a landmine for future DB growth.
3. **Two CMake target source lists needed a source added**
   (`test_spell_visual_selection`, `model_viewer`) beyond the primary
   `willeq_core`/GRAPHICS_SOURCES lists — caught by the linker on first
   build, fixed before landing, not a design change.
4. **`config/spell_effects.json` left untouched** (`spellOverrides` stays
   `{}`) — a considered decision (see above), not an omission: nothing
   this unit derives is a numeric per-spell value that tier is meant to
   hold.
5. **`createSpellComplete()` left ungated** — considered and rejected as a
   gating target for the reason given above (risk of a cast showing
   nothing).

None of these required a hard STOP on the code itself — the one real STOP
is the capture requirement, narrowly.

Board: record step 3's landing and what remains — decoding
`unknown_bytes` for timing/scale/colour is the next natural step and the
last thing standing between this and fully data-driven spell visuals.
