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
