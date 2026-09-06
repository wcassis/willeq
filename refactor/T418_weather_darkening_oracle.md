# T418 — active-weather darkening measured on the official client and mimicked (B231, Will 2026-09-06)

Measurement + match; eqemu lock (atomic; must be ABSENT). Read B231,
T396's §Review (the weather controller, `maxDarkening`, the modifier
log lines), T405's rig (`review/T405/scripts/` — heading pinned via
`#goto x y z h`, rain_chance zeroed, `#set time` re-pinned per window,
the composition control), and T367's spots.

1. Both clients at T405's spot, 11:00 and 22:00, clear (proof lines).
   Then `#weather 1 <intensity>` for intensities 2, 5, 9 (rain) and, if
   the zone supports it, snow (`#weather 2 …`); video each transition;
   per intensity the settled whole-frame grey and per-channel ratio vs
   clear, on BOTH clients. Also the fade: how many seconds the oracle
   takes to settle (frame-stepped).
2. Table; the oracle's curve (intensity → ratio) replaces fable's
   `1 - 0.6*i/10` with a fit whose every point is a measured one (a
   lookup table over the measured intensities with linear
   interpolation is fine — say so); the fade time matches the oracle's.
3. Tests pin the table and the fade (pre-fix failing against the 0.6
   curve). Live confirmation: fable re-captured at one intensity after
   the change, within the noise floor.
4. Strike B231. Restore rain_chance, world clock, positions (world DOWN);
   `down.sh`; delete the lock.

STOP: lock present; the official client shows no darkening at any
intensity (then the modifier is 1.0 always — say so, that is the
answer).

---

## Review

Executed 2026-09-06 in slot2, branch `task/T418-weather-darkening-oracle`, base
**`113b6388`**. Media, tables and the full rig: **`/mnt/projects/fable/review/T418/`**
(`README.md` first; `contact/T418_rain_intensity9_contact_sheet.png` is the
sheet for Will).

### 0. The answer

**The STOP fired, in the form the plan anticipated: the official Titanium client
shows no darkening at any intensity.** It shows the opposite — the world gets
BRIGHTER. So the modifier is 1.000 always, and that is the answer.

The measurement went further than the STOP required in three ways, and each is
committed or boarded rather than left as a remark:

1. **Intensity does nothing and snow is rain.** Rain 2 / 5 / 9 and snow 5 settle
   to the same plateau, day and night.
2. **What the oracle does instead is FOG, not ambient** — its ratio grows with
   depth. A scalar on the scene ambient cannot produce it. Board **B360**.
3. **The fade IS mimicable and was mimicked**: 9.24 s mean, intensity
   independent, against fable's 1.08 s at intensity 9.

### 1. Both clients at the spot, clear, with the proof lines (plan step 1)

qeynos2, `#goto -74 428 3.751 160` on both clients, `zone.rain_chance1..4` and
`snow_chance1..4` zeroed for the session, world clock re-pinned with
`#set time <h> 0` immediately before every clear reference, and every weather
state paired with its own clear reference taken 30 s earlier under that pin.
Video-stepped at 5 fps on the oracle side; frame-dumped on fable's.

**Deviation, flagged: `#set weather 0 0` — the command T405 and T408 used — is a
silent no-op.** eqemu's `SetWeather` handles only `argnum == 2` and `argnum == 4`
after the `#weather` → `#set weather` alias rewrite, and that line is `argnum 3`.
The working forms are `#weather 0`, `#weather 3 1 <i>` (rain) and
`#weather 3 2 <i>` (snow), and the 3-form only fires while `zone_weather` is
`None`. Those units were nonetheless weather-free because the `rain_chance`
zeroing is what actually disables the re-roll timer. Recorded in the rig script's
header and in `review/T418/README.md` §1a.

**Deviation, flagged: the Wine client crashed three times**, so the eight states
were collected across five oracle sessions (o1..o5) rather than one. That forced
— and supplied — cross-session controls: the day clear reference reproduces to
**0.03%** across two sessions and the day rain-5 ratio to **0.09%**. One block
(o1's first, taken 30 s after a night→day clock step) is **discarded and
reported**, not hidden: its clear reference was still climbing. The rig now
bounds `import` with `timeout 25` and checks the EverQuest window between states.

### 2. The oracle table (plan steps 1 and 2)

Geometry band `560x110+200+350` (below the horizon, clear of both clients' UI),
linear light, each state against its own clear reference. **Ratio > 1 means
brighter.**

| hour | state | R | G | B | grey | fade s |
|---|---|---|---|---|---|---|
| 11:00 | rain 2 | 1.4752 | 1.1730 | 1.1059 | **1.2496** | 9.0 |
| 11:00 | rain 5 | 1.4809 | 1.1772 | 1.1136 | **1.2552** | 9.0 |
| 11:00 | rain 5 (2nd session) | 1.4806 | 1.1756 | 1.1124 | **1.2541** | 9.6 |
| 11:00 | rain 9 | 1.4804 | 1.1768 | 1.1137 | **1.2549** | 9.2 |
| 11:00 | snow 5 | 1.4752 | 1.1736 | 1.1069 | **1.2501** | 9.4 |
| 22:00 | rain 2 | 1.2238 | 1.1157 | 1.0894 | **1.1327** | — |
| 22:00 | rain 5 | 1.1175 | 1.0583 | 1.0489 | **1.0693** | 7.8 |
| 22:00 | rain 9 | 1.2272 | 1.1123 | 1.0881 | **1.1316** | 10.2 |
| 22:00 | snow 5 | 1.0033 | 1.0061 | 1.0067 | **1.0056** | — |

Day spread across intensities: **0.4%**, against a 0.03% control. Night is a
fifth of the signal and scatters accordingly; its two bracketed fades are
noise-driven and are not used. **No row of either table is below 1.0.**

Depth decomposition on the same frames (ten wall strips, near → far):
R **1.42 → 2.73**, G **1.06 → 1.65**, B **1.01 → 1.25**; the sky goes
(.163,.290,.429) → (.285,.286,.348). Fog and an overcast sky, not a light scale.

### 3. The committed curve and its residuals (plan step 2)

`include/client/graphics/weather_ambient_oracle.h` / `.cpp`, new TU:

    kOracleRainAmbient[] = {0,1.0} {2,1.0} {5,1.0} {9,1.0} {10,1.0}
    kOracleSnowAmbient[] = {0,1.0} {5,1.0} {10,1.0}
    kOracleWeatherTransitionSeconds = 9.3f

A lookup table over the measured intensities with linear interpolation between
knots — **said so, per the plan**. Knots 2/5/9 (rain) and 5 (snow) are measured;
0 and 10 bound the table so a lookup never extrapolates.

Fade residuals against the five clean day transitions (9.0, 9.0, 9.2, 9.4, 9.6):
**+0.3, +0.3, +0.1, −0.1, −0.3 s — ≤3.3%.**
Curve residuals against what fable now renders live: **−0.31%** at rain 9 and
**−0.32%** at rain 2, against a 0.7% session control.

The fade required changing the ramp, and the ramp turned out to have **three
copies, of which the one under test was one production never executes** — board
**B361**. All three now call one `advanceWeatherDarkening()` at a rate captured
when the target changes, so a move of any size takes the measured 9.3 s.

**The weather FOG is deliberately unchanged.** `currentDarkening_` still targets
`maxDarkening * i/10` and still drives the blend; the blend now reads it
directly, because `1 - getAmbientLightModifier()` is identically zero under the
measured curve and would have deleted the weather fog silently.

### 4. Tests (plan step 3)

| test | pre-fix | post-fix |
|---|---|---|
| `WeatherDarkeningOracleT418Test` (10 cases, new) | **8 FAIL** | **10 PASS** |
| `WeatherAmbientModifierTest` (T396, 9 cases) | **2 FAIL** | **9 PASS** |

The two T418 cases green pre-fix pin the new constants and the two snapshot
sites, which the revert does not touch. The pre-fix run used
`review/T418/scripts/t418_prefix_revert.py`, which edits exactly the five
production sites and leaves the tests standing — **not** `git stash`, **not**
`git checkout --`.

**Live confirmation, done as a before/after pair rather than an after-only
capture**, because the first live session exposed **B362**: fable recomputes the
scene ambient only inside `updateTimeOfDay()`, which runs only on an OP_TimeOfDay
packet, so a full day block of forced rain with the **pre-fix 0.460 modifier
live** measured **0.998–1.014** and logged no `weatherMod` line at all. Re-pinning
the clock 25 s after the trigger forces the recompute:

| day state | fable PRE-FIX | fable POST-FIX | oracle |
|---|---|---|---|
| rain 9 | **0.4203** | **0.9969** | 1.2549 |
| rain 2 | **0.9380** | **0.9968** | 1.2496 |

Both fable sessions read the same pack (`5881918` bytes) in their own `mmap'd`
line, so the pair is not confounded by the mid-unit pack change.

### 5. Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** |
| `ctest -j6` | **4,296 / 4,298** — two reds, **NOT this branch** (below) |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate, standalone | **FAIL on three `eqe1MappedBytes` lines only — NO IMAGE GOLDEN MOVED**, so the plan's STOP did not fire |
| `scripts/build-arm-noble.sh` | **exit 0**, ELF 32-bit ARM produced |
| `check_file_sizes.sh fable` | 15 checked, **15 ok, 0 fail** |

Both reds are the shared pack `/mnt/projects/EverQuest_ETC1/willeq.eqe1`,
rewritten under this unit by another slot. `PackReproducible`: the shipped sha
is `14a6bd45…`, this branch pins `aca03e7f…` and **fable's own tip pins
`584ba1fe…`** — the shipped pack matches neither, so it moved again after T417
merged. The gate's `eqe1MappedBytes` deficit is **identical (1,398,240) in all
three zones** = T417's blessed +349,664 plus **exactly 1,048,576** more from that
newer, unmerged pack. Every CPU p95 is under its ceiling.

### 6. Board (plan step 4)

**B231 struck.** Three new entries: **B360** (the overcast sky/fog swap the
oracle actually performs, with the depth strips as its target), **B361** (three
ramp copies, the tested one unreached), **B362** (the ambient recomputed only on
a clock packet).

### 7. Server (plan step 4)

Restored with the eqemu-server container **stopped** and only mariadb up, then
verified by `SELECT` and by a full `diff` of the before/after dumps — not a
spot-check of the rows touched.

| value | snapshot | restored |
|---|---|---|
| `zone` qeynos2 `rain_chance1..4` | 10, 10, 10, 10 | `diff` IDENTICAL |
| `zone` qeynos2 `snow_chance1..4` | 0, 0, 0, 0 | `diff` IDENTICAL (never changed) |
| `character_data`, all 18 rows | — | `diff` IDENTICAL |
| `eqtime` clock | minute 26, hour 24, day 6, month 4, year 4004 | IDENTICAL on all five clock columns |
| `character_corpses` | 21 rows | `diff` IDENTICAL |

Both characters were moved only by `#goto`, to the pose they already held.
`down.sh` run; `/mnt/projects/fable/.eqemu-lock` deleted.
