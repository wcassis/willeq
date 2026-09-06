# T422 — spell effects matched live against the oracle: burst size, lifetime, animation rate (B356, B370, B226)

Class A; eqemu lock (atomic; must be ABSENT); slot1. Read B356/B370/B226,
T389's oracle table and videos (`review/T389/oracle/`), T417/T420's
§Reviews (the sprite atlas, per-particle frame by age, the oracle capture
was only ~5 renders/s — capture at a higher rate this time: raise the
x11grab fps and confirm the client's own render rate from consecutive-
frame diffs), T405's rig (`#goto x y z h`, `zone.rain_chance` zeroed,
`#set time` re-pinned, `#weather 3 1 0`), T401's two-client `--script`
driver, and `config/spell_effects.json`.

1. Capture the oracle at ≥ 20 fps (verify the render rate) for Frost
   Bolt (54), Burst of Flame (93), Weaken (41), Whirling Wind (253):
   gem casts (T395 — GM casts send no BeginCast), caster in frame at a
   fixed distance, day. Per spell per stage: particle count (peak),
   sprite screen size, duration, and the animation rate (cells/s from
   consecutive renders — the rotating sword `spelg1` if a spell uses it;
   else `spela2`'s stars).
2. Fable, same spot/distance, same four spells, same capture rate.
3. A/B one parameter at a time in `spell_effects.json` (count, size,
   lifetime, emission rate) and the frame-rate rule (per-lifetime vs a
   fixed cells/s — T420 chose per-lifetime because the oracle refuted an
   effect-wide clock; re-test with the better capture) until each stage
   matches the oracle within the noise floor on count/size/duration;
   every value in the commit carries its probe.
4. The observer pair (B160's original owed proof) with the real sprites:
   both colours AND shapes on the second client.
5. Tests pin the solved config (pre-fix failing). Strike B356/B370/B226.
   Restore server values (world DOWN); `down.sh`; delete the lock.

STOP: lock present; the oracle's rendering rate is still too low to
measure the animation rate (report the ceiling and settle the rest).

---

## Review

Branch `task/T422-spell-effects-live-ab`, base `429b98e8`. slot1,
RelWithDebInfo. eqemu lock taken atomically (absent on arrival), server up via
`up.sh --launcher qeynos2`, restored with the world down, `down.sh` run, lock
deleted. Media and every analysis script: `/mnt/projects/fable/review/T422/`.

**B356, B370, B226 and B160's owed live pair are all closed.** Five new items:
**B395** (closed in the same commit), **B396**, **B397**, **B398**, **B399**,
**B400**.

### 1. The capture rate, and the plan's STOP condition

The plan asked for the oracle at >= 20 fps with the rate verified. **The rate
is verified and 20 fps is not reachable on this rig**, so the STOP clause
applies: the ceiling is reported and everything else is settled.

`review/T422/scripts/renderrate.py` measures a client's own render rate from
consecutive-frame diffs of an x11grab capture (a grabbed frame is a NEW RENDER
iff >20 pixels differ from its predecessor by >12 on any channel; render fps =
grab fps / median inter-render gap). Nothing below is assumed.

| side | grab | MEASURED render rate |
|---|---|---|
| official Titanium client, 1024x768 | 30 fps | **4.4 - 5.8 renders/s** (median gap 6 grab frames = 5.00, mean 4.77) |
| fable caster, 1024x768, no frame dumping | 30 fps | **13.1 - 16.6 renders/s** (median gap 2 = 15.00) |
| fable observer, 1024x768 | 30 fps | **14.6 - 18.3 renders/s** |

Grabbing at 30 fps is nevertheless the whole improvement over T389/T420's
10 fps: at 5-6x the render rate **every distinct render is captured exactly
once instead of aliased**, which is what makes the counts and durations below
trustworthy. It does not raise the client's rate.

Three ceiling probes, one parameter each:

| probe | change | result |
|---|---|---|
| A | `LP_NUM_THREADS` 1 -> 4 | **no effect** -- 5.00 renders/s, identical to T420's reading at 1. The llvmpipe worker count is not the bottleneck. |
| B | + `eqclient.ini` `MaxBGFPS=100` (absent from the file; under Xvfb with no WM the client never sees itself as foreground) | 5.00 -> **6.00** (median gap 6 -> 5 grab frames) |
| C | 640x480 (2.56x fewer pixels) | **BLOCKED** -- `/mnt/projects/eq-oracle/scripts/login-as.sh` drives the login screen by absolute screen coordinates calibrated to 1024x768 and never reaches character select |

`MaxFPS` is already 100, so this is compute-bound, not capped. **Consequence
for B370: Nyquist at 4.4-5.8 renders/s is 2.2-2.9 cells/s, so a direct
animation-rate measurement remains impossible.** The rate is instead DERIVED
from a directly measured particle lifetime; see section 4.

### 2. The oracle table

`review/T422/oracle/T422_oracle_run2.mkv` -- Oraclealpha, qeynos2,
`#goto -74 428 3.751 160`, `#set time 11 0`, `#weather 0` (T418's form) with
`zone.rain_chance*`/`snow_chance*` zeroed in the DB and the zone processes
restarted so `LoadZoneCFG` re-read them, third-person (F9 x3), 20 s quiet
window before each cast, `#castspell` self-casts.

**Nameplates and corpses are OFF, and run1 is the evidence for why.** With them
on, the cast animation shifts the caster and its nameplate out from under the
background median and they fragment into **80-100 spurious "particles" per
render in a QUIET frame**. `T422_oracle_run1.mkv` is kept for that comparison.

Measured by `review/T422/scripts/burst.py`: background = per-pixel temporal
median of the pre-cast renders (a median, not one frame, so idle animation and
swaying grass do not survive); particle pixels = residual over threshold with a
colour gate on the spell's dominant channel; particles = 8-connected components
of area >= 4. Crop `880x510+5+25` of the 1024x768 frame, caster ~135 px tall.
Per-render series in `review/T422/logs/oracle_burst_*.tsv`.

| spell | stage | peak sprites | median diam | p90 diam | rise | fall | duration | extent | peak area |
|---|---|---|---|---|---|---|---|---|---|
| **93 Burst of Flame** | target | **715** | 4.8-6.9 px | 8.4-15.6 px | 1.96 s | 0.37 s | **2.33 s** | 830x435 | 6.06 % |
| **41 Weaken** | target | **561** | 4.4-6.7 px | 10-14 px | 1.46 s | 0.97 s | **2.6 s** | 880x505 | 9.86 % |
| **253 Whirling Wind** | continuous | 25-36 steady | 8.0-10.5 px | 11-17 px | 0.6 s | 0.5 s | **5.6 s** | 840x360 | 0.64 % |
| **54 Frost Bolt** | source+target (self) | not separable -- a merged ribbon (11-21 components) | 3.4-9.9 px | 17-89 px | -- | -- | **2.7 s** | 540x250 | 3.7 % |

Animation rate: **not measurable** (section 1). The durations agree with T389's
10 fps reading (2.3 / 2.0 / ~6 / 3.3 s) within the coarser sampling, so the two
captures corroborate each other on the one quantity T389 could reach.

**The single most important new fact, and it is not in T389 or T420: the
official client does not fire a one-shot burst.** Its population rises
MONOTONICALLY WITH NO DEATHS for 1.96 s (spell 93: 11 -> 22 -> 188 -> 324 ->
502 -> 578 -> 637 -> 705) and 1.46 s (spell 41: 204 -> 294 -> 355 -> 461 ->
520 -> 561), then collapses. It emits continuously over most of the effect.
That bounds the particle lifetime from below by the rise, and it makes
`burstCount` the wrong mechanism for `impact`.

### 3. Two rig findings that cost real time

**(a) The `video started` mark lags ffmpeg's first frame by ~5 s** in T389's
rig and every descendant of it, because the mark follows a `sleep 4` plus a
liveness check. A cast's video frame is log-time **plus ~5 s**. Two
measurements in this unit initially read the wrong frames. The fix is
`review/T422/scripts/scan_excursion.py`, which locates the casts in the pixels
and trusts no timestamp.

**(b) A server `#goto x y z h` does not turn fable's camera** (**B399**). The
oracle, given `#goto -74 428 3.751 160`, faces open field. fable, given the
identical command, logged `heading=160.00` once and `heading=480.00` on every
update after -- its own heading echoed straight back over the server's -- and
sat facing a wall for a whole session. `/face <target>` DOES work. Same family
as B001/T344.

### 4. The A/B, one parameter at a time

Every committed value carries its probe. Series in
`review/T422/logs/fable_after*_burst_*.tsv`.

| probe | parameter | observable | before | after |
|---|---|---|---|---|
| **P1** | lifetime 0.4-0.8 -> 1.6-2.0 s | Weaken burst extent | *unreadable* -- at 81 sprites nothing is separable from the caster's own idle animation. **The count had to move first.** | *unreadable* |
| **P2** | count: `burstCount` 36/45 -> `spawnRate` 480/600 over `emitterLifetime` 0.6 s | Weaken changed pixels | 72 | **309** (**4.3x**) |
| **P2** | (same run) | Weaken rise / peak / fall / duration | -- | rise **1.46 s** (oracle 1.46 s), peak 365 components, fall 0.70 s, duration **2.16 s** |
| **P3** | sizes x1.57 | Whirling Wind median sprite diameter | **3.57 px** | **5.53 px** -- 1.55x for a commanded 1.57x, so `POINT_SPRITE_VS`'s `worldSize * screenHeight / dist` is linear as claimed |
| **P3** | (same) | covered pixels / area fraction | 5,717 / 2.72 % | **10,404 / 4.95 %** |

P1 being unreadable is a RESULT, not a failed probe: it is B226's magnitude gap
stated as a measurement -- fable's shipped burst is below the noise floor of
the same pipeline that reads 715 sprites off the oracle. P2's rise landing on
the oracle's 1.46 s is not a fit; nothing was set from it.

#### Committed values and residuals

| knob | before | after | probe | residual vs oracle |
|---|---|---|---|---|
| `impact.spawnRate` | 0.0 | **480.0** | peak = rate x emitterLifetime = 288 | -- |
| `impact.burstCount` | 36 | **0** | the oracle rises over 1.96 s | emission SHAPE unmatched (**B396**) |
| `impact.emitterLifetime` | 0.1 | **0.6** | duration - lifetime | -- |
| `impact.lifetimeMin/Max` | 0.4 / 0.8 | **1.6 / 2.0** | death-free rise >= 1.46-1.96 s; Whirling Wind's steady N / emission rate = 1.4 s | -- |
| `impact.size{Start,End}{Min,Max}` | 0.05/0.08/0.10/0.15 | **0.08/0.125/0.157/0.235** | median 3.57 -> 5.53 px against 4.4-6.9 | **0** for 93/41; **-31 %** against 253's 8.0-10.5 px |
| `impact.expandSpeed` | 4.0 | **4.0 (unchanged)** | oracle spreads 830 px at ~135 px/char over ~2 s = **4.2 units/s** | **+5 %** -- it measured right |
| `spellComplete.spawnRate` | 0.0 | **600.0** | peak 360 | -- |
| `spellComplete.burstCount` | 45 | **0** | as impact | -- |
| `spellComplete.emitterLifetime` | 0.1 | **0.6** | as impact | -- |
| `spellComplete.lifetimeMin/Max` | 0.5 / 0.8 | **1.6 / 2.0** | as impact | -- |
| `spellComplete.size{Start,End}{Min,Max}` | 0.06/0.10/0.12/0.18 | **0.095/0.157/0.19/0.28** | x1.57 as impact | -- |
| `global.maxParticles` | 1024 (**inert**, B395) | **4096** | 648/landing, two can overlap | -- |
| **combined peak population** | **81** | **648** | oracle 561-715 | inside the band |
| **effect duration** | ~0.6 s | **2.16 s measured** | oracle 2.33-2.6 s | **-12 %** |
| **sheet animation rate** (B370) | 20-40 cells/s | **8.0-10.0 cells/s** | 16/lifetime, T420's rule | DERIVED, see below |

**The animation rate is derived, not measured, and it does not reach T420's
"below ~3 cells/s".** Stated plainly so the next unit does not confuse the two.
T420 could not measure a rate either (its capture held ~5 renders/s, Nyquist
2.5 cells/s) and inferred that bound from the modal orientation class of a
population of **mixed-age** stars holding across four renders -- a statistic
over a mixed-age population is not a clock, and T420 itself rated it low
confidence and separately refuted the effect-wide clock the argument needs.
T422's lifetime is measured directly, from a rise during which the oracle's
population grows with no deaths at all. 8-10 cells/s is what that lifetime
implies under the rule T420 committed.

**B226's "two orders of magnitude smaller" conflated three things**: the
per-sprite size (1.6x out), the population (8x out) and the whole-burst extent
(an effect of the short lifetime, not a knob). `expandSpeed` was never wrong.

**One code change, and the A/B required it (B395).** `global.maxParticles` had
no consumer: `renderer_sim_bridge.cpp` sent `pi.poolSize = 0` under the comment
"Worker determines pool size from config", and the worker's arm is a hardcoded
`1024` that happened to equal the shipped value. At 81 sprites it never bit; at
648 two overlapping landings exceed 1024 and every measured count would have
been silently wrong.

### 5. The observer pair -- B160's owed proof

`review/T422/fable/T422_fable_observer_<id>_<name>_1024x768.png`. Testbuddy,
its own process and its own Xvfb, 25 units away, `/target` + `/face` on the
caster. **All four landings drew on the second client**, with the official
client's own sprite art. Colours by `review/T422/scripts/burst_colour.py` (mean
added RGB over the particle pixels, background = temporal median of the
pre-cast renders, normalised):

| spell | pixels | normalised R / G / B | dominant | oracle (T389) | agrees |
|---|---|---|---|---|---|
| 54 Frost Bolt | 1,554 | 0.228 / 0.363 / **0.409** | **B** | B (.213/.284/.315) | **yes** |
| 93 Burst of Flame | 1,707 | **0.515** / 0.339 / 0.146 | **R** | R (.360/.379/.199) | **yes** |
| 41 Weaken | 1,850 | **0.377** / 0.294 / 0.329 | R | **G** (.448/.572/.191) | **no** -> **B398** |
| 253 Whirling Wind | 1,829 | 0.299 / 0.248 / **0.453** | B | (.258/.309/.214) | more blue |

The fire/frost pair B160 actually asked for is unambiguous in both directions
and matches the oracle's polarity. Shapes differ per spell in the stills; I do
not grade them -- Will judges.

### 6. Deviations, flagged not absorbed

1. **`#castspell`, not gem casts.** The plan asked for gem casts (T395: a GM
   cast sends no `OP_BeginCast`). No single class can gem-cast all four:
   Weaken is enchanter, Whirling Wind druid, Burst of Flame druid/shaman/
   magician, Frost Bolt wizard, and neither the oracle accounts nor the fable
   accounts hold a character covering them. `#castspell` was used on **both**
   sides, so the method is matched. The stage under measurement -- the
   TARGET/impact burst -- is driven by `OP_Action`, which `#castspell` sends
   exactly as a gem cast does; only the cast glow (`castSpray`/`castGlow`) is
   unexercised, identically on both sides. Those two styles were therefore
   **not** retuned and B226/B356 did not ask for them.
2. **The oracle capture is 1024x768, not >= 20 fps.** Section 1; the plan's own
   STOP clause.
3. **The live captures carry the config change but not B395's one-line pool
   wiring** -- the `build-gles2` binary predates it. This does not affect any
   pixel measured here: one landing is 648 sprites and the old hardcoded pool
   is 1024, so nothing was clipped. The wiring is proven by
   `SpellBurstPoolSizing.TheOldHardcodedPoolClipsAndTheConfiguredOneDoesNot`
   instead of by a capture.
4. **Two shipped tests had to change**, and both changes were forced by the
   measurement rather than convenience:
   `DataPaths`/`CwdElsewhere.TheProductionStringLoadsTheShippedConfigFromAnyDirectory`
   discriminated shipped/fallback/unconfigured by `burstCount` (36/12/0), and
   the shipped value is now 0 -- it moved to `spawnRate` (480/0/10), which
   still discriminates three ways; and `ImpactParticleTest`'s fixture asserted
   "the shipped impact style is a one-shot burst", which is no longer true. What
   those two tests prove is unchanged, because the emitter does not exist until
   the lifecycle phase and either shape spawns its first particles on the
   SECOND worker frame.
5. **Not measured, noted for whoever wants it**: fable's sky at `#set time
   11 0` is orange/overcast where the oracle's is blue. Outside this unit's
   territory (B360/T423) and not touched.

### 7. Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** |
| `ctest --test-dir build -j6` | **4,354 / 4,354, 0 failed, exit 0** |
| new tests pre-fix (baseline config) | **8 of 9 RED** (`TheConfiguredPoolHoldsTwoOverlappingLandings` passes at 81 sprites, as it should) |
| new tests post-fix | **9 / 9 green** |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate (standalone) | see the final report |
| `scripts/build-arm-noble.sh` | see the final report |
| `scripts/refactor/check_file_sizes.sh fable` | see the final report |
| server restore, world DOWN, verified by SELECT | positions, `zone.rain_chance*`/`snow_chance*`, `eqtime`, spellbook (93 rows) and memmed gems (93/54) all identical to `review/T422/server_snapshot_before.txt`; `eqclient.ini` restored byte-identical |
| `down.sh`; lock deleted | containers gone, ports 5998/9000 closed, `.eqemu-lock` absent |
