# T441 — the per-spell colour comes from the spell data (B398); measured per-spell styles (B397)

Class A/B, slot1, no server (T438 holds the lock). Read B398 and B397,
T422's §Review (the measured populations/sizes/colours per spell on the
OBSERVING client: 93 Burst of Flame 715 sprites, 41 Weaken 561, 253
Whirling Wind 25–36; the normalised RGB means), T389/T417/T420 §Reviews
(the sprite sheets, styles, atlas), `config/spell_effects.json`
(`resistColors`, `spellOverrides` = `{}`), `SpellEffectsConfig::
getStyleSettings(spellId)`, `getSpellColor`, and the data the official
client itself reads: `/mnt/projects/EverQuestP1999/spells_us.txt` (the
record for 41/93/54/253 — which columns select the particle sheet and
the colour; quote the column indices from an existing parser in the
tree or from the eqemu `spells_new` schema at
`/mnt/projects/akk-stack/code/common/spdat.h`, cited absolutely) and
`SpellEffects/*.eff` if those carry colour.

1. B398: find WHERE the official client's colour for a spell comes from
   (spell record field(s) → sheet/colour), and make fable's
   `getSpellColor` take the same input. Prove on the four measured
   spells: fable's normalised RGB (the T422 method, headless render of
   the burst on an EDT_NULL/gate device if the capture rig allows;
   otherwise the colour computed for each spell) lands on the oracle's
   ordering (41 green-dominant, 93 red, 54 blue, 253 not blue-heavy).
   If the data does not carry colour and the mapping is a table in the
   client binary, say so and add MEASURED per-spell overrides for the
   four spells only (never a guessed one) — the board says either closes.
2. B397: populate `spellOverrides` for 93, 41, 253 with T422's measured
   population and size (spawnRate/lifetime/size per the T420 model: the
   oracle ramps to N sprites with no deaths for ~2 s), leaving unmeasured
   spells on the style default. Test pins each override and that an
   unmeasured spell still resolves to the style.
3. Tests pre-fix failing: the colour test for 41; the override test.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
G05 in place if a pass changes, visual gate standalone + ARM (no image
golden should move — bursts are not in the gate frames; if one moves,
STOP with the sheet), file sizes. Strike B398/B397 or say exactly what
remains (the live observer re-capture is OWED to a lock unit — the duel
rig T422 built). New items B453+ (T442 in slot2 allocates from B453 too;
say which you used).

STOP: an image golden moves.

## Review

Branch `task/T441-spell-colour-styles`, base `9825e098`. slot1,
RelWithDebInfo, `EQT_GLES2_IRRLICHT_DIR` pointing at slot1's own Irrlicht.
**Offline unit** — T438 held the eqemu lock and no connection to 127.0.0.1:5998
was opened. **The live observer re-capture is OWED** (T422's duel rig).

**B398 and B397 are both STRUCK.** Two new items: **B453** and **B454** (T442
in slot2 allocates from B453 too; if it used them, renumber at merge).

### 1. Where the official client's per-spell colour comes from

**There is no per-spell colour, and B398's own premise — "a per-spell colour
lookup that is wrong for 41" — is refuted.** The chain the official client
walks is one of ART from end to end:

| step | evidence |
|---|---|
| `spells_us.txt` field **123 SPELLAFFECTINDEX** | `/mnt/projects/akk-stack/code/common/spdat.h:1653` — `uint16 spell_affect_index; // -- SPELLAFFECTINDEX` |
| the record at that **index** in `/mnt/projects/EverQuestP1999/spells.eff` | 256 fixed 2792-byte records, the index IS the position (T70, `include/client/spell/spells_eff_reader.h`) |
| the record's three 928-byte stage blocks, each opening with three 32-byte sprite-name slots | T70 |
| `GEN<L><d>0_SPRITE` → `SpellEffects/spel<l><d+1>.tga`, digit **0-based** | T389, MEASURED against the official client (board B162) |
| the particle is that file's own texels | T417/T420 ship all sixteen frames of all 33 files as `data/textures/spell_sprite_atlas.bmp` |

Read straight out of the shipped `spells.eff` (field 123 taken from
`spells_us.txt` for each spell):

| spell | field 123 | Source stage | Middle | Target |
|---|---|---|---|---|
| 41 Weaken | 20 | `GENN00` x3 | `GENN00` | `GENN00` x3 |
| 93 Burst of Flame | 0 | `GENC10`/`GENC30`/`GENC10` | `GENC30` | `GENC20`/`GENC10`/`GENG00` |
| 54 Frost Bolt | 23 | `GENU00` x3 | `GENA10`/`GENU10` | `GEND10`/`GENU10` x2 |
| 253 Whirling Wind | 35 | `GENA10` x3 | (empty) | `GENA10` x3 |

Nothing in the chain is a colour. Field **85 RESISTTYPE** is a mechanic — it
decides who resists — and the official client does not paint with it. The only
colour-shaped candidate inside `spells.eff` was already **REFUTED by T73**: the
`float[12]` at stage-block offset `0x370` reaches 32.0, over 12x the [0,2]
range its claimed decode requires (`spells_eff_reader.h`, "REFUTED (as
colour)"). `SpellEffects/*.eff` is a red herring — the `*.eff` files in the EQ
tree are `<zone>_sounds.eff` / `<zone>_sndbnk.eff` sound banks, not spell data.

**So the defect was not a lookup, it was a multiplication.** fable already drew
the right Target-stage sprites and then multiplied them by
`getSpellColor(spell_id)`, a resist/school colour —
`POINT_SPRITE_FS` computes `vec4 color = texColor * vColor;`
(`unified_particle_renderer.cpp:109`). Weaken is resist type 1 (magic), so
`speln1.tga`'s green discs went through `(200,100,255)` and came out red.

### 2. The fix

`SpellPresets::applyStageSpriteNames()` — the one place a real stage's sprites
are installed — now also sets `colorStart.rgb` and `colorEnd.rgb` to white.
The **alphas are untouched**: they are the style's fade curve, not a colour.
The resist/school colour survives exactly where no stage resolved (the ~946
spells T70 counted with no index or an empty record), because that is the one
case where fable has no art to take a colour from.

Three stages were being RESOLVED and then thrown away — `createCastGlow()`
read the Source stage only to decide whether to suppress the glow, and
`createProjectile()` the Middle stage likewise — so all three now route their
names, exactly as T389 did for Target. `createSpellComplete()` deliberately did
**not** gain a suppression gate: adding one would silently delete the burst for
every spell with an empty Source block, which no measurement supports.

### 3. Before / after, against the oracle

Normalised alpha-weighted mean RGB over every frame of every cell the Target
stage's emitter samples, on the SHIPPED sheet. "BEFORE (model)" applies the
resist tint the code applied; "BEFORE (live)" is T422's measurement on the
observing client, which is the same quantity read through an additive blend
and a background-median subtraction (so it is compressed toward neutral — the
DOMINANCE is what survives, not the magnitude).

| spell | BEFORE (model) | BEFORE (live, T422) | **AFTER (model)** | ORACLE (T389) |
|---|---|---|---|---|
| 41 Weaken | 0.578/0.422/0.000 **R** | 0.377/0.294/0.329 **R** | **0.407/0.593/0.000 G** | 0.448/0.572/0.191 **G** |
| 93 Burst of Flame | 0.803/0.177/0.021 **R** | 0.515/0.339/0.146 **R** | **0.625/0.292/0.083 R** | 0.360/0.379/0.199 |
| 54 Frost Bolt | 0.071/0.282/0.647 **B** | 0.228/0.363/0.409 **B** | **0.148/0.325/0.527 B** | 0.213/0.284/0.315 **B** |
| 253 Whirling Wind | 0.000/0.121/0.879 **B** | 0.299/0.248/0.453 **B** | **0.000/0.260/0.740 B** | 0.258/0.309/0.214 |

**41 flips to green-dominant, which is B398's headline, and it does so purely
from the data** — no colour was chosen. 93 stays red and 54 stays blue, the two
T422 recorded as already agreeing, and both move TOWARD the oracle. **253 does
not land**, and no rule fable can apply will make it: `spela2.tga` is
**zero-red**, so the oracle's measured R = 0.258 cannot come from any
multiplication of it. Boarded as **B453** (class C — it needs one live
re-measurement before code moves) rather than closed with an invented override.
**No colour override was added for any spell**: the data carried the colour, so
the plan's "measured overrides only if the data does not carry colour" arm
never fired.

### 4. B397 — the overrides

Style baseline is T422's own wide-burst tuning: impact 480/s and spellComplete
600/s over 0.6 s = 648 sprites, sizes measuring 5.53 px median, duration 2.6 s.

| spell | override | derivation | result |
|---|---|---|---|
| **93** | `impact.spawnRate` 530, `spellComplete.spawnRate` 662 | 715/648 = 1.1034 | peak **715.2** (oracle 715) |
| **41** | `impact.spawnRate` 416, `spellComplete.spawnRate` 519 | 561/648 = 0.8657 | peak **561.0** (oracle 561) |
| **253** | `spawnRate` 7.4 / 9.3, `emitterLifetime` 3.6, all four sizes x1.673 | steady N = rate x 1.8 s mean lifetime, split in the style's own 480:600 ratio; 3.6 + 2.0 = 5.6 s; 5.53 px onto the 8.0–10.5 px band's midpoint 9.25 | steady **30.1** (oracle 25–36), duration **5.6 s**, sizes **+67 %** |
| **54** | none | T422 could not separate its merged ribbon into components — there is no measured population to pin | resolves to the plain style, asserted |

93 and 41 name **only** `spawnRate`, so the measured sizes, lifetimes, expand
speed and texture regions still come from the style — the three-tier
hierarchy's contract, pinned rather than assumed. The emission SHAPE is
untouched and remains **B396**.

### 5. Tests

`tests/test_spell_colour_and_overrides.cpp`, 17 cases, registered in
`tests/cmake/spells.cmake` with the same source set as
`test_spell_target_stage_textures` (pure logic — no renderer, no device). It
reads the SHIPPED sprite sheet's pixels and, when the client data is mounted,
the real `spells.eff` to confirm the stage literals it rests on.

| | pre-fix | post-fix |
|---|---|---|
| whole binary | **10 of 17 RED** | **17 / 17 green** |

The ten reds: `InstallingAStagesArtDropsTheModulationToWhite`,
`WeakenIsGreenDominantLikeTheOracle`,
`WhirlingWindDrawsItsOwnArtUntintedAndIsStillBlue`,
`BurstOfFlameReachesItsOwnMeasuredPopulation`,
`WeakenReachesItsOwnMeasuredPopulation`,
`TheTwoWideBurstsAreNoLongerTheSameSize`,
`WhirlingWindIsASteadyTrickleNotAWideBurst`,
`WhirlingWindSpritesAreTheLargerMeasuredOnes`,
`WhirlingWindLastsAsLongAsTheOracles`, `AnOverrideOnlyMovesWhatItNames`.
The seven that pass in both directions are the invariants (the alpha ramp, the
no-stage fallback, 93 and 54 already agreeing, the spells.eff chain, the
unmeasured-spell default, the pool) and are there so a "fix" that breaks one is
a red test.

`WeakenIsGreenDominantLikeTheOracle` asserts the **negative** as well —
re-applying the magic tint must flip the dominance back to R — so a
reintroduced modulation is a red test rather than a purple burst.

### 6. G05 — two shipped tests learned the edit in place

Both changes were forced by the data, not by convenience:

1. **`CwdElsewhere.TheProductionStringLoadsTheShippedConfigFromAnyDirectory`**
   (`tests/test_data_paths.cpp`) discriminated shipped/fallback/unconfigured by
   the impact `spawnRate` read for **spell 93** — which now carries an override
   of 530. Its subject is which FILE was read, not any one spell, so it now
   asks for the STYLE (spell id 0, which `getStyleSettings()` documents as "no
   per-spell lookup"). Asking for 93 would have made a path test track a
   per-spell tuning number. Its sibling `AMissingConfigFileIsReportedAsAFailure`
   moved with it; `setDefaults()` clears `spellOverrides_`, so a failed load
   cannot leave the previous file's tuning standing either way.
2. **`ConfigKeyReachability.EveryShippedConfigKeyIsNamedByCode`** went red on
   `"93"`, `"41"`, `"253"` — three shipped keys no source names. They are DATA
   (spell ids), so `spell_effects.json`/`spellOverrides` joins `kDataKeyedMaps`
   naming its enumerating call site, `SpellEffectsConfig::loadSpellOverrides`,
   which walks `root.getMemberNames()` and `std::stoul`s each. The test's own
   failure message prescribes exactly this.

`SpellBurstOracleMatch`'s eight cases were re-run and are **unchanged and
green**: they read spell 93, whose override moves only `spawnRate`, and the
peak-count band (420–900) now reads 715.2 — the oracle's own number.

### 7. Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** (background + exit file; no exit-143 anywhere in this unit) |
| `ctest --test-dir build -j6` | **4,600 / 4,601** — the single failure is `DocCitations`, **pre-existing at the tip**, see below |
| new tests pre-fix / post-fix | **10 of 17 RED** → **17 / 17 green** |
| `visual_gate` under `ctest -j6` | FAIL, gfaydark p95 10.717 vs ceiling 10.672 — **0.4 % over, the documented load artefact** |
| `visual_gate` standalone | **PASS** (qeynos2 6.546 / gfaydark 9.335 / befallen 9.927, all well inside) |
| image goldens | **untouched** — `git status` clean of `tests/goldens`; bursts are not in the gate frames |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `scripts/refactor/check_file_sizes.sh fable` | **5 checked, 5 ok, 0 fail** (largest touched: `spell_visual_fx.cpp` 915); the new test file is 482 |

**`DocCitations` is red on the base tip `9825e098` and not on anything T441
touched.** All five failing citations are inside the **B452** board entry the
T442 plan commit added, which writes renderer component paths without their
`src/client/graphics/` prefix (`zone_lifecycle/renderer_zone_lifecycle.cpp:180`,
`zone_load/renderer_zone_load.cpp:426`,
`zone_lifecycle/renderer_zone_draw.cpp:321`,
`diagnostics/diagnostics_component.cpp:214`,
`diagnostics/diagnostics_debug.cpp:282`). B452 is T442's territory in slot2, so
it was left alone rather than fixed into a merge conflict. **The orchestrator
should not read the tip as green.**

### 8. Deviations and surprises, flagged not absorbed

1. **The plan's fallback arm never fired.** It allowed measured per-spell colour
   overrides "if the data does not carry colour and the mapping is a table in
   the client binary". The data DOES carry it, as art, so no colour override
   was written for any spell — including 253, where an override is the only way
   to reach the oracle's number and is therefore exactly the guess Will's ruling
   forbids.
2. **Three stages beyond the plan's letter now route sprite names.** The plan
   named the colour source; taking it consistently means the cast glow, the
   spellComplete burst and the projectile trail can no longer be tinted while
   the impact is not. Their pixels are UNMEASURED against the oracle (T422 used
   `#castspell`, which sends no `OP_BeginCast`, and self-casts, which have no
   travel stage) — boarded as **B454** rather than claimed.
3. **`spellComplete` changed appearance more than any other effect** and is the
   one to look at first in the owed capture: it went from procedural white
   sparkles tinted by resist type to the Source stage's own art (for 93:
   `spelc2`/`spelc4`/`spelc2`).
4. **The surprise: `SpellEffects/` was the wrong place to look for colour, and
   the `.eff` files in the EQ root are sound banks.** The colour question was
   already answered by work in the tree — T70's reader, T389's measured 0-based
   rule and T417's sheet — and the remaining defect was one multiply. The
   reason it survived T389, T417, T420 and T422 is that every one of those
   units was looking at WHICH sprite drew, and the tint is invisible until you
   compare a normalised triple against the oracle's.
5. **T422's oracle numbers are compressed toward neutral** by additive
   saturation and background subtraction (93's oracle reading is
   0.360/0.379/0.199 where its art is 0.625/0.292/0.083). Dominance is
   comparable across the two; magnitude is not. That is why every assertion
   here is on dominance or on an explicit band.
