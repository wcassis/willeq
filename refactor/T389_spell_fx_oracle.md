# T389 — spell effects against the oracle (B162 hypothesis, B022 ruling, T380 Leg B2 match)

Measurement first; eqemu lock (atomic; must be ABSENT — if present read
it and STOP); the slot that frees after T386. Read `refactor/T380_spell_visuals_survey.md`
§3 (the three oracle spells and the capture recipe) and §4 Legs B2/B3,
the B162/B165/B022 board text, `review/T361/README.md` + scripts (the
official client rig), and T381/T384's Reviews (what the vocabulary now
carries).

## Leg A — capture (both clients, one session)

Official client (T361 rig, Oraclealpha or a Wizard-capable GM char),
qeynos2, daylight (`#set time 11 0`), continuous `ffmpeg x11grab` at 5+
fps for the whole session; OCR-verified `#castspell <id>` for **54 Frost
Bolt** (populated, Target stage, travel_type 3 → projectile), **93 Burst
of Flame** (fire, populated), **1494 Flame Jet** (EmptyRecord), **6594 GM
Cleansing** (NoAffectIndex); `#nukebuffs` + 25 s between. Then the same
four on fable at the same tip, same spot, same camera. Step the videos:
for each spell, per stage (Source = caster, Middle = travel, Target =
impact): does anything draw, what colour, what sprite shape (crop and
enlarge the particle), duration in frames. Table in `review/T389/`.

1. **B162:** the sprite the oracle draws for 54 and 93 versus the two
   candidate mappings (`GENx0` = `spelx0` vs `spelx1`, files under
   `/mnt/projects/EverQuestP1999/SpellEffects/`): which file's image
   matches the enlarged crop? Settle the hypothesis or record that neither
   matches (then the name is not a texture file — say what it is).
2. **B022 / Leg B3:** 1494 vs 6594 — do both draw the generic effect, both
   nothing, or differ? If they DIFFER, STOP that leg for Will.
3. **Projectile:** does 54 show a travelling object between caster and
   target on the oracle, and over how many frames?

## Leg B — the smallest match (T380 Leg B2)

With the oracle frames in hand: route Frost Bolt's Target-stage sprite
names into `EmitterConfig::textureRegions[]` via
`SpellEffectsConfig::getTextureIndex()`, mapping the measured names onto
EXISTING atlas tiles that best match the oracle crop (pick by comparing
tile against crop, record the comparison). Unit test on the name→tile
mapping (pre-fix failing); the fable frame for 54 re-captured and placed
beside the oracle frame. STOP before extending `ParticleAtlas` past 16
tiles or loading `spel*.tga` — if the oracle's sprite is not
approximable by any tile, that is the finding: board it (B195+) with the
crops as the Will decision (atlas from EQ textures = new render
feature).

Restore every server value; `down.sh`; delete the lock. Strike/annotate
B162, B022, B165 with the measured answers.

STOP: lock present; the official client will not cast (10 minutes,
record why); Leg A item 2 differs; Leg B needs the atlas.

## Review

Branch `task/T389-spell-fx-oracle`, based on `3c9b0682`, with
`task/T395-spell-impact-eqstr-race-data` (`084d2ef1`) merged in at
`9874c137` so the fable-side captures run on a binary that carries T395's
two B204 fixes. Media: `/mnt/projects/fable/review/T389/`.

### 1. The rig, and what each run measured

Seven official-client sessions and two fable sessions, all in `qeynos2`,
all forced to `#set time 11 0`, all recorded as continuous video (a cast is
a transient -- never sampled with stills). The oracle rig is T361's
(`review/T361/scripts/t361_oracle.sh`) with the sequence replaced;
`review/T389/scripts/t389_oracle{,2..7}.sh`. The fable rig is T386's
`--script` driver (xdotool is the wrong driver for this), never xdotool.

| run | character | what it measured |
|---|---|---|
| run1 | Oraclealpha | 93, 54, 6594, 1494 self-cast; 1494's window CONFOUNDED (Grennet_Stormhand cast his own spell in frame) |
| run2 | Oraclealpha | 41, 201, 285 (three GENx00-only records) + 4243 (EmptyRecord) + 6594 (NoAffectIndex) |
| run3 | Oraclealpha | clean 1494 + 6594; projectile attempt 1 -- backpedal walked the character into `qeynos` (zone 1) |
| run4 | Oraclealpha | projectile attempt 2 -- the DB heading did not take; target closed to melee before the cast |
| run5 | Oracledelta (wizard) | first REAL GEM casts (`#scribespells 12`, `#memspell 54 1`, `/cast 1`); target closed to melee and interrupted them |
| run6 | Oracledelta | gem cast of 93 succeeded (Source stage captured); projectile attempt 3 |
| run7 | Oraclealpha | **the B162 discriminator**: 253, 231, 261, 41 |
| fable run1 | Testcaster | 93, 54, 41, 4243, 6594 by `#castspell` + one `/cast 1` gem cast, 1024x768 |
| fable fb54 | Testcaster | Frost Bolt x6 at 640x480 (607 frames, **measured 3.22 fps**) |

**Which casts were GM and which were gem.** Every oracle cast in runs 1-4
and 7 and every fable cast except one is a GM `#castspell`, which calls
eqemu's `SpellFinished` directly: no `OP_BeginCast`, no cast bar, no travel
stage. The only real gem casts are run5's and run6's `/cast 1` / `/cast 2`
on Oracledelta and fable run1's single `/cast 1` on Testcaster. Run6's
`/cast 2` (Burst of Flame) is the one that got a clean cast window; its
Source stage is `oracle/T389_oracle_93_gemcast_SOURCE_stage_crop3x.png`.

### 2. What the oracle draws, per spell, per stage

All figures are means over the crop `300x220+360+300` (the caster) of the
10 fps video, `d` against that window's own median.

| spell | id | affect index / source | does anything draw | colour (peak RGB) | sprite | duration |
|---|---|---|---|---|---|---|
| Burst of Flame | 93 | 0, Resolved | **yes**, and heavily: a fountain of small orange point sprites over a third of the screen plus one large soft yellow-white glow on the target | .360/.379/.199 (R-dominant) | Target `GENC20`->`spelc3.tga` (large yellow starburst) + `GENC10`->`spelc2.tga` (orange eight-point stars) | 23 frames = **2.3 s** |
| Frost Bolt | 54 | 23, Resolved | **yes**: purple/blue ribbon swirls at the caster's hands (Source `GENU00`->`spelu1.tga`) and a blue burst on the target | .213/.284/.315 (B-dominant) | Source `spelu1.tga`; Target `GEND10`->`speld2.tga` + `GENU10`->`spelu2.tga` | 33 frames = **3.3 s** |
| Weaken | 41 | 20, Resolved, **every slot `GENN00_SPRITE`** | **yes**: a field of large green/orange mottled spheres | .448/.572/.191 | **`speln1.tga`, an exact match** | 20 frames = **2.0 s** |
| Whirling Wind | 253 | 35, Resolved, **every slot `GENA10_SPRITE`** | **yes**: bright blue EIGHT-pointed stars | .258/.309/.214 | **`spela2.tga`, an exact match -- NOT `spela1.tga`** | ~60 frames = **6 s** |
| Pendril's Animation | 285 | 11, `GENO00` only | **yes**: golden burst dots (plus the summoned sword pet, which is a model, not a sprite) | .284/.321/.202 | consistent with `spelo1.tga` | ~1 s |
| Flash of Light | 201 | 22, `GENH00` only | the screen goes **fully black** (the blind effect) for 14 s -- the sprite cannot be observed this way | 0/0/0 | not observable | 136 frames |
| Intoxication I | 4243 | **70, EmptyRecord** | **yes**: the generic bright-blue dome of point sprites | .378/.383/.523 | none (no record sprites) | 28 frames = **2.8 s** |
| GM Cleansing | 6594 | **-1, NoAffectIndex** | **yes**: the same generic bright-blue dome | .381/.386/.624 | none | 28 frames = **2.8 s** |
| Flame Jet | 1494 | 71, EmptyRecord | **yes** (blue, generic), but the only window captured is confounded by an NPC casting in frame -- 4243 above is the clean measurement of the same byte-identical template | .244/.291/.312 | none | ~6 s |

Two traps worth keeping. **(a) The purple ribbon swirl at a caster's hands
is the client's own casting animation, not a record sprite** -- it appears
on any caster, including the NPC Grennet_Stormhand while he cast his own
spell, which is what confounded run1's 1494 window and briefly looked like
evidence for the verbatim reading. **(b) `#castspell` is
`SpellFinished`**: it produces the impact but no cast bar, so a Source
stage measured under `#castspell` is the landing animation, not the cast.

### 3. B162 -- SETTLED: the digit is 0-BASED

`SpellsEffReader::effectNameToTexture()` takes the digit verbatim and
excludes `'0'` ("never resolves"). **Both halves are wrong.**

The two records that settle it were chosen because every non-empty sprite
slot in every stage holds ONE name, for a letter that has at least two
`spel<letter><n>.tga` files on disk, so the drawn particle can only have
come from that name:

| record | every slot | spell cast | verbatim would give | 0-based would give | **what the oracle drew** |
|---|---|---|---|---|---|
| 20 | `GENN00_SPRITE` | 41 Weaken | `speln0.tga` (does not exist) -> nothing | `speln1.tga` (green/orange mottled spheres) | **green/orange mottled spheres = `speln1.tga`** |
| 35 | `GENA10_SPRITE` | 253 Whirling Wind | `spela1.tga` (blue blob cloud) | `spela2.tga` (bright blue eight-point stars) | **bright blue eight-point stars = `spela2.tga`** |

Record 20 alone refutes the `'0'` exclusion (something drew where the
shipped rule says nothing can). Record 35 alone refutes the verbatim
digit. Together the rule is

    GEN<L><d>0_SPRITE  ->  spel<lowercase L><d+1>.tga

which is exactly T380's alternative reading: 36 of 40 names, 34 of them on
disk, 272 of 301 sprite slots (90%) instead of the shipped 157 (52%).
**Matching files**: `speln1.tga` (the primary), `spela2.tga` (the
corroboration), `spelo1.tga` (Pendril's, consistent), `spelu1.tga` (Frost
Bolt's `GENU00` Source swirls, consistent).

Crops: `oracle/T389_oracle_41_weaken_full_1024x768.png`,
`oracle/T389_oracle_253_GENA10_crop3.5x.png`; the candidate files enlarged
are `sprites/speln1_big.png` and `sprites/montage_discriminators.png`
(spela1, spela2, speld1, speld2, spelb1, spelb2).

**`effectNameToTexture()` was NOT changed here.** Fixing it changes what
157 -> 272 sprite slots resolve to across 8,066 spells, and nothing
downstream can draw a `spel*.tga` yet (see B225 below); it is a separate
unit. Boarded on B162.

### 4. B022 / Leg B3 -- 1494 vs 6594: THEY DO NOT DIFFER, so no STOP

`4243 Intoxication I` (affect index **70**, EmptyRecord, self-target,
effect 10 = null, so it lands harmlessly) and `6594 GM Cleansing` (affect
index **-1**, NoAffectIndex) were cast back to back in the same session,
20 s apart, from the same spot and camera. Both draw **the same generic
bright-blue dome of point sprites**, for the same 2.8 s, differing only in
peak brightness (B .523 vs .624 -- 6594 is a group buff and 4243 is a
single-target debuff). `1494 Flame Jet` (index 71, the other byte-identical
template) drew the same blue generic effect in run3, confounded only as to
duration.

So **EmptyRecord must behave exactly like NoAffectIndex**, and the three
`!visual.isFallback() && !visual.hasXStage()` gates in
`spell_visual_fx.cpp` are correct as written for both fallback classes:
`isFallback()` is true for each, so neither is ever suppressed. The
Leg A item 2 STOP condition did **not** fire. Frames:
`oracle/T389_oracle_6594_full_1024x768.png`,
`oracle/T389_oracle_1494_full_1024x768.png`.

### 5. The projectile -- NOT MEASURED, and why

Four attempts, none of which produced a Frost Bolt in flight between a
caster and a distant target. The reasons are worth keeping so the next
unit does not repeat them:

1. `#castspell` calls `SpellFinished` -- there is no travel stage to see.
   The Middle stage needs a real gem cast.
2. A hostile target closes to melee in seconds. In run4 Grennet_Stormhand
   was already adjacent by the first cast; in run5 the spawned `Fbtarget`
   punched the caster continuously, which **interrupts a gem cast**.
3. `#goto [x y z] [h]` (eqemu `zone/gm_commands/goto.cpp`) is the reliable
   way to place and aim: a heading written to `character_data` did NOT take
   on zone-in (run4). The convention, derived over runs 1-4 and confirmed
   in run6, is **`world_angle_atan2 = heading * (360/512) + 90`, with
   +X = north and +Y = east** -- so `h=128` faces -X (south) and `h=384`
   faces +X (north).
4. Backpedalling to make distance walked the character out of the zone
   (run3, `qeynos2` -> `qeynos`).

The recipe that should work next time: a real gem cast at a target ~90
units away that has not been aggroed, placed with `#goto x y z h`, cast
immediately with no intervening screenshots. Boarded as **B227**.

### 6. Leg B -- the smallest match

`GEND10_SPRITE` and `GENU10_SPRITE`, the two names in Frost Bolt's Target
stage, now map to atlas tiles in
`SpellEffectsConfig::buildTextureNameMap()`, and
`SpellVisualFX::createImpact()` / `createImpactAtPosition()` route the
resolved Target stage's raw sprite names into
`EmitterConfig::textureRegions[]` through the new
`SpellPresets::applyStageSpriteNames()`. An unmapped name still falls
through `getTextureIndex()`'s existing `SoftCircle` default, so nothing
degrades.

**The comparison, tile against crop** (the atlas shapes are read from
`generateParticleAtlas()` in `src/client/graphics/texture_generators.cpp`,
because the shipped `.bmp` has lost them -- see B225):

| measured name | file (0-based rule) | what it looks like | chosen tile | why |
|---|---|---|---|---|
| `GEND10_SPRITE` | `speld2.tga` | cyan **eight**-pointed star, bright core, soft radial glow | `SpellSparkle` (8) | the only tile drawn as a pointed star *with* a bright core *and* a soft glow; its `2 + 5*abs(cos(4*theta))^3` lobe function gives **eight** lobes, matching the point count |
| `GENU10_SPRITE` | `spelu2.tga` | small blue diagonal **cross** with a bright white square core | `IceCrystal` (14) | the only angular, pale-blue tile (`200,230,255`); the cross arms have no counterpart in the sixteen at all -- this is the weaker of the two matches and is recorded as such |

Not mapped, deliberately: `GENU00_SPRITE` (`spelu1.tga`, purple ribbon
strands). **No tile in the atlas resembles a ribbon**, so it keeps the
`SoftCircle` fallback. `ParticleAtlas` was NOT extended past 16 tiles and
no `spel*.tga` is loaded -- the plan's STOP holds.

`tests/test_spell_target_stage_textures.cpp`, 8 tests, pure logic (two
Environment TUs, no renderer). **6 failed before the fix** (with only the
two `textureNameMap_` lines reverted) and **8 pass after**.

### 7. The fable side, and what it still does not show

fable run1 (Testcaster, 1024x768) fired all five `#castspell`s and the gem
cast, and with T395 merged the chain now reaches the worker every time --
`ParticleManager: Enqueued CreateSpellEffect 'SpellComplete'` +
`'Impact'` and `SimWorker: Created spell effect 'Impact'` for each of the
five, which is the half of T395's B204 fix that this session confirms
live. **But no impact burst is resolvable in the frames.** fb54 repeated
Frost Bolt six times at 640x480, 607 frames at a measured 3.22 fps: the
per-frame changed-pixel count in the 3D area (420x280 around the caster,
UI excluded) sits at 300-1,400 from idle animation and grass, and the
largest post-cast excursion (frame 15, 3,617) is nameplate and text
churn, not a burst -- `fable/T389_fable_54_impact_crop4x.png` beside
`fable/T389_fable_54_before_crop4x.png` shows nothing.

The arithmetic says why, and it is a magnitude gap rather than a wiring
gap: the `impact` style is **36 particles of 0.05-0.15 world units for
0.4-0.8 s**, where the oracle paints hundreds of sprites across a third of
the screen for 2-3 s. Boarded as **B226** with the frame pair
(`fable/T389_fable_54_impact_640x480.png` vs
`oracle/T389_oracle_54_self_full_1024x768.png`).

### 8. T386 item 3 (fire vs frost on an observing client) -- NOT TAKEN

The two-client rig was never brought up: the seven oracle sessions took
the session's budget, and the fable runs were single-client. B160's owed
observer pair is still owed. What this unit *can* add is the oracle half
of the same question, measured: Burst of Flame is R-dominant
(.360/.379/.199) and Frost Bolt is B-dominant (.213/.284/.315) on the
official client, so the two colours are real in the oracle and the pair is
worth taking once B226 makes fable's burst visible at all.

### 9. Verification

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2-config tree) | **exit 0** |
| `ctest --test-dir build -j6 --output-on-failure` | **100% passed, 0 failed out of 4092** |
| new test pre-fix | **exit 1, 6 of 8 failed** |
| new test post-fix | **exit 0, 8 passed** |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/ci/visual_gate.sh` | **FAIL**, goldens untouched -- and the cause is outside every checkout: `/mnt/projects/EverQuest_ETC1/willeq.eqe1` was rewritten on the shared NFS asset directory at 21:20 today and grew 239 bytes, so all three memory-coverage zones miss `eqe1MappedBytes` by exactly 239. Golden images match, CPU p95 under ceiling in all three zones, budget block clean. Boarded **B228**; a re-bless is Will's call and was not self-blessed |
| `scripts/build-arm-noble.sh` | **exit 0**, `build-arm-noble/bin/willeq` = ELF 32-bit ARM EABI5 |
| `scripts/refactor/check_file_sizes.sh fable` | 17 checked, **17 ok, 0 warn, 0 fail** |

### 10. Server state changed, and restored

| value | snapshot at unit start | restored |
|---|---|---|
| `eqtime` | minute 26, hour 24, day 6, month 4, year 4004, realtime 1788601087 | restored with the world DOWN, verified by SELECT |
| `character_data` Oraclealpha | zone 2, -74 / 428 / 3.751, h=160 | same |
| `character_data` Oracledelta | zone 2, -74 / 428 / 3.751, h=0 | same |
| `character_data` Testcaster | zone 36, -259 / -1201 / -5.75, h=0 | same |
| `character_spells` / `character_memmed_spells` Oracledelta | 0 / 0 | rows added by `#scribespells 12` + `#memspell` deleted |
| `character_spells` / `character_memmed_spells` Testcaster | 93 / 8 | unchanged (`#scribespells 12` added nothing a level-25 wizard did not have) |
| `character_data` Testchar | zone 2, -74 / 428 / 3.75, h=160 | untouched |
| `character_corpses` | ids 27-42 | unchanged, no new corpse |
| `npc_types` / `spawn2` | no `Fbtarget` row | unchanged -- `#spawn` NPCs are transient and did not persist |
