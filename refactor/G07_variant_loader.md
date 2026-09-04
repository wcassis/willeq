# G07 — `loadModelFromGlobalChrWithVariantsForAnimation`: the 815-line asset pipeline becomes phases

Batch: `refactor/G_oversize_methods.md` (corrections 1–17). **Runs ALONE
after G06.** Measured at `ad97818d`: `src/client/graphics/eq/variant_loading_animation.cpp`
(the body starts ~line 32, ~815 lines; not a component — a loader class
in `EQT::Graphics`). Re-measure mechanically: the phases (resolve the
global chr archive → load the base model → enumerate variants → attach
animations → register/cache), the cross-phase locals, every early
return/failure path.

⚠ **Will's standing decision: EQE1 is ALL-OR-NOTHING** — no S3D fallback,
assets must be complete. Every failure path's semantics must survive
unchanged; the order test and the multiset prove the code, and the
visual gate + the EQE1 crash-matrix tests (T201, T266 — grep `tests/` for
`eqe1`) prove the behaviour.

## Shape

As G06: a private `struct VariantLoadFrame` for cross-phase locals, one
`bool phaseX(VariantLoadFrame&)` per phase (failure paths return false
and the list honours it — STOP if a failure path did partial cleanup
that a bool-list cannot reproduce exactly), the top-level becomes the
ordered list; order golden + planted faults; text readers
(`tests/cmake/*.cmake` name `variant_loading*.cpp` for `gles2_zone_benchmark`
and the model viewer — the TU is not moved, so only confirm) and the
tests that drive the loader pass with assertions untouched.

Predicted: the body 815 → ≈ 40 lines; the loader's header gains ≈ 6
declarations + the struct.

STOP: a partial-cleanup failure path; any change to the all-or-nothing
outcome; a golden diff.

Territory: `src/client/graphics/eq/variant_loading_animation.cpp`, its
header (`include/client/graphics/eq/…` — find it), the new test + golden
+ cmake, `docs/module_map.md`. Visual gate + ARM NOT skippable.
