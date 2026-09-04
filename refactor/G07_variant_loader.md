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

## Review

**STOPPED — the contracted shape does not fit the method. No code changed.**

Measured on `task/G07-variant-loader` at `b93096c6` (clean tree). Body is
lines 32–846 of `src/client/graphics/eq/variant_loading_animation.cpp` =
**815 lines**, exactly as the plan predicted. Declared at
`include/client/graphics/eq/race_model_loader.h:385`. Sole production
caller: `src/client/graphics/eq/variant_loading.cpp:273`.

### Why it stopped

The dispatch's STOP is: *"a failure path did partial cleanup **or continued
past the failure in a way a bool list cannot reproduce**."* Six failure
paths do the second thing, and two success paths do a third thing the
shape has no expression for.

The plan modelled the body as "sequential phases (resolve → load →
variants → animation attach → register/cache)" collapsible to
`if (!phaseN(frame)) return false;`. It is not sequential. Brace-matched
control-flow enumeration of all 23 exits (script:
`scratchpad/nest2.py`; loop headers resolved by brace matching, not
indentation — G01 correction 4):

| line | construct | enclosing loop | semantics | ordered bool list? |
|---|---|---|---|---|
| 69 | `return false` | none (fn scope) | `codesToTry` empty → abandon | **yes** — the only one that maps |
| 155 | `continue` | char loop @153 | skip null/partless character, keep scanning the same source | no — iteration |
| 280 | `continue` | char loop @153 | variant meshes not found → **next character**, then next source, then the global path, then the next race code | no |
| 287 | `continue` | char loop @153 | geometry combine returned null → **next character** | no |
| 505 | `return true` | depth 5, inside 3 loops | success **escapes two enclosing loops mid-iteration** | no — the shape has no early success |
| 515 | `continue` | outer code loop @73 | `loadGlobalModels()` failed → **next race code**, not abandon | no |
| 525 | `continue` | global char loop @523 | skip null/partless character | no |
| 640 | `continue` | global char loop @523 | variant parts missing → next character | no |
| 646 | `continue` | global char loop @523 | geometry combine null → next character | no |
| 840 | `return true` | depth 4, inside 2 loops | success escapes the loops | no |
| 845 | `return false` | fn scope | all candidates exhausted | fall-through only |
| 209, 224, 258, 272, 396, 470, 572, 587, 618, 632, 752, 812 | `break` | inner search loops | loop breaks (fallback-part search, animation-source search) — **not** arm-terminating breaks | n/a |

Three structural facts, any one of which is disqualifying:

1. **The body is one loop, not a pipeline.** Lines 73–843 are the body of
   `for (const std::string& codeToTry : codesToTry)`. An ordered bool list
   runs each phase once; here every candidate phase runs once *per race
   code*, and the candidate count is data-dependent (1 or 2 — the row code,
   then the row's `fallback_code`).
2. **Success returns from inside two/three nested loops** (lines 505, 840).
   `if (!phaseN(f)) return false; … return true;` cannot express "succeed
   and unwind three loops now".
3. **The six `continue`s are try-the-next-candidate, not fail.** This is
   the load-bearing point for Will's rule. `false` from this method means,
   at `variant_loading.cpp:273-277`, `variantCache[key] = nullptr; return
   nullptr;` — a permanent negative cache entry. Rewriting any `continue`
   as `return false` would abort the whole load at the **first** failed
   candidate, so a race whose primary code misses but whose `fallback_code`
   hits would go from *loading* to *permanently null*. That is contract
   STOP #2, "any change to the all-or-nothing outcome", reached by the
   mechanical route.

### Secondary finding: there are no banners

G03 correction 10 and G04 correction 18 both require the step list to come
from the file's own banners. This TU has **zero** banner comments
(`grep -c -E '^\s*//\s*[=*-]{3,}'` → 0). Every comment is an inline
explanatory line. There is no author-supplied seam to cut at, so the
"verbatim phase slices at the body's own banners" instruction has no
referent here. That is a warning sign in its own right, not just a
missing convenience.

### What the method actually is: a 90%-duplicated pair

The reason it is 815 lines is duplication, not phase count. Normalised
(comments and blanks stripped) similarity, `scratchpad/dup.py`:

| block A | block B | similarity | identical lines |
|---|---|---|---|
| zone-path character block (151–508, 282 code lines) | global-path character block (519–843, 270 code lines) | **0.906** | 250 |
| anim-merge A, zone path / zone-S3D source (346–401) | anim-merge C, global path / zone-S3D source (700–757) | **0.939** | 46 |
| anim-merge B, zone path / global source (415–472) | anim-merge D, global path / global source (761–814) | **0.957** | 44 |
| anim-merge A (346–401) | anim-merge B (415–472) | **0.915** | 43 |

The animation-merge body (add-missing-animations + map-bones-by-code-
substitution + merge-missing-tracks) appears **four times**, ~48 code lines
each, ~190 lines total, mutually 91–96% identical. The whole
match-select-fallback-combine-register sequence appears **twice**.

### The decomposition that does fit (NOT done — needs Will/orchestrator)

Not the contracted one, and it cannot carry the contracted proof:

- `bool buildCodesToTry(uint16_t, uint8_t, std::vector<std::string>&, std::string&)` — the prologue, lines 36–70. This part *is* sequential and *does* have a real `return false`.
- `int mergeAnimationsFrom(Skeleton& ourSkel, const Skeleton& sourceSkel, const std::string& code, const std::string& sourceCode)` — collapses the four copies to one, ~190 lines → ~50.
- `bool selectVariantParts(const CharacterModel&, …, std::vector<CharacterPart>& skinned, std::vector<CharacterPart>& raw)` — the fourfold mesh-name/fallback search.
- `bool tryBuildVariantFrom(const CharacterModel&, …)` — the per-character body shared by both paths; returning `bool` here is honest, because at *this* level false genuinely means "next character".
- the top level stays a loop: `for (code : codesToTry) { … if (tryBuildVariantFrom(...)) return true; … } return false;`

Predicted ≈ 815 → ≈ 250 across five functions. **But**: de-duplicating four
near-copies is exactly what a verbatim-slice multiset forbids, so Batch G's
call-graph-multiset proof does not apply. This unit would need a different
proof obligation (behavioural: the EQE1 crash matrix + visual gate + a new
unit test driving the loader across a race with a `fallback_code`), and
that is a scope change only Will or the orchestrator can authorise.

### Verification steps

Not run, and deliberately so. No source file was modified (`git status
--porcelain` empty at STOP), so build / ctest / GLES2 / visual gate / ARM /
`check_file_sizes.sh` would each be measuring the unmodified `fable` tip —
they would prove the tree green, not the change safe. Per the protocol,
"report narrowed evidence, don't thrash." Baseline confirmed clean instead:
branch `task/G07-variant-loader`, tip `b93096c6`, empty working tree.

### Text readers (corrections 7 and 13 — recorded though negative)

- `loadModelFromGlobalChrWithVariantsForAnimation` across `tests/`, `scripts/`, `tools/`, `docs/`: **zero hits**. No test names the method.
- `variant_loading` across the same: only build files — `tests/cmake/graphics.cmake:34-36` and `:125-127` (the two graphics test targets), `CMakeLists.txt` ×5 target lists, and one prose mention in `docs/future/entity_texture_atlas.md:227` (names the sibling `variant_loading.cpp`, not this TU). The TU is not moved, so all of these are confirm-only, as the plan said.
- EQE1 tests present (12 files): `test_eqe1_loader`, `test_eqe1_archive_component`, `test_eqe1_archive_config`, `test_eqe1_composite_open`, `test_eqe1_texture_index`, `test_eqe1_alpha_companion_mip`, `test_constrained_renderer`, `test_constrained_texture_cache`, `test_entity_viewer_regression`, `test_zone_load_component`, `test_static_ui_scroll`, `test_ui_atlas_pixels`. **None drives this loader** — so the EQE1 crash matrix would not have caught a `continue`→`return false` regression. The behavioural net the alternative decomposition needs does not currently exist.

### Deviations

- Order golden, planted faults, and the multiset were **not** produced: all three presuppose the cut, and the cut is what stopped.
- `docs/module_map.md` **not** updated — nothing moved.
- Committed under an honest message rather than the dispatched
  "G07: the variant/animation loader becomes ordered bool phases; EQE1
  all-or-nothing preserved", which asserts work that was not done.
