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
