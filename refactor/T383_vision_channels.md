# T383 — vision colour channels matched to the oracle (B171)

Class A, slot1, no server (the oracle numbers are in `review/T377/` §2 and
`review/T361/`). Read the B171 board text and T361's §Review (how the
gains were solved; the per-channel deltas it measured) and
`src/client/graphics/vision_ambient.cpp`.

Measured T377, same spot/crop, video-stepped, ratio buffed/unbuffed per
channel: oracle Ultravision R×1.458 / G×1.463 / B×1.851 (near-neutral with
a blue lift); fable R? / G×1.281 / B? (green lags). Oracle Infravision
raises all three (B×1.208) at R×1.600; fable drives B DOWN to ×0.821.
Whole-frame ratios: oracle 1.560 / 1.252, fable 1.449 / 1.237.

1. Read `review/T377/logs/` for the full per-channel table (all six
   numbers per client per vision) and put it in the Review.
2. The floor in `vision_ambient.cpp` is per-channel already; the
   chromaticity constants were T361's deltas from a different capture.
   Re-solve the three per-channel gains for each vision type against
   T377's oracle channel ratios (the same power-law fit T361 used, per
   channel), so that fable's per-channel ratios match the oracle's within
   the still noise floor (1.3%). Infravision must not lower blue — check
   whether the red shift in `sim_animation.cpp`'s point-light arm
   (redShift 0.3, T360) is what pulls blue down; if so, the light arm's
   Infravision tint is the second parameter and is solved too (the oracle
   raises B ×1.208 on its lamps as well).
3. Test, pre-fix failing: the vision→ambient mapping yields per-channel
   ratios within 2% of the oracle table for both visions (a unit test on
   the mapping with the oracle constants as the expectation).
4. The live re-measure is OWED to the next lock unit (T377's driver).
   Strike B171 provisionally, noting the owed re-measure.

## Proof

Build, `ctest -j6`, the test with pre-fix/post-fix runs, file sizes;
graphics change → visual gate standalone + ARM (the gate's zones are
daytime/no vision, no golden should move; if one moves, STOP with the
contact sheet).

STOP: no per-channel gain set reproduces both the whole-frame ratio and
the channel ratios (report the nearest reach; do not commit a compromise
picked by argument).

## Review

Executor session 2026-09-05, slot1, branch `task/T383-vision-channels`,
base/tip `524194bc`. Desk work only; no server. Full evidence, every table:
**`/mnt/projects/fable/review/T383/README.md`** (the fit is runnable at
`review/T383/fit_per_channel.py`).

### 1. The oracle table (from `review/T377/logs/`, section 2 of its README)

Linear light, 700x420 crop, qeynos2 `-74, 428, 3.75, h=160`, forced 22:00.

| | R | G | B | grey |
|---|---|---|---|---|
| **oracle Ultravision** | x1.4581 | x1.4633 | x1.8507 | x1.5612 |
| **oracle Infravision** | x1.3284 | x1.2111 | x1.2079 | x1.2555 |
| fable Ultravision (pre-fix) | x1.4609 | x1.2814 | x1.6629 | x1.4483 |
| fable Infravision (pre-fix) | x1.6002 | x1.1264 | **x0.8209** | x1.2450 |

Oracle channel means behind them: night base 0.038491 / 0.035384 / 0.025689,
Ultravision 0.056123 / 0.051777 / 0.047543, night base (2) 0.038510 /
0.035409 / 0.025831, Infravision 0.051156 / 0.042885 / 0.031200. fable's:
night 0.004443 / 0.003763 / 0.002664, Ultravision 0.006491 / 0.004822 /
0.004430, night (2) 0.004435 / 0.003757 / 0.002663, Infravision 0.007097 /
0.004232 / 0.002186.

### 2. The fit — six floor channels, no free choice

T361 measured fable's linear-light frame value against the ambient as a power
law, its exponent given independently by two probe points as 1.1402 and
1.1409. Inverted **per channel** against fable's night ambient
(0.08 / 0.08 / 0.15):

    floor_c = night_c * oracle_ratio_c ^ (1 / 1.1405)

| | required ambient ratio | solved floor | shipped as |
|---|---|---|---|
| Ultravision | 1.3919 / 1.3963 / 1.7156 | 0.11135 / 0.11170 / 0.25733 | gain **0.1601** x tint **(0.695, 0.698, 1.607)** |
| Infravision | 1.2827 / 1.1829 / 1.1801 | 0.10262 / 0.09463 / 0.17701 | gain **0.1248** x tint **(0.823, 0.759, 1.419)** |

Forward-checked with the rounded constants that ship:

| | predicted R / G / B | residual vs the oracle |
|---|---|---|
| Ultravision | 1.4569 / 1.4640 / 1.8503 | **-0.08% / +0.05% / -0.02%** |
| Infravision | 1.3298 / 1.2125 / 1.2085 | **+0.10% / +0.11% / +0.05%** |

All six inside T377's 1.3% still noise floor and well inside the plan's 2%.

**No STOP.** Both halves are satisfied by the same gain set: weighting the
per-channel targets by fable's own measured night channels gives whole-frame
grey **1.5561** (oracle 1.5612, **-0.33%**) and **1.2582** (oracle 1.2555,
**+0.22%**).

### 3. The second parameter — the zone-light arm's Infravision tint

`visionLightModifiers()` applies `r*(1+s)`, `g*(1-0.5s)`, `b*(1-s)`. At
s = 0.3 a lamp's red-to-blue spread is **1.857**; the oracle's Infravision
spread is **1.0998**. Least squares on the log of the oracle's three
Infravision channel ratios (scale-free, so intensity is not double-counted)
gives **s = 0.0508**, residuals **-0.35% / +1.38% / -0.99%** against
**+28.7% / -7.7% / -23.8%** at 0.3.

### 4. Was the point-light Infravision tint the blue-down cause? — NO

It cannot be. Against Normal's flat intensity 0.25, Infravision's blue
multiplier `0.75*(1-s)` is **above** 0.25 for every s < 0.667: x2.10 at the
old 0.3, x2.85 at the new 0.0508. It pushes blue UP.

All three `VisionType` consumers were traced.
`applyVisionAmbientFloor` is a per-channel `std::max` and can only raise;
both zone-light arms raise every channel against Normal; and the object
lights (`updateObjectLightColors`, `computeFireFlicker`) never see
`visionType` at all — they are vision-**invariant**, a separate observation
that may be its own defect. **Nothing in the tree lowers a channel under a
vision buff**, so fable's measured **B x0.8209** is not reproducible from the
source as read.

Nor is the rest of the fable row. Fitting `frame_ratio_c = (ambient_c
ratio)^p` to T377's fable numbers needs p = 0.582 / 0.518 / 3.448 for
Ultravision and 0.772 / 15.9 / undefined for Infravision; a three-parameter
additive model per channel (ambient + point-light + vision-invariant sky)
puts blue's point-light coefficient at **-0.00177**, negative and
non-physical. **DEVIATION, flagged not absorbed:** this unit's gains are
therefore a model extrapolation from T361's exponent, not a closed
measurement loop, and the **live re-measure owed to the next lock unit is
what settles it**. The direction of every change is nevertheless unambiguous
against the oracle.

### 5. Why the floors came out blue-heavy

A ratio is measured against each client's own base. fable's night ambient is
blue-dominant (0.08/0.08/0.15) while the oracle's night frame is red-dominant
(0.0385/0.0354/0.0257), so the same per-channel ratios need proportionally
more blue in the floor. Ratios are the only composition-invariant quantity
across the two clients (T367's rule). T361's tints were the oracle's
*added-light chromaticity* on a near-wall crop — a different quantity, and
the reason the shape drifted.

### 6. Tests

`tests/test_vision_ambient.cpp`, **6 of 18 FAIL pre-fix, 18 of 18 pass
after** (`review/T383/logs/test_vision_ambient_{prefix,postfix}.log`):
`VisionAmbientTest.InfravisionLiftsLessThanUltravisionAndIsRedBiased`,
`VisionOracleChannelsTest.{UltravisionChannelRatiosMatchTheOracle,
InfravisionChannelRatiosMatchTheOracle, WholeFrameGreyRatiosMatchTheOracle,
UltravisionRedAndGreenAreNeutralToEachOther}`,
`VisionLightTintTest.InfravisionLampSpreadMatchesTheOracle`.
`NoVisionLowersAnyChannel` and `InfravisionRaisesEveryLampChannelAgainstNormal`
pass both before and after — which is item 4's finding stated as a test.

`tests/test_vision_zone_lights.cpp` — T360's Infravision case restated the
literal `0.975 / 0.6375 / 0.525` and went red on the new shift. It now reads
the shift from `visionLightModifiers()` and pins the FORMULA plus "every
channel lands above Normal's 0.25" (the part T360's swap got wrong); the
value is pinned with its oracle residuals by `VisionLightTintTest`. 4/4.

### 7. Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** |
| `ctest --test-dir build -j6` | **4005/4008** — see the three reds below |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate (standalone) | **PASS**, goldens untouched |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `scripts/refactor/check_file_sizes.sh fable` | 6 checked, **6 ok, 0 warn, 0 fail** |

* `AudioManagerTest.PreloadCommonSounds` — the known ALSA host quirk.
* `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` — load
  artefact inside `-j6`; passes standalone in 5.07 s.
* `DirectSenderCensus.LoginAndWorldStreamsAreTheOnlyExemptionAndStillTwelve`
  — **pre-existing red on the base tip `524194bc`, unrelated to T383.** T379
  (`f08369db`) closed B146 by routing the 12 login/world sends through
  `PacketSender`; T378's census (`6e4f9d9e`) still asserts 12 and now
  measures 0, exactly as its own failure text predicts. Boarded **B180**,
  not fixed here (out of file territory).

The first ARM build hit the 600 s ceiling (exit 143). Post-143 sweep: no
zero-byte files, no `build*/lib/st??????` ar temps, `libwilleq_core.a`
present with 317 members, `bin/willeq` valid ELF magic. Re-run clean, exit 0.

### Owed

**The live re-measure of fable's per-channel ratios at T377's spot** — B171 is
struck provisionally on the fit, and the next lock unit must confirm the six
numbers and account for the pre-fix B x0.8209 that no code path explains.
