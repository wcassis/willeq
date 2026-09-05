# T396 — the weather ambient modifier (B211); the coin sprite (B218)

Class A + B, slot3, no server. Read the B211 board text and T394's
§Review (the bisect that found it), `src/client/graphics/environment/`
(the weather effects and `getAmbientLightModifier()`),
`IrrlichtRenderer::updateTimeOfDay` and `vision_ambient.cpp`; for Leg B
the B218 text, `tools/` UI atlas builder and `EQUI_Inventory.xml`'s
`A_PlatinumCoin` etc. decals.

## Leg A — B211

T377's vision session logged `weatherMod=0.460` with no weather in the
recipe; the line only fires below 0.99 so a return to 1.0 is never
logged; and the modifier multiplies the ambient BEFORE the vision floor.
1. Why 0.46 with no weather: read what sets the modifier (server weather
   packets `OP_Weather`, a zone default, a fade-in state that starts dark,
   a stale value across zone-ins). Reproduce headlessly: construct the
   weather effects object, feed it the sequence a zone-in produces (or
   none), and read the modifier over simulated time — pre-fix test
   showing 0.46 (or whatever it is) with no weather packet.
2. Match the oracle for the RULE, not the number: in the official client
   clear weather does not darken the world (T361/T367 measured the oracle
   night frame at the same spot as a stable reference — quote it). So
   with no weather the modifier must be 1.0; with rain/snow it darkens by
   the amount the official client does — if that amount is unmeasured,
   leave the weather-active value alone and board the measurement.
3. Log the modifier on every change (both directions) and once per
   zone-in, so a session log can prove weather-free.
4. Order: decide by reading whether the vision floor must clamp the
   FINAL ambient (after weather) — it is a floor on what the player sees,
   so it applies last; if the code already does that, say so; if not,
   fix and test.
5. Strike B211; annotate B203 (the re-solve can now pin weather = 1.0 and
   prove it from the log).

## Leg B — B218

The oracle's coin rows carry a coin decal per denomination
(`A_PlatinumCoin` … from the official UI atlas). Add the four decals to
fable's UI atlas via the existing atlas builder (read where the item
icons come from — the same PFS?), draw them in `coinDecalRect()` (T393),
and pin with the static-panel seam (pre-fix failing: the decal rect draws
the letter). Strike B218. If the source sprites are not in any archive
fable reads, say so and STOP that leg.

## Proof

Build (all trees if the atlas changes), `ctest -j6` (no expected
failures), the tests with pre-fix/post-fix runs, file sizes; graphics
change → visual gate standalone + ARM (weather at 1.0 in the gate's
daytime zones — if a golden MOVES, that is the confound made visible:
STOP with the contact sheet, never bless).

STOP: the 0.46 comes from a server packet the gate zones also receive
(then the goldens carry the weather and the ruling is Will's).

## Review

Branch `task/T396-weather-modifier-coin-sprite`, base `15f846ca`.
Commits `0c0d484d` (tests + reproduction) and `824de12e` (fix + board).
Leg A DONE. **Leg B STOPPED** on the plan's own condition.

### 1. Where the 0.460 came from — reproduced, with its source

**It is not a phantom, not a fade artifact and not a stale value. The server
sent rain.** `review/T377/logs/fable_session_vis.log` carries, before the
`weatherMod=0.460` at line 9193:

| line | log |
|---|---|
| 288 | `WeatherConfigLoader: Loaded storm settings (lightning=true, darkening=0.6)` |
| 294 | `Applied low preset to weather config: lightning=false` |
| 1074 | `[ZONE] Weather packet: type=0, intensity=9` |
| 7443-4 | `Weather update: type=0, intensity=9` -> `decoded newType=1 newIntensity=9` |
| 7494 | `SimWorker: Activated weather type=1 intensity=9` |
| 9193 | `updateTimeOfDay: weatherMod=0.460, applying to ambient r=0.080 g=0.080 b=0.150` |

`setWeather()` decodes packet `type=0, intensity>0` as RAIN, and

    targetDarkening = storm.maxDarkening * intensity/10 = 0.6 * 0.9 = 0.54
    modifier        = 1 - 0.54                          = 0.460

exactly, reached ~1.08 s after the packet at the 0.5/s ramp. The two overlay
multipliers contributed nothing: `getRainDaylightMultiplier()` /
`getSnowBrightnessMultiplier()` return **false** whenever the unified GLES2
weather particles are active, which line 7494 shows they were. The lightning
flash term never fired because the session's own `low` quality preset had
lightning off (line 294). qeynos2 was raining at intensity 9 for the whole
measurement window.

Reproduced headlessly in `tests/test_weather_ambient_modifier.cpp`
(`ServerRainIntensityNineReproducesT377sPointFourSixZero`): the controller
takes four null device pointers, `setConfig()` supplies the session's own
`maxDarkening = 0.6` / `lightningEnabled = false`, `setWeather(0, 9)` is the
literal packet, and `computeState()/applyState()` runs the same integrator
`SimulationWorker` does.

### 2. The oracle's rule — already held for clear, broken across a zone line

The rule (clear weather does not darken; T361/T367 use the oracle night frame
as a stable cross-session reference, which it could not be otherwise) was
already satisfied on the clear path: with no packet, `currentDarkening_` is 0
and both overlays return false, so the modifier is exactly 1.0. Pinned by
`NoWeatherPacketLeavesTheModifierAtExactlyOne` and, for the garbage
`type=255, intensity=49` packet qeynos2 sent first,
`TheGarbageZoneInPacketDecodesToNoDarkening`.

It was **not** satisfied across a zone change. `WeatherEffectsController::
onZoneEnter()` and `::onZoneLeave()` had **zero callers tree-wide** (grepped
`src/ include/ tests/ tools/ benchmarks/ docker/`), and `onZoneEnter()` reset
nothing anyway — it set `isIndoorZone_` and forwarded to the storm cloud
layer. Every other environment manager in SEQ Step11 gets its `onZoneEnter`
and every other one in `unloadZone()` gets its `onZoneLeave`; the weather
controller was the odd one out. So the previous zone's rain followed the
player through the zone line and held the ambient at 0.460 in a zone the
server had said nothing about.

Fix: both hooks share one `resetWeatherState()`, `onZoneEnter()` calls it, and
both are wired — SEQ Step11 (`renderer_zone_load_env.cpp`) and `unloadZone()`
(`renderer_zone_lifecycle.cpp`).

**The weather-ACTIVE value is unmeasured against the oracle and was not
touched.** Boarded as **B231** (class C: needs a lock unit and Will's method).

### 3. The logging change

The old line fired only when the modifier both changed by >0.01 **and** was
below 0.99, so a return to 1.0 was never logged and no session could be shown
weather-free from its own log — which is precisely how T377 read as clean. The
last-logged value was also a function-`static`, shared by every renderer in
the process.

Now: a member `weatherModLogged_`; fires on any change >0.005 in **either**
direction; prints old value, new value, and the weather type/intensity behind
it plus the pre-weather ambient; and `onZoneEnter()` logs one line per zone-in
at INFO. A session log can now be read for "was this window weather-free".

### 4. Floor vs weather — the ordering decision and its test

**The floor applies to the FINAL ambient, after weather, and the code already
did that.** It is a floor on what the player *sees*: a vision buff is a promise
about the darkest the world can get, so nothing — weather included — may
multiply it afterwards. What was missing is that the decision was stated only
in a comment.

The four-step composition (day/night curve -> user ambient multiplier, clamped
-> weather -> vision floor) moved into `composeSceneAmbient()`
(`vision_ambient.h/.cpp`); `updateTimeOfDay()` calls it. Six
`VisionAmbientCompositionT396Test` cases pin it, including B211's own
consequence stated as an assertion:
`UncontrolledWeatherInflatesAMeasuredVisionRatio` — at weatherMod 0.460 the
buffed frame is unchanged while the base halves, so the *apparent* Ultravision
ratio more than DOUBLES.

Proven a real net rather than a tautology: inverting the two steps in
`composeSceneAmbient()` turns **2 of the 6 red**
(`TheVisionFloorClampsTheFinalAmbientAfterWeather`,
`UncontrolledWeatherInflatesAMeasuredVisionRatio`); restoring the order
returns all 6 to green. (Done with a file copy, not `git stash`.)

### 5. Tests, pre and post

`tests/test_weather_ambient_modifier.cpp` (new, 9 cases) and
`tests/test_vision_ambient.cpp` (+6 cases).

| test | at `0c0d484d` | at `824de12e` |
|---|---|---|
| `NoWeatherPacketLeavesTheModifierAtExactlyOne` | PASS | PASS |
| `TheGarbageZoneInPacketDecodesToNoDarkening` | PASS | PASS |
| `ServerRainIntensityNineReproducesT377sPointFourSixZero` | PASS | PASS |
| `TheDarkeningRampsAtHalfAUnitPerSecond` | PASS | PASS |
| `ClearingTheWeatherReturnsTheModifierToOne` | PASS | PASS |
| `ZoneEnterClearsStaleWeatherDarkening` | **FAIL** (0.460) | PASS |
| `ZoneLeaveClearsStaleWeatherDarkening` | PASS | PASS |
| `TheZoneEnterResetIsReachedFromZoneLoad` | **FAIL** (no caller) | PASS |
| `TheWeatherModifierLogIsNotGatedToDarkeningOnly` | **FAIL** | PASS |
| `VisionAmbientCompositionT396Test.*` (6) | n/a (new API) | PASS; 2 red under inverted order |

### 6. Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | EXIT=0 |
| `ctest --test-dir build -j6` | **4086/4086 pass, 0 fail, EXIT=0** |
| `scripts/build-gles2-x86.sh` | EXIT=0 |
| visual gate (standalone) | **PASS**, goldens byte-identical |
| `scripts/build-arm-noble.sh` | EXIT=0 |
| `check_file_sizes.sh` | 18 checked, 18 ok, 0 warn, 0 fail |

**The STOP condition did not fire.** `gles2_zone_benchmark` loads zones from
files with no network, so it receives no `OP_Weather`, the modifier is 1.0 in
all three gate zones, and no golden moved. The goldens do not carry weather.

One deviation to note: a `cmake --build` hit the 600 s ceiling (exit 143) after
the reconfigure. The post-143 sweep found one `build/lib/st??????` ar temp file
and no damage (zero zero-byte files, `libwilleq_core.a` 322 members matching
`main`'s, every `build/bin` binary valid ELF); temp deleted, rebuild EXIT=0.

### 7. Leg B — STOPPED

The plan's escape hatch ("if the source sprites are not in any archive fable
reads, say so and STOP that leg") is met, twice over. The pixels exist and were
located exactly — all four animations live in ONE texture,
`/mnt/projects/EverQuestP1999/uifiles/default/window_pieces01.tga`, as a
contiguous 72x18 strip at (90,140): `A_PlatinumCoin` (90,140), `A_GoldCoin`
(108,140), `A_SilverCoin` (126,140), `A_CopperCoin` (144,140), 18x18 each. The
file is a 256x256 uncompressed 32-bit TGA (type 2, bottom-left origin,
262,162 B) and the four alpha masks decode as real ~16px coin discs.

But: (1) nothing fable runs reads them. `uifiles/` appears nowhere in
`src/ include/ tools/ scripts/` except one comment;
`tools/ui_atlas_builder.cpp` is fully procedural and opens **no file at all**;
the only TGA decoder in the tree is a standalone `loadTGA()` in
`tools/dump_icon_sheet.cpp`, linked by nothing under `src/`; and `eqe1_builder`
takes `.bmp`/`.dds`/`.json`/`.wld`, never `.tga`. (2) Even with a decoder, the
atlas only reaches the client by overwriting
`/mnt/projects/EverQuest_ETC1/willeq.eqe1` — a 2.8 MB **shared** asset outside
every worktree, read live by every slot's ctest and by the visual gate — and
the worktree cannot reproduce its inputs: the documented recipe names
`data/atlas/`, which exists in neither `main` nor any slot, and the pack holds
entries `data/textures`'s 64 BMPs do not explain. Repacking from a guessed
input set mid-wave would silently drop entries for three worktrees at once.

Doing the code half alone would be a regression, not a partial win: with no
atlas entry `UIAtlas::mapSprite` drops the name with a `LOG_WARN`,
`getSprite()` returns the empty rect, and `drawSprite` would replace a readable
`p`/`g`/`s`/`c` with a degenerate quad. Nothing was changed.

Boarded as **B230** (the reproducible-pack blocker, which gates every future
atlas change, not just the coins). **B218 stays open** with the sprite
coordinates recorded so the next unit starts from measurement.

### 8. Board

- **B211 STRUCK** (`0c0d484d` + `824de12e`), with the packet, the arithmetic
  and all three fixes recorded.
- **B203 annotated**: the confound is now closed *and worse than "uncontrolled"*
  — T377's window ran under rain the whole time, so the pre-T383 baseline
  numbers are SUSPECT until re-measured under a verified-clear sky. The owed
  live A/B must assert `weatherMod = 1.000` from the session log, which is now
  possible.
- **B218 annotated** with the STOP evidence; stays open.
- **B230** (B) new: the atlas builder reads no files and `willeq.eqe1` has no
  reproducible build.
- **B231** (C) new: the weather-ACTIVE darkening curve is unmeasured against
  the oracle.

### 9. Surprises

- **The 0.460 was legitimate.** B211 framed it as weather with "no `#set
  weather` in any measurement recipe"; the recipe indeed asked for none, but
  the *server* volunteered rain at intensity 9 on zone-in. The defect is that
  nobody could tell — which is the observability half of B211, and it is the
  half that actually cost T358/T361/T377/T383/T386 their comparability.
- **Two zero-caller lifecycle hooks** on a class every zone load touches. The
  reachability pattern again, and this time the missing call is what breaks the
  oracle rule the plan asked us to enforce.
- `fable` moved under the branch mid-unit (T395 merged), so
  `check_file_sizes.sh fable` reported 18 changed files rather than this
  branch's 10; all 18 pass, and the branch's own files are all well under the
  ceiling.
