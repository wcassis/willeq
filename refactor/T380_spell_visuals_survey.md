# T380 — spell visuals: the measured gap (B022 survey, no server)

Class P (a survey that turns B022's ruling into numbers); slot3.
B022 says the data-driven spell visual layer "is still entirely ahead":
nothing consumes the `spells.eff` reader in place of the hardcoded
resist/school vocabulary, and spells whose `spell_affect_index` hits an
empty padding record need a rendering ruling. Read the B022 board text,
`src/client/graphics/spell_visual_fx.cpp`, the `spells.eff` reader (grep
`spells.eff` / `SpellsEff` under src/ include/ tools/), and the willeq.db
`spells` table (`spell_affect_index`, `casting_anim`, `target_anim`,
`spell_visual` columns — whatever the builder loads).

1. Reachability census: which production path plays a spell visual today
   (cast start, cast land, buff tick?), what it keys on (resist/school
   vocabulary — quote the table), and whether the `spells.eff` reader has
   ANY production caller (T48/T264 pattern). Report as a table.
2. Data census over `/mnt/projects/EverQuestP1999/spells.eff` and the db:
   record count; for every spell in `spells_us.txt`, does its
   `spell_affect_index` land on a populated record or a padding record?
   Counts + the list of the padding-hit spells by class/level (are they
   the ones a level-1 test character will cast?).
3. What the official client does for a padding-hit spell is an oracle
   question (video under Wine — needs the lock; NOT this unit): name the
   three spells that would settle it (one populated, one padding-hit, one
   with no affect index) and the capture recipe (T361's rig).
4. The shape of Leg B (the wiring): which existing render path a
   data-driven effect would enter (the particle system? the `spell_visual_fx`
   sprites?), what the reader yields per record (fields), and the smallest
   end-to-end slice (one spell, one effect, matched to a capture) — a plan
   section written for a follow-up unit, with the STOPs.
5. Review: the tables; annotate B022 with the numbers (do not strike);
   board anything found dead along the way.

## Proof

Docs-only + any census script under `scripts/refactor/` (commit it);
`ctest -R Doc`. No build needed unless you add a test.

STOP: none expected.

---

## Review

Executor session 2026-09-05, slot3, branch `task/T380-spell-visuals-survey`,
base/tip `82d1801f`. Survey only — **no production code changed**. Census
script committed at `scripts/refactor/t380_spell_visual_census.py`
(read-only; re-run with
`python3 scripts/refactor/t380_spell_visual_census.py --db data/willeq.db`).

### Headline: B022 understates the gap by one whole level

B022 says the data-driven layer "is still entirely ahead". Measured, it is
worse than that: **the resist/school vocabulary B022 treats as the shipped
fallback is itself unreachable.** `SpellVisualFX::setSpellDatabase()`
(`include/client/graphics/spell_visual_fx.h`) has **zero callers tree-wide**,
so `m_spell_db` is always `nullptr` in production. Every consumer of it
short-circuits:

| member | guard | value in production |
|---|---|---|
| `getSpellColor(spell_id)` (`src/client/graphics/spell_visual_fx_nodes.cpp`) | `if (m_spell_db)` | always the default `SColor(255,150,180,255)` — the 6 resist colours and 5 school colours are dead |
| `isSprayCastStyle()` (`src/client/graphics/spell_visual_fx.cpp`) | `if (m_spell_db)` | always `false` — every cast is the smolder/orbital style, the spray style never plays |
| `resolveVisual()` (`src/client/graphics/spell_visual_fx.cpp`) | `if (m_spell_db)` | `affect_index` stays `kSpellsEffNoEffectIndex` → **always `NoAffectIndex`** |
| `getTextureForSpell()` (`src/client/graphics/spell_visual_fx.cpp`) | `if (m_spell_db)` | always `m_particle_texture` (non-GLES2 path only) |

Consequence: **every spell in the game renders one identical lavender-blue
orbital glow**, and the `spells.eff` layer T70/T72 built can never fire,
because the only thing it is wired to do is *suppress* a stage on a
`Resolved` record — and `Resolved` is unreachable. Boarded as **B160 (A)**.

The renderer has no route to a `SpellDatabase` today: the only instance is
`SpellManager::m_spell_db` (`include/client/spell/spell_manager.h`), owned on
the game thread, and `SpellDatabase` carries an LRU (`m_cache`, `m_lru`) with
**no mutex** — handing that pointer across the bridge would be a data race.
That constraint drives the Leg B shape below.

### 1. Reachability census

Wire → game → bridge → renderer, as it stands at `82d1801f`:

| stage | producer | event | consumer | what it plays | keyed on |
|---|---|---|---|---|---|
| cast start | `SpellManager::handleBeginCast` (`src/client/spell/spell_manager.cpp`, from `OP_BeginCast`) | `SpellCastVisualStarted` | `src/client/bridge/irrlicht_bridge.cpp` | `createCastGlow(casterId, spellId, castTimeMs)` + casting bar | `getSpellColor` (dead) + `isSprayCastStyle` (dead) → constant colour, `CastGlow` preset |
| cast land | `SpellManager::handleAction` (`OP_Action`, type 231), success packet only | `SpellCastVisualComplete` | same file | `removeCastGlow` + `createSpellComplete(casterId)` + `createImpact(targetId)` | same constant colour |
| cast interrupt | `SpellManager::handleInterrupt` | `SpellCastVisualInterrupted` | same file | `removeCastGlow` only | — |
| cast timeout | `SpellManager::update` (cast_duration + 5 s) | `SpellCastVisualComplete` (`isSuccess=true`) | same file | as "cast land" | — |
| buff tick / buff land | **nothing** | — | — | **no buff aura is ever created** | — |
| projectile travel | **nothing** | — | — | **no projectile is ever created** | — |

`spells.eff` reader production caller: **YES, one** —
`SpellVisualFX::loadSpellsEff()` runs in the `SpellVisualFX` constructor
(`src/client/graphics/renderer/renderer_entities.cpp` builds it), so the file
is opened, all 256 records are parsed, and the count is logged. But the parse
result is consumed only by `resolveVisual()`, which per B160 can never see a
non-`-1` index. **The reader is loaded, correct, and inert.** This is the
T48/T264 pattern one hop further along than usual: not "no caller" but "a
caller that is structurally guaranteed to take the null branch".

Dead public API on `SpellVisualFX` — **zero callers tree-wide**, production or
test (grep over `src/`, `include/`, `tests/`): `createProjectile`,
`createImpactAtPosition`, `createBuffAura`, `createRainEffect`,
`createGroundCircle`, `removeBuffAura`, `removeAllForEntity`. With them die
`createProjectileNode`, `createAuraNode`, `createRainNode`,
`createGroundCircleNode` (`src/client/graphics/spell_visual_fx_nodes.cpp`),
`updateProjectile`/`updateAura`, and the `pendingImpacts` re-entry dance in
`SpellVisualFX::update()`. `createProjectileNode` and `createAuraNode` are
also reached from `applyWorkerResults()`, but only for `createEvents` the
worker emits in response to commands that `createProjectile`/`createBuffAura`
would have pushed — so that route is dead too. Boarded as **B161 (B)**.

### 2. Data census

`/mnt/projects/EverQuestP1999/spells.eff`, 714,752 bytes = 256 × 2,792, and
`/mnt/projects/EverQuestP1999/spells_us.txt`, 8,066 spells with `id > 0`.
`data/willeq.db` agrees with the text file on `spell_affect_index` for
**8,066 / 8,066 rows** (zero mismatches).

| population | count | % of 8,066 |
|---|---|---|
| `Resolved` (index hits a record that has sprite names) | 7,105 | 88.1% |
| `EmptyRecord` (index hits a record with no sprite names) | 163 | 2.0% |
| `NoAffectIndex` (`spell_affect_index == -1`) | 798 | 9.9% |
| `OutOfRange` (index ≥ 256) | 0 | 0.0% |

`spells.eff` records:

| | count |
|---|---|
| records total | 256 |
| records with at least one sprite name or attach tag | 90 |
| records with none ("padding") | 166 |
| of the 90, with a **Source** stage | 53 |
| of the 90, with a **Middle** stage | **9** |
| of the 90, with a **Target** stage | 89 |

**Correction to T70, boarded on B022.** T70 recorded "5 empty-padding indices
(70, 71, 80, 84, 88) and 148 spells". Measured here it is **6 indices and 163
spells** — index **60** (15 spells) was missed:

| padding index | spells hitting it |
|---|---|
| 60 | 15 |
| 70 | 109 |
| 71 | 1 |
| 80 | 6 |
| 84 | 5 |
| 88 | 27 |

`tests/test_spell_visual_selection.cpp`'s comment and its
`EXPECT_GE(emptyRecord, 148u)` still pass, but the documented figure is stale.

**And "padding" is the wrong word.** Not one of the 256 records is zero-filled:
every one carries non-zero bytes inside the 800-byte region per emitter that
`SpellsEffReader` reads into `unknown_bytes` and never interprets (168 non-zero
bytes for a typical sprite-less record, 270 for index 60). Of the 166
sprite-less records, **164 are byte-identical** (a single default template
occupying indices 37..255); the two exceptions are index **36** (unreferenced)
and index **60**. So the sprite-less records are a *default template*, not
unwritten space — and the numeric parameters the client presumably reads out
of that template are exactly what the reader discards. Also measured:
`header_a` is 1 for only 32 of the 256 records, and 58 of the 90 sprite-bearing
records have `header_a == 0` — **`header_a` is not the enabled flag**, so do
not build a ruling on it.

**Are the padding-hit spells the ones a level-1 test character casts? No — not
one of them is castable by any player character at any level.** All 163 have
every one of the 16 class-level fields set to 255. The distribution B022 asks
for is therefore degenerate and worth stating as such:

| padding-hit spells by minimum castable level | count |
|---|---|
| (no class can cast any of them) | 163 |

They are NPC breath/rot/mist attacks (`WurmBreath`, `Poison Breath`, `Slime
Mist`, `Drake Breath` ×4, `Rotting Flesh`), weapon-proc lines (index 84 = the
Heated/Burning/Blazing/Flaming Blade line) and quest/NPC augury spells (index
88, the `Xalirilan's …` / `Iony's …` family).

Restricting to the population a player ever sees:

| player-castable spells (any class level in 1..254) | 2,584 of 8,066 |
|---|---|
| `Resolved` | 2,548 |
| `EmptyRecord` | **0** |
| `NoAffectIndex` | 36 |

(383 of the 2,584 are AA/discipline rows — every class level is the 254 marker.
The 36 `-1` rows are the Imbue-gem line, the Berserker Strike disciplines and
`GM Cleansing`.)

**So the "rendering ruling for padding-hit spells" B022 asks for is a
low-priority question**: it can only ever be observed when an NPC casts at the
player, never on the player's own bar. The high-priority question is B160.

### The sprite names have nowhere to go

Even with B160 fixed, a `Resolved` record yields sprite names that mostly do
not resolve, and none of them can be drawn:

- 40 distinct sprite names occupy 301 sprite slots across the 90 records.
- `SpellsEffReader::effectNameToTexture()` (digit taken verbatim, `'0'`
  deliberately excluded) maps **18 of 40** names; 16 of those 18 exist in
  `/mnt/projects/EverQuestP1999/SpellEffects/`. Weighted by use that is
  **157 of 301 sprite slots (52%)**.
- The 22 it cannot map are the `GENx00_SPRITE` family plus `GEN_Z_SPB`,
  `I_SNOWFLAKESPRITE`, `I_TFIRE_SPB`, `I_TSMOKE_SPB` — and they are the
  *commonest* names (`GENI00` ×32, `GENN00` ×15, `GENG00` ×12).
- A single alternative reading — **the digit is 0-based, so `GENx0 -> spelx1`**
  — maps 36 of 40 names, 34 of them on disk, **272 of 301 slots (90%)**. That
  is a hypothesis, not a finding; it is settled by the oracle, not by argument.
  Boarded as **B162 (C)**.
- Regardless of the mapping: the GLES2 spell path draws point sprites from a
  **single 4×4, 16-tile procedurally generated atlas**
  (`data/textures/particle_atlas.bmp`, `ParticleAtlas` in
  `include/client/graphics/environment/particle_types.h`), of which exactly one
  tile — `SpellSparkle = 8` — is a spell shape. There is no path that binds an
  arbitrary `spel*.tga` for a spell particle. **This is the "blocked on the
  procedural particle atlas, not on data" note, confirmed**: the seam is
  `EmitterConfig::textureRegions[]`, filled via
  `SpellEffectsConfig::getTextureIndex(name)`
  (`src/client/graphics/environment/spell_effects_config.cpp`), which maps a
  *name* to an atlas tile index and falls back to `SoftCircle` for anything
  unknown.

### Fields the builder drops

`include/client/spell/spell_constants.h` defines `TargetAnim = 121` and
`TravelType = 122`, but `tools/willeq_db_builder.cpp::loadSpells()` binds
neither — the `spells` table has `cast_anim` and no `target_anim` /
`travel_type` / `spell_visual` column (verified by `PRAGMA table_info`). Both
carry real signal: `target_anim` is non-zero for 3,608 of 8,066 spells (27
distinct values) and `travel_type` is 3 for exactly 85 spells — **that 85 is
the projectile population**, and it is the only field that says a spell should
have a travel stage at all. Boarded as **B163 (B)**.

### 3. The oracle question and the three spells

What the official Titanium client draws for a sprite-less record is not
derivable from the file; it must be captured. Three spells settle it, chosen so
each frame is unambiguous:

| role | spell | id | affect index | why this one |
|---|---|---|---|---|
| populated, all three stages, and a real projectile | **Frost Bolt** | 54 | 23 | Wizard **level 1**; `travel_type = 3` (one of only 85), so the Middle stage should actually fly. Record 23: Source `GENU00`×3 / Middle `GENA10`,`GENU10` (attach `Sprite`) / Target `GEND10`,`GENU10`×2. `GENA10`→`spela1.tga` and `GENU10`→`spelu1.tga` both exist, so the shipped mapping is exercised on the same frame as the unmapped `GENU00` |
| padding-hit | **Flame Jet** | 1494 | 71 | the **sole** spell referencing index 71, so nothing else can be confused for it. Record 71 is a byte-copy of the 164-record default template. NPC-only (all class levels 255) → must be cast with `#castspell` |
| no affect index | **GM Cleansing** | 6594 | −1 | player-castable by all 16 classes, GM spell, **no reagent**, beneficial self-target so it lands reliably |

A useful fourth if time allows: **Burst of Flame** (93, index 0, Druid/Shaman/
Magician level 1) — record 0 mixes a mapped name (`GENC10`→`spelc1.tga`) with
an unmapped one (`GENG00`) in the same Target stage, which is the cleanest
single frame for the B162 mapping question.

**Capture recipe** (T361's rig, `review/T361/scripts/t361_oracle.sh`; needs the
eqemu lock, which T377 holds — this is explicitly a follow-up unit):

1. Take `/mnt/projects/fable/.eqemu-lock` atomically (`set -o noclobber`).
2. Xvfb `$EQO_XVFB_DISPLAY` 1024×768×24, `WINEARCH=win32`,
   `LP_NUM_THREADS=1`; `/mnt/projects/eq-oracle/scripts/login-as.sh <account>`
   then `enter-world.sh`; arrival verified against the **world-server log**,
   not the screen.
3. Continuous `ffmpeg -f x11grab -framerate 5 … -pix_fmt yuv444p` for the whole
   session — **a cast is a transient; never sample it with stills**
   (`dont-sample-transients-with-stills`).
4. `#set time 11 0` (space-separated; `11:00` does not parse) — casts must be
   judged in daylight, and confirm the client survives a few seconds of
   rendering after the command before building the sequence on it.
5. Face a stationary NPC at a fixed camera; every command goes through T361's
   `cmd()` helper, which **OCRs the client's own chat panel** to verify the
   line landed (blind chat input silently loses text).
6. Per spell: mark, `#castspell <id>`, hold ~8 s, mark. Order 54 → 1494 → 6594
   → 93, with `#nukebuffs` and a 25 s settle between them.
7. Then the identical sequence on fable at the same tip, spot and time, into
   `review/<UNIT>/fable/` — the pair is the deliverable; a single-client
   capture proves nothing.

Questions the capture must answer, in priority order:
**(a)** does the padding-hit spell (1494) draw *nothing*, or the same generic
effect as a `-1` spell (6594)? **(b)** does a `-1` spell draw nothing at all?
**(c)** for 54, which texture appears at the caster's hands — the answer to
B162 is whether it is `spelu1.tga` (0-based) or something else entirely;
**(d)** does the Middle stage of 54 visibly travel, confirming
`travel_type == 3` as the projectile trigger?

### 4. Leg B — the wiring (plan section for a follow-up unit)

Ordered so each step is provable on its own. **Step 1 is worth doing even if
nothing else is**: it is a live class-A defect fix with visible output.

**Leg B1 — make the existing vocabulary reachable (B160). Class A, no oracle
needed.** Give the renderer a spell lookup. Two candidate shapes:

- *(preferred)* enrich the bridge event. `SpellCastVisualStartedData` /
  `SpellCastVisualCompleteData` (`include/client/state/`) already carry
  `spellId` and `spellName`; add `resistType`, `school`, `spellAffectIndex`
  and `travelType`, filled on the **game thread** in `SpellManager` where the
  `SpellData*` is already in hand. No new ownership, no thread question, and
  it matches the "enriched event" precedent named in `CLAUDE.md` for
  `InventorySlotChanged`.
- *(alternative)* give `SpellVisualFX` its **own** `SpellDatabase` instance
  (SQLite WAL readers are safe across threads; the LRU is not, so it must not
  be the `SpellManager` one) and call `setSpellDatabase()` from
  `renderer_entities.cpp`.

Proof: a unit test that a cast of a Fire-resist spell and a cast of a
Cold-resist spell produce **different** particle colours (red pre-fix — both
are `(150,180,255)` today), plus a two-client live pair showing a fire spell
and a frost spell side by side. **STOP** if the enriched-event route would
grow `SpellCastVisual*Data` past what the coalescing in `GameStateBridge`
expects — report, do not redesign the bridge.

**Leg B2 — sprite name → atlas tile.** The smallest end-to-end slice: one
spell, one stage, matched to the capture. Take **Frost Bolt (54)**, its
**Target** stage, and route `SpellVisualResolution::target_stage.sprite_names`
into `EmitterConfig::textureRegions[]` through
`SpellEffectsConfig::getTextureIndex()` — the seam already exists and already
defaults to `SoftCircle` for an unknown name, so an unmapped name degrades
exactly as today. That means adding `GENU10_SPRITE` / `GEND10_SPRITE`-style
entries to `textureNameMap_` pointing at existing atlas tiles, **not** loading
`spel*.tga`. Proof: the fable frame for spell 54 versus the oracle frame from
step 3, plus a unit test on the name→tile mapping.
**STOP** before extending `ParticleAtlas` past 16 tiles or making the atlas
load EQ textures — that is a new render feature and a Will decision, not part
of this slice.

**Leg B3 — the padding ruling (B022's actual question), oracle-gated.** Only
after the capture. If the official client draws the generic effect for a
sprite-less record, then `SpellVisualSource::EmptyRecord` must behave exactly
like `NoAffectIndex` and the three `!visual.isFallback() && !visual.hasXStage()`
suppression gates in `spell_visual_fx.cpp` are correct as written. If it draws
nothing, they are correct for that case too — but then the `Resolved`-with-a-
missing-stage case needs its own capture, because the two are conflated in the
current code. Note this is **NPC-cast-only** (measured: 0 of 2,584
player-castable spells hit a padding record), so it is low priority.
**STOP** if the oracle's answer differs between 1494 and 6594 — that means
`EmptyRecord` and `NoAffectIndex` are *different* states in the official
client and the ruling is Will's, not the executor's.

**Leg B4 — projectiles, or delete them (B161).** `travel_type == 3` marks
exactly 85 spells and 9 records carry a Middle stage; that is the entire
projectile population. Either wire `createProjectile` (a producer at cast start
for `travel_type == 3`, target from `SpellCastVisualStartedData`) or delete the
seven dead methods and their node builders. Do **not** do both halves in one
unit. The `target_anim` / `travel_type` builder columns (B163) are a
prerequisite for the first option and a `kWilleqDbExpectedVersion` bump —
which invalidates **every** build tree, so it must be its own leg.

### Board changes

- **B022** annotated with the numbers above (not struck — the ruling it asks
  for is still open, and is now correctly scoped as NPC-cast-only).
- **B160 (A)** new — `setSpellDatabase()` has no caller; the resist/school
  vocabulary and the whole `spells.eff` resolution are unreachable.
- **B161 (B)** new — seven dead `SpellVisualFX` creation/removal methods.
- **B162 (C)** new — `effectNameToTexture()` resolves 52% of sprite slots; the
  0-based reading reaches 90%; oracle question.
- **B163 (B)** new — `target_anim` (121) and `travel_type` (122) are defined in
  `spell_constants.h` and dropped by the db builder.

### Verification

`ctest -R Doc` in `/mnt/projects/fable/wt/slot3/build` — see the run recorded
with this commit. No build was needed (docs + one read-only script; no
production code and no test source changed).
