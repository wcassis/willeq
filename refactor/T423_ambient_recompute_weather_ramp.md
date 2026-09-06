# T423 — the scene ambient recomputes on every input (B362); one weather ramp (B361)

Class A + B, slot2, no server. Read B362 (fable recomputes the scene
ambient only on an `OP_TimeOfDay` packet — T418's first live session ran
a whole block with the pre-fix 0.460 modifier live), B361 (three copies
of the weather ramp; the one under test is one production never runs),
T396/T418's §Reviews, `updateTimeOfDay()` / `composeSceneAmbient()`
(T396), the weather controller, and the vision floor (T405).

## Leg A — B362

Every input to `composeSceneAmbient()` (clock, weather modifier, vision
floor, zone override) must trigger a recompute when IT changes, not only
the clock packet: a dirty flag set by each writer, consumed once per
frame (G05 frame-order golden learns the statement in place). Tests
pre-fix failing: a weather-modifier change with no clock packet changes
the ambient next frame; a vision change likewise (T405's floors already
call `refreshVisionLighting()` — read whether that path is the same or a
second one; make it one).

## Leg B — B361

Three weather ramp implementations; `computeState()` and
`update()`/`updateStormAtmosphere()` are unreached. Delete the two
unreached ones (compiler proof; K12 ff), keep the one production runs,
and move T396/T418's tests onto it (they must fail against a planted
ramp change in the live copy). Strike B361.

## Proof

Build (background), `ctest -j6`, pre-fix/post-fix, visual gate standalone
+ ARM (no image golden should move), file sizes.

STOP: none expected.

---

## Review (executor, slot2, 2026-09-06)

Branch `task/T423-ambient-recompute-weather-ramp`, base `fable` @ `429b98e8`
(verified at checkout). Two commits, no merge, no push. No server used (T422
holds the eqemu lock; nothing here opens a connection).

| leg | commit |
|---|---|
| A — B362 | `8b82b125` |
| B — B361 + board + this Review | see the final report |

### 1. Leg A — the recompute trigger

**What was actually wrong.** `IrrlichtRenderer::updateTimeOfDay()` is the only
production site that calls `composeSceneAmbient()` and
`smgr_->setAmbientLight()`. Its callers were the `OP_TimeOfDay` bridge arm,
zone-in, and `PlayerCameraComponent::refreshVisionLighting()`. eqemu sends that
packet at zone-in and on `#set time` and at no other moment, so between two
clock packets the weather modifier and the zone-light gate were frozen at
whatever they were when the last packet landed — B362, and the reason T418's
first live session measured nothing.

**Design.** One `bool sceneAmbientDirty_` on `IrrlichtRenderer` with
`markSceneAmbientDirty()` / `isSceneAmbientDirty()`, set by every writer of a
NON-clock input and consumed once per frame. `updateTimeOfDay()` itself clears
the flag — it *is* the recompute — so the clock path is unchanged and a
marked-then-clocked frame recomposes once, not twice. The flag starts **true**,
so the first frame composes whether or not a clock packet has arrived. It is
cleared after the `isLoading()` guard but before the `!smgr_` guard: a mark
taken during a zone load must survive the load, but a renderer with no scene
manager has nothing to recompose.

**The consumer** is a new render pass, `FrameComponent::
recomposeSceneAmbientIfDirty()`, and it is **first** in `processFrameRender()`,
before `beginSceneAndDrawWorld()` — the ambient it sets is read by the same
frame's `drawAll()` and by the zone shader's uniforms, so a recompute after the
draw would land a frame late. Cost when nothing changed: one bool test.

**Which inputs set it** (the census that answers the plan's four):

| input | writer that marks | note |
|---|---|---|
| clock | *none* — `updateTimeOfDay()` is called directly by the bridge and clears the flag | unchanged path |
| weather modifier | `ZoneLifecycleComponent::setWeather()` (OP_Weather); `SimBridgeComponent`'s weather `applyState()` hop **when the modifier actually moves**; the zone-load `weatherEffects->onZoneEnter()` reset | the modifier is not a pure function of the packet: a lightning flash lifts it for `flashDuration` and drops it with no packet either way, and that arrives through the worker hop. Marking on every apply would dirty the ambient every frame weather is enabled, so the mark is taken on a >0.0005 move of `getAmbientLightModifier()`. |
| vision floor | `PlayerCameraComponent::refreshVisionLighting()`, which the three `ctx_.currentVision` writers already call | **this is the "make it one path" the plan asked for**: T405's floors used to call `updateTimeOfDay()` here, which was a SECOND private path into the recompute that only the vision input had. It marks now. |
| zone override | `DiagnosticsComponent::toggleZoneLights()` (all three states) and `::toggleDirectionalLight()` (both directions) | both used to recompose inline. `updateTimeOfDay()` READS `lightingEnabled_`, `zoneLightsEnabled_` and `debugDirectionalLightEnabled_`, so all three are ambient inputs. |
| user ambient multiplier | **NO WRITER EXISTS** | `ambientMultiplier_` has exactly one read (`composeSceneAmbient()`'s second argument) and zero writes across `src/ include/ tests/ tools/ benchmarks/`. Its comment names "Page Up/Down"; no binding produces it. Boarded as **B380**, left in place because `composeSceneAmbient()`'s four-term order is measured and pinned. |

**Deviation from the plan, flagged:** the plan says "a dirty flag set by each
writer". For weather that is only half-implementable, because the modifier
moves without any writer being called (lightning). The `SimBridgeComponent`
site therefore compares the modifier across `applyState()` rather than marking
blind. This is still one flag set by writers; it is not a general
compare-the-inputs consumer.

### 2. Leg A — tests, pre-fix and post-fix

`tests/test_scene_ambient_recompute.cpp`, 11 cases: five **behavioural** on a
default-constructed, never-`Init()`'d `IrrlichtRenderer` (the
`test_diagnostics_component.cpp` headless idiom — no GL context) and six text
pins on the sites a headless renderer cannot reach, because their bodies are
guarded on a `weatherEffects_` only `initDeferredGraphics()` builds.

Pre-fix run: the seven Leg-A `src/` files reverted with an exported patch
(`git apply -R`; never the stash), header and tests kept so the binary still
compiles.

```
PRE-FIX   [==========] 11 tests ran.  [  PASSED  ] 1 test.  10 FAILED TESTS
POST-FIX  [==========] 11 tests ran.  [  PASSED  ] 11 tests.
```

| case | pre | post |
|---|---|---|
| `AFreshRendererStartsDirty` | PASS | PASS |
| `TheRecomposeConsumesTheMark` | FAIL | PASS |
| `AWeatherPacketMarksTheAmbientWithNoClockPacket` | FAIL | PASS |
| `AVisionChangeMarksTheAmbientWithNoClockPacket` | FAIL | PASS |
| `TheZoneLightToggleMarksTheAmbient` | FAIL | PASS |
| `TheDirectionalLightToggleMarksTheAmbient` | FAIL | PASS |
| `TheVisionPathMarksInsteadOfRecomposingInline` | FAIL | PASS |
| `TheWeatherWriterMarksTheAmbient` | FAIL | PASS |
| `TheWorkerWeatherApplyMarksOnAModifierMove` | FAIL | PASS |
| `ZoneEnterMarksTheAmbient` | FAIL | PASS |
| `TheFrameConsumesTheMarkOncePerFrame` | FAIL | PASS |

`AFreshRendererStartsDirty` is the one that cannot be red in that revert: the
flag's initial value lives in the header, which the revert deliberately keeps
so everything else compiles. Recorded rather than hidden.

### 3. The G05 golden, learned in place with both faults re-planted

Entry **3** inserted — `if (owner_.isSceneAmbientDirty())
owner_.updateTimeOfDay(ctx_.currentHour, ctx_.currentMinute);` — directly after
entry 2 (`return false;  // Signal loading screen was shown`) and before the
first line of `runSceneProfileIfScheduled()`. Count 309 → 310, pass count
16 → 17. Entries 0-2 byte-identical, 3-308 became 4-309 with their text
byte-identical, nothing reordered. `kGoldenSize` and the `EveryPassIsCalled...`
count were edited with the reason written above them, per batch-G correction 32
(never regenerated from the tip).

Re-proved as a net, both faults, against the taught golden:

| planted fault | result |
|---|---|
| the pass deleted (the Leg-A revert) | `EveryPassIsCalledExactlyOnceAndNoneIsOrphaned` + `ComposedDrawOrderMatchesThePreG05Golden` FAIL |
| two passes swapped (`collectScenePassTimings` ↔ `renderFootprints`) | `ComposedDrawOrder...` FAIL, diverging at the swapped statement |
| the new statement's text changed (`currentMinute` → `0`) | `ComposedDrawOrder...` FAIL, "diverges at statement 3" |
| all faults removed | 4/4 PASS |

### 4. Leg B — one weather ramp

Census over `src/ include/ tests/ tools/ benchmarks/ docker/`:

| copy | callers |
|---|---|
| `SimulationWorker::computeWeatherEffectsState()` | `simulation_worker.cpp:556` — **production** |
| `WeatherEffectsController::computeState()` | T396's and T418's tests only |
| `WeatherEffectsController::update()` → `updateStormAtmosphere()` | **none at all** |

**Deleted:** `computeState()`, `update()`, `updateStormAtmosphere()` and
`updateLightning()` — the last because `update()` was its only caller. Nothing
they did was lost: `updateRain()`/`updateSnow()`/the storm cloud layer run
every frame from `updateRenderOnly()` (`renderer_frame_input.cpp:859`), the
darkening ramps in the worker, and the lightning timers ride the same worker
state back through `applyState()`, which calls
`triggerLightning()`/`scheduleLightning()`.

**Correction to B361's own text:** it says `updateRain()`/`updateSnow()` are
"still unreached". They are not — `updateRenderOnly()` calls both every frame.
Only `update()`'s ramp and storm-atmosphere body were dead. Struck accordingly.

`SimulationWorker::computeWeatherEffectsState()` is now **public and static**:
it touches no worker member, and the tests must be able to drive the copy the
client runs. `tests/weather_worker_step.h` makes the production hop
field-for-field — the `input.weather*` snapshot `renderer_sim_bridge.cpp`
builds, the worker call, and the `weatherEffectsState` apply arm — and both
weather test files' `settle()`/`advance()` helpers now call it.

**The blindness was measured, not argued.** The same fault — `input.
weatherDarkeningRate * 4.0f` in `sim_weather.cpp`, i.e. the production copy —
planted twice:

| tree | result |
|---|---|
| tests still on `computeState()` (the pre-T423 arrangement) | `test_weather_darkening_oracle` 10/10 PASS, `test_weather_ambient_modifier` 9/9 PASS — **19 green against a 4x change in the ramp the client runs** |
| tests moved onto the worker copy | 4 RED: `TheTransitionTakesTheOracleMeasuredSeconds`, `ClearingTakesTheSameMeasuredTime`, `TheDarkeningRampsOverTheOracleTransitionTime`, `TheProductionHopMatchesTheBareRamp` |
| fault removed | 19/19 PASS |

Two cases were rewritten rather than moved, because what they asserted no
longer exists: `EveryCopyOfTheRampCallsTheOneDefinition` →
`WeatherDarkeningOracleT423Test.TheRampHasExactlyOneImplementation` (the worker
calls the shared definition AND the controller carries no ramp and none of the
four deleted symbols came back), and `TheWorkerRampMatchesTheControllerRamp` →
`TheProductionHopMatchesTheBareRamp` (the bare `advanceWeatherDarkening()` and
the production hop step for step, 400 steps, 1e-6).

### 5. Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` (RelWithDebInfo, GLES2 config, background + exit file) | **exit 0** |
| `ctest --test-dir build -j6 --output-on-failure` | **100% passed, 0 failed out of 4356** (287.7 s). `visual_gate` passed inside `-j6` this time. |
| `bash scripts/build-gles2-x86.sh` | **exit 0** |
| `bash scripts/ci/visual_gate.sh` standalone | **=== VISUAL GATE: PASS ===**; CPU p95 qeynos2 5.740/10.108, gfaydark 6.389/10.672, befallen 8.441/10.948 |
| image goldens | **untouched** — `git status tests/goldens/` shows only the statement-text `frame_render_order_g05.txt`; no `.png` moved, so no contact sheet and no STOP |
| `bash scripts/build-arm-noble.sh` | **exit 0**, `build-arm-noble/bin/willeq` produced |
| `bash scripts/refactor/check_file_sizes.sh fable` | 16 files checked, **16 ok, 0 warn, 0 fail** (largest touched: `irrlicht_renderer.h` 1399) |

No exit-143 build, so no truncation sweep was needed. Exit files were unit-private
(`scratchpad/T423/`) and `rm -f`'d before each launch; every `wait_for.sh` MET
reported a non-zero elapsed time.

### 6. Surprises

1. **`ambientMultiplier_` has no writer** — one of the four inputs B362 names
   cannot move at all. B380.
2. **B361 undercounted what was dead and overcounted it in the other
   direction**: `updateLightning()` was dead too (it went with `update()`),
   while `updateRain()`/`updateSnow()` were never dead.
3. **The lightning flash is a weather-modifier change with no packet**, so
   "mark on the writer" alone is not sufficient for weather; the mark has to be
   taken on the modifier moving across the worker apply.
4. **19 green tests against a 4x production ramp change** is the sharpest
   statement of B361 available, and it is now on the board as a measurement.
5. The suite is at **4356**, well above the 3,759 the journal quotes — read it,
   never quote it.
