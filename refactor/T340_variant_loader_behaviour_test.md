# T340 — A behaviour test for the variant loader, then the de-duplication (B052)

Two legs, same slot (slot1), sequential. G07 measured the shape: one
`for (codeToTry : codesToTry)` candidate loop, `continue` = next
candidate, success `return`s from three loops deep; the animation-merge
body appears four times (0.92–0.96 similar) and the zone/global character
blocks are 0.91 similar. **No test drives the method.** Will's rule: EQE1
is ALL-OR-NOTHING — a race whose primary code misses but whose fallback
code hits must LOAD; all candidates exhausted must return null and cache
null.

## Leg A — the test (no production change)

`tests/test_variant_loader_candidates.cpp`, driving
`loadModelFromGlobalChrWithVariantsForAnimation` through a controllable
archive source. Read `src/client/graphics/eq/variant_loading.cpp` (the
caller, the `variantCache` negative entry at ~273) and
`variant_loading_animation.cpp` (the loop) to find the seam the loader
already has for where models come from (the EQE1 archive component /
`getOrOpenEQE1`, or the S3D path in tests — `tests/test_race_model*.cpp`,
`test_eqe1_*.cpp` and the model-viewer harness show what fixtures exist;
`data/race_models.json` carries `fallback_code`). Cases, each named for
the exit it exercises:
1. primary code hits on the first character → loads (the depth-5
   `return true`).
2. primary code misses every character, fallback code hits → loads
   (the outer `continue` at ~515 then success) — the case a bool-list
   would have broken.
3. a character with no usable model → `continue` to the next character,
   then success.
4. all candidates exhausted → returns false, caller caches null, second
   call does not re-scan (the ~845 `return false` and the negative cache).
5. `codesToTry` empty → the line-69 `return false`.
Fixtures: the smallest archives that make each path real (an existing
EQE1 test tree if one is mounted — `test_eqe1_archive_component` skips
when not; otherwise a stub archive source; say which). Assert on the
returned model AND on which candidate/character was chosen (add the
smallest read-only observation seam if none exists — e.g. the loader's
existing log line captured, or a `lastResolvedCandidate()` getter).
Prove each case is real by planting the fault G07 predicted: change one
`continue` to `return false` in the code → case 2 must fail; restore.

## Leg B — de-duplicate, behind the test (production change, move-only where possible)

Only after Leg A is merged and green: `mergeAnimationsFrom()` replaces
the four copies (diff the four first; where they differ, the difference
becomes a parameter — record each); the zone/global character blocks
become one `tryBuildVariantFrom(source, …)` parameterised by the
archive; `buildCodesToTry()` and `selectVariantParts()` as G07 laid out.
The loop keeps its shape. Proof: Leg A's five cases unchanged and green;
the G07-style exit census identical before/after (same exits, same
depths); the visual gate + ARM standalone; the model viewer builds.
Predicted 815 → ~250 across five functions.

STOP (Leg A): no seam lets a test control the candidate source without
a production change beyond a read-only getter — then report the seam
needed. STOP (Leg B): any of the four copies differs in a way that is
not a parameter (a behavioural difference) — report it, do not unify.
