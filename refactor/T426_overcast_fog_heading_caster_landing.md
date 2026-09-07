# T426 — the overcast fog swap (B360); a server heading reaches the camera (B399); the caster's own landing draws (B400)

Class A ×3; eqemu lock (atomic; must be ABSENT); slot1. Read B360
(T418: the oracle's weather is a FOG COLOUR + SKY swap — near wall ×1.42
R, far ×2.73 R, sky blue → grey; fable has `getWeatherFogColor()`,
`getFogDensityModifier()`, `getRainFogSettings()`, `SkyRenderer` all
unmeasured), B399 (`#goto x y z h` sets the heading once and the client's
own heading echo overwrites it with 480 — every rig has fought this),
B400 (a landing draws on the observer but intermittently not on the
caster's own client), T418's rig and README (`review/T418/scripts/`,
`#weather 3 1 <i>`), T405's rig, T422's rig and traps (`video started`
mark lags ffmpeg ~5 s; `scan_excursion.py`).

## Leg A — B399 first (it fixes the rig for Leg B)

Read the heading flow: `[ZONE-IN] setPlayerPosition heading=160` then
`480` — the client's own movement echo (`OP_ClientUpdate` round trip or
the local integrator) reapplies the pre-goto heading. Find the writer
that overrides a server-authoritative position update and make a
server relocation win (the same shape T404 used for the self entity
row: the server's relocation is authoritative until the player moves).
Test pre-fix failing: a synthetic `#goto`-style position packet with a
new heading → the camera yaw equals it on the next frame and stays
until input. Live: `#goto -74 428 3.751 160` turns the camera; the T405
composition control passes without hand-turning.

## Leg B — B360 (the fog swap, live A/B)

With heading now pinned by `#goto`: at T418's spot, day and night, rain 5
and clear, capture both clients (30 fps grab; note the oracle renders
~5/s). Measure per depth strip (T418's three strips) and the sky: the
oracle's fog colour and the distance at which the ratio saturates. Then
A/B fable's `getWeatherFogColor()` / fog start-end / sky overcast one
parameter at a time until each strip and the sky match within the noise
floor; every value carries its probe. Tests pin the solved values;
strike B360.

## Leg C — B400

Reproduce with the caster capture: which path differs between caster
and observer for the same `OP_Action`? (The caster also gets the
`effect_flag=4` repeat — does the second packet's arm REMOVE the effect
the first created? T397's per-entity removal, T395's keying.) Headless
test pre-fix failing: caster receives 0 then 4 for its own cast → the
impact effect is alive at +0.5 s. Fix; live re-check on the caster
capture. Strike B400.

Restore every server value (world DOWN); `down.sh`; delete the lock.

STOP: lock present; Leg A's writer is the movement integrator's own
prediction (then report the shape — a prediction that ignores server
relocation is a design point, not a one-line fix).

---

## Review

Branch `task/T426-fog-heading-caster-landing`, base `8f5e586a`. Media and the
full tables: `/mnt/projects/fable/review/T426/` (`README.md` first).

### Leg A (B399) — the stated mechanism is REFUTED; a different, real defect fixed

**`480` is not an echo of `160`; it IS `160`.** A player's own heading takes the
`90 - server_deg` flip at every inbound edge and `SendPositionUpdate` inverts it
on the way out. Wire 160 -> 112.5 deg -> 337.5 deg -> 480 in the 512 format.
An eight-heading `#goto` sweep (`review/T426/logs/fable_sweep1.log`) gives eight
distinct camera poses: 160->480, 288->352, 32->96, 416->224, 96->32, 224->416,
352->288, 0->128. Nothing overwrites anything.

**And the camera aims where the server aims it.** Six-heading first-person A/B
against the official client at the same spot and the same `#goto`
(`review/T426/probe/T426_fp_pairs.png`, wire h = 0/128/256/384/160/288): the two
clients match row for row. The official client's own compass in those same
frames calibrates the wire heading independently -- h=0 -> N, 128 -> W,
256 -> S, 384 -> E, dead centre
(`review/T426/probe/T426_oracle_compass_sheet.png`).

**The framing trap that produced B399**: the oracle rig leaves the official
client in FIRST person; fable's default is third-person follow, 20 units back.
At one heading that offset can fill fable's frame with a wall the oracle sees
past. T426's own first cut fell for it in the opposite direction and only the
first-person pair settled it. Compare first person to first person
(`/camera fp`).

**The real defect, fixed.** `EntitiesComponent::ZoneProcessClientUpdate`'s SELF
branch computes `self_heading = 90 - server_h` into `ctx_.self` and then handed
`BridgeComponent::OnSpawnMovedGraphics` the RAW `h_player`, which that function
converts to the 512 format and publishes as `PlayerMoved` -- straight into
`setPlayerPosition` and the camera. One argument was serving two conventions
(flipped for our camera, verbatim for the entity row -- an asymmetry
`docs/CLAUDE.md` forbids unifying). Live at zone-in the camera took 480, then
160, then 480. Any server-forced relocation arriving as a self `OP_ClientUpdate`
without a zone-change packet aimed the camera wrong and left it there.

Fix: read `ctx_.self.HeadingDeg()` instead of the argument, in
`src/client/bridge/bridge_component.cpp`. Test
`tests/test_self_relocation_heading.cpp`, 3 cases:

| case | pre-fix | post-fix |
|---|---|---|
| `TheSelfStoreTakesTheInternalConvention` | pass | pass |
| `AServerRelocationReachesTheCameraInTheInternalConvention` | **FAIL** (160, expected 480) | pass |
| `TheEntityRowStillCarriesTheServerHeadingVerbatim` | pass | pass |

`logs/pre_self_relocation.txt` / `logs/post_self_relocation.txt`.

### Leg C (B400) — reproduced deterministically and fixed

eqemu sends TWO `OP_Action` packets for one cast (T395): `effect_flag 0` to
everyone in range, `effect_flag 4` to target and caster only. The bridge's
`SpellCastVisualComplete` arm runs twice on the caster, once on an observer, and
calls `removeCastGlow(casterId)` unconditionally before the `isCastAnimation`
gate that creates the burst. `removeCastGlow` issued
`removeSpellEffectsForEntity()`, whose worker arm removes every effect whose
CASTER **or** TARGET is that entity -- and a self-target cast puts the caster's
spawn id in both. The second packet's teardown destroyed the impact and
spell-complete bursts the first had just created; the observer, never receiving
the second packet, kept its copy. That is T422's measured asymmetry exactly.
B400's suspicion (T395's attach-position protocol against the frustum test) is
refuted.

Fix: the removal command carries an optional style filter over
`SpellEffectDef::name`; empty keeps the old "everything on this entity"
behaviour, and `removeCastGlow` names `{"CastGlow", "CastSpray"}`. Touched
`simulation_worker.h`, `sim_particles.cpp`, `particle_manager.{h,cpp}`,
`spell_visual_fx_nodes.cpp`.

Test `tests/test_caster_own_landing.cpp`, 4 cases on the real chain (a real
`SpellVisualFX` on an EDT_NULL device -> a real `ParticleManager` -> a real
`SimulationWorker`; no hand-built commands):

| case | pre-fix | post-fix |
|---|---|---|
| `TheLandingIsLiveAfterTheCastAnimationPacket` | pass | pass |
| `SelfCastLandingSurvivesTheSuccessRepeatTeardown` | **FAIL** | pass |
| `TheCastGlowItselfIsStillRemoved` | pass | pass |
| `AGlowTeardownRemovesTheGlowAndNothingElse` | **FAIL** | pass |

`logs/pre_caster_landing.txt` / `logs/post_caster_landing.txt`. T397's
`test_spell_effect_removal` (the unfiltered contract) stays green.

One new test seam: `ParticleManager::Test_SetUnifiedRendererInitializedForSeam`.
`initUnifiedRenderer()` needs a live GL context and `createSpellEffect()`
refuses to enqueue without the flag it sets, so a headless test of the real
chain could not get one command out of the manager.

**Owed**: the live caster/observer re-capture of the four spells. T426 spent its
live budget on Leg A's oracle A/B and Leg B's fog table. Noted on the struck
B400.

### Leg B (B360) — measured, NOT solved. DEVIATION.

The plan asked for the values to be solved and B360 struck. They are not, and it
is not. What was taken is the thing B360 says is missing and that nobody could
take before Leg A settled the framing: the target table, on matched first-person
framing, both clients, day and night, rain 5 against its own clear.
`review/T426/logs/T426_fog_table.txt`; ten 50px wall strips (band y=180..300,
near x=40 -> far x=490) plus the sky patch, linear light.

* **Day geometry.** ORACLE R 1.436 -> **2.544**, G 1.037 -> 1.401,
  B 1.001 -> 1.114 (reproduces T418's own 1.42 -> 2.73). **fable 1.000 +- 0.001
  everywhere** while demonstrably in rain 5 -- fable's weather fog reaches no
  pixel, because `blendFactor = getCurrentDarkening()` and T418 flattened the
  darkening curve to 1.000 at every knot. T418's comment at that line predicted
  the hazard; the measurement says it fires.
* **Day sky.** ORACLE (0.2141, 0.3585, 0.5117) blue -> (0.3160, 0.3159, 0.3933)
  grey, i.e. R x1.475 / G x0.881 / B x0.769. **fable x0.065 / x0.067 / x0.074**
  -- a 15x darkening where the oracle brightens R and greys the hue. Producer:
  `skyBrightness = 0.5*pow(0.5,(intensity-1)/4)` into `setWeatherBrightness()`.
* **Night.** The oracle's night geometry term is below 8-bit quantisation at
  this crop (nine of ten strips byte-identical), so there is no night geometry
  target. The night sky moves the same way (deep blue -> grey); fable's goes to 0.
* **Why nothing was committed.** The oracle's day term does not fit
  `active = clear(1-f) + F*f` with one `F`: least squares over the ten strips
  gives `F ~ (0.139, 0.119, 0.098)` linear with an inadmissible `f = 2.09` on
  the near strip and residuals to 0.022. Near-strip increments (0.050, 0.004,
  0.000) and far-strip (0.056, 0.024, 0.007) cannot come from one `F`; an
  additive model fails identically. The oracle's weather is not a
  one-parameter fog lerp over this scene, so the remaining work is a model, not
  a sweep -- and committing numbers picked to make one crop match is exactly
  what B360 exists to stop.

B360 stays OPEN, rewritten with the table, the two mechanisms, the rig and the
strip band that makes the ramp monotone.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | exit 0 |
| `ctest --test-dir build -j6` | **4,395 / 4,397**; the two reds are the two named in the dispatch (see below) |
| `scripts/build-gles2-x86.sh` | exit 0 |
| visual gate | **FAIL, expected shape only**: `eqe1MappedBytes mismatch` by exactly **-378** in all three memory-coverage zones, no image golden moved, no CPU-ceiling miss |
| `scripts/build-arm-noble.sh` | exit 0 |
| `scripts/refactor/check_file_sizes.sh fable` | no new FAILs |

The two ctest reds:

* `PackReproducible` -- built hash **equals** the pinned hash; only the SHIPPED
  pack differs (`19e78745...` installed vs `14a6bd45...` built/pinned). That is
  T424's pack on the shared path against a branch cut before T424. Nothing in
  this unit writes the pack.
* `visual_gate` -- the same T424 cause, `eqe1MappedBytes` short by 378 bytes in
  every zone.

### Server

Snapshot `review/T426/server_snapshot_before.txt`. Changed and restored:
`zone.rain_chance1..4` for `qeynos2` (10 -> 0 for the weather block -> 10);
world clock repeatedly re-pinned by `#set time`, which is not persisted;
`character_data` poses untouched (`#goto` moves are in-memory and every session
ended with `/camp`). Restored with the world DOWN and verified by `SELECT`.
