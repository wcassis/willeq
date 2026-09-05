# T394 — T383's vision gains reverted to the last live-matched values (B203)

Class A, slot3, no server. Read the B203 board text and T386 §4: T383's
forward model predicted R/G ×1.46 and the live measure gave ×0.97; both
scalar ratios regressed (Ultravision 1.449 → 1.169, Infravision 1.237 →
1.076 against the oracle's 1.561 / 1.256). A model refuted by measurement
is reverted, not patched.

1. `git revert` T383's source changes to `vision_ambient.{h,cpp}` and the
   Infravision `redShift` in `sim_animation.cpp` (commit `776357f1`), so
   the constants are T361's live-matched values again; keep T383's test
   FILE but rewrite its expectations to pin the RESTORED constants and to
   state, in the test names, that the per-channel fit is refuted (T386
   §4) and the live-measured pre-T383 ratios (R 1.461 / G 1.281 / B 1.663;
   Infra R 1.600 / G 1.126 / B 0.821) are the baseline the next live
   re-solve starts from. T360's `test_vision_zone_lights.cpp` Infravision
   case returns to its literal.
2. B203 also records fable's night BASE moving +55% in blue between tips
   on the same crop, untouched by T383's stated changes: bisect that with
   the sim/ambient code between `c0a76fea` and `65cbed39` — which commit
   changed the night base? (T367's culling rule? T370? a config?) Name
   it; if it is a defect, board it (B210+), do not fix here.
3. Strike nothing: annotate B203 with the revert hash and that the
   per-channel re-solve is OWED as a LIVE A/B search (one parameter at a
   time, oracle frame beside fable frame, T361's method) in the next lock
   unit.

## Proof

Build, `ctest -j6`, the tests, file sizes; graphics change → visual gate
standalone + ARM (no golden moves — daytime zones).

STOP: none expected.

## Review

Executor session 2026-09-05, slot3, branch `task/T394-vision-gains-revert`,
base `e3c80278`. Desk work only; no server (the eqemu lock is held by T391 and
was not taken).

### 1. The revert — four files, byte-identical to the pre-T383 tree

`git diff 776357f1^ -- include/client/graphics/vision_ambient.h
include/client/graphics/vision_type.h src/client/graphics/vision_ambient.cpp
tests/test_vision_zone_lights.cpp` is **EMPTY**. Restored:

| constant | T383 shipped | restored (T361) |
|---|---|---|
| `kUltravisionAmbientGain` | 0.1601 | **0.1521** |
| `kInfravisionAmbientGain` | 0.1248 | **0.0956** |
| `kUltravisionTint` | (0.695, 0.698, 1.607) | **(1.009, 0.848, 1.143)** |
| `kInfravisionTint` | (0.823, 0.759, 1.419) | **(1.539, 0.843, 0.617)** |
| Infravision lamp `redShift` | 0.0508 | **0.3** |

`visionFrameRatios()` and `kVisionFrameExponent = 1.1405f` went with them:
both existed only to express the model T386 refuted, and neither had a
production caller (`applyVisionAmbientFloor` is the one production entry
point, unchanged). Nothing was patched and **no new constant was introduced**.

**Deviation, flagged:** the plan located the Infravision `redShift` in
`src/client/graphics/sim_animation.cpp`. It is not there — the constant lives
in `include/client/graphics/vision_type.h` (`visionLightModifiers()`);
`sim_animation.cpp` is one of its two consumers and T383 never touched it. The
constant was reverted where it actually lives and `sim_animation.cpp` is
untouched.

### 2. The tests — 6 fail against T383's constants, all pass against the restored

`tests/test_vision_ambient.cpp` keeps T383's FILE (it is not deleted) but its
T383 section is replaced by `VisionGainsRevertedT394Test`, five cases whose
names carry the refutation, above a header block holding the whole T386 §4
table and the pre-T383 baseline. `tests/test_vision_zone_lights.cpp` returns
to T360's literal.

Proof both ways, same tree, only the five constants moved (applied by `sed`,
rebuilt, run, then restored from `776357f1^` — no `git stash`):

| build | test_vision_ambient | test_vision_zone_lights |
|---|---|---|
| **T383's constants** | **11 passed, 5 FAILED** | **3 passed, 1 FAILED** |
| **restored constants** | **16 passed, 0 failed** | **4 passed, 0 failed** |

The six that fail against T383's constants:

* `VisionAmbientTest.InfravisionLiftsLessThanUltravisionAndIsRedBiased`
  (`infra.r > infra.b` — 0.1027 vs 0.1771 under T383's blue-heavy tint)
* `VisionGainsRevertedT394Test.T383PerChannelFitRefutedByT386SoTheGainsAreT361LiveMatchedValues`
* `VisionGainsRevertedT394Test.T383PerChannelFitRefutedByT386SoTheTintsAreOracleAddedLightChromaticities`
* `VisionGainsRevertedT394Test.T383PerChannelFitRefutedByT386SoTheInfravisionFloorIsRedDominantAgain`
* `VisionGainsRevertedT394Test.T383LampRedShiftRefutedByT386SoTheInfravisionShiftIsT360sLiteral`
* `VisionZoneLightsTest.InfravisionIsThreeQuarterIntensityRedShifted`

The fifth new case, `PreT383LiveRatiosAreTheBaselineForTheOwedLiveResolve`,
passes either way by construction: it carries the live-measured numbers the
next re-solve starts from (Ultravision R 1.4609 / G 1.2814 / B 1.6629, grey
1.4483; Infravision R 1.6002 / G 1.1264 / B 0.8209, grey 1.2450) and asserts
that on both greys the restored constants sit closer to the oracle than
T383's did.

### 3. The +55% blue night base — bisected to NO COMMIT, and the plan's range was wrong

**The plan's range does not span the two measurements.** `c0a76fea` IS
T386's own base/tip (`review/T386/README.md` line 4) and `65cbed39` is later
still, so `c0a76fea..65cbed39` is entirely AFTER the second measurement. The
interval that actually separates the two night-base readings is
**`032de79a`** (T377's base/tip, `review/T377/README.md` line 4) **..
`c0a76fea`** — 48 commits.

Diffed in full. Under `src/client/graphics/` and `include/client/graphics/`
the interval contains exactly three things: T383's vision floors
(`vision_ambient.{h,cpp}`, `vision_type.h`), T381's spell FX
(`spell_visual_fx*`) and T382's UI (`frame_component.h`, `static_panels.*`,
`config/hotkeys.json`). **Nothing touches `updateTimeOfDay`, the day/night
ambient curve, the zone lights, `sim_animation.cpp`, any shader, fog or
culling.** Outside graphics the interval is PacketSender routing (T376/T378/
T379), spell-db columns + `willeq.db` v7 (T384) and `tools/` splits (T385) —
none of which reaches a pixel.

T383's floors cannot explain it either, and not by argument: **Testchar is
race 1**, and T377's own session log resolves that on line 808 as `Player race
1 -> base vision: Normal`, so an unbuffed frame takes no racial floor at all
and T383's constants are not in its path.

**Answer: no commit changed the night base.** T367's culling rule was already
in T377's binary (its README says so), T370 is outside the interval, and no
config in the interval touches lighting.

**What the bisect found instead — boarded as B211.** `updateTimeOfDay`
multiplies `r`, `g`, `b` and `sunIntensity` by
`weatherEffects_->getAmbientLightModifier()`, and T377's vision session logged

    10:35:38.603 [DEBUG] [GRAPHICS] updateTimeOfDay: weatherMod=0.460,
                 applying to ambient r=0.080 g=0.080 b=0.150

26 seconds before its first measurement window opened, with no `#set weather`
anywhere in the recipe. Two properties make this poison for these
measurements:

1. the log line fires only when the modifier **changes by >0.01 AND is below
   0.99**, so a return to 1.0 is never logged — no session can be shown from
   its log to have been weather-free;
2. the vision floor is applied **AFTER** the weather multiply (deliberately,
   per the comment on the floor block), so weather darkens the base a ratio is
   measured against while leaving the floor fixed — it **inflates a measured
   vision ratio in proportion to the weather at the time**.

That is enough to unpin every cross-session night and vision comparison in
T358 / T361 / T377 / T383 / T386, T383's fit included. It is NOT a full
explanation of the +55%: the shift is not uniform across channels
(R x1.244, G x1.208, B x1.546) and a weather multiply is, so at least the sky
dome and the zone lights are also in play. Narrowed, not root-caused, and not
fixed here — T394 is a revert and holds no server lock. **B211** (class A);
B210 left free for T391.

### 4. What is still OWED — B203 stays open

The per-channel fit is owed as a **LIVE A/B search** in the next lock unit:
one parameter at a time, oracle frame beside fable frame, T361's method,
starting from the pre-T383 baseline the new test carries. A second desk re-fit
is exactly what T383 did. B203 records the revert hash and the owed method;
B171's provisional strike is reversed and points at B203.

### 5. Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT=0** (twice: with T383's constants for the pre-fix proof, and with the restored ones) |
| `ctest -j6 --output-on-failure` | **4038 / 4039**, exit 8; sole failure `AudioManagerTest.PreloadCommonSounds` (the waived ALSA quirk) |
| `scripts/build-gles2-x86.sh` | **EXIT=0** |
| visual gate, run **standalone** (`scripts/ci/visual_gate.sh`) | **EXIT=0, `=== VISUAL GATE: PASS ===`**; qeynos2 / gfaydark / befallen all under their CPU-p95 ceilings (4.342/10.108, 8.459/10.672, 9.286/10.948) |
| goldens | **untouched** — `git status --porcelain` lists only the 5 source/test files, `refactor/BOARD.md` and this plan; no golden path appears. Expected: the gate's zones are daytime and the vision floor is a night-only effect (day ambient 0.5 is above every floor) |
| `scripts/build-arm-noble.sh` | **EXIT=0** |
| `scripts/refactor/check_file_sizes.sh fable` | **5 checked, 5 ok, 0 warn, 0 fail** (largest: `tests/test_vision_ambient.cpp` 282) |

### 6. Surprises

* **The plan's bisect range was after both measurements, not between them.**
  Taking it literally would have returned "nothing changed" for the wrong
  reason.
* **The bisect's real answer is that there is no commit at all** — and the
  mechanism it turned up (B211) is worse than a bad commit would have been,
  because it retroactively unpins the measurements T383 was fitted to AND the
  measurement that refuted it.
* B203's night-base numbers (R 0.004256 / G 0.003610 / B 0.002649) are not the
  ones T377's README publishes for its night windows (0.004443 / 0.003763 /
  0.002664); they come from a different stretch of the same
  `logs/fable_frame_rgb_vis.txt`. The discrepancy is small and does not change
  the +55% conclusion, but the two are not the same window.
