# T94 — Remember that a race has no model, instead of rediscovering it 130 times

**Slot: slot1. Headless characterization + fix. Live re-measure at the end —
eqemu lock normally if free; if slot3 still holds it, report the headless
numbers and flag the live leg as outstanding.**

**Territory: `src/client/graphics/eq/` (race model loading), `tests/`,
`refactor/`, `/mnt/projects/fable/review/T94/`.**

Read T91's Review in full first — especially its honesty caveat.

## The measurement this is built on

T91 measured Nektulos's `loadZoneStep8_Entities` at **246.065s**, of which
**230,749ms (93.8%)** went to **130 of 452 entities (28.8%)** whose race
matches no model anywhere. Befallen (171 entities, 5.85s) shows no such cost —
only 14 failures at 18%.

There is **no negative cache**: `notFound` in
`race_model_loader.cpp:984-1032` is a counter, not a memo. So "race R gender G
has no model" is rediscovered from scratch for every entity carrying that race.

Note what is *not* the cost: `loadNumberedGlobalModels()`
(`model_loading.cpp:52`) is guarded by `numberedGlobalsLoaded_`, so
`global2-7_chr.s3d` are read from disk once. The repeated expense is the
**per-race search across the loaded data**, plus the `otherChrCaches_` LRU
thrash T91 already fixed.

## Part 1 — Establish the post-T91 baseline FIRST

T91's LRU fix landed but **its magnitude was never confirmed** — the executor
could not reproduce the 230s headlessly, most likely warm page cache locally
versus T82's cold NFS capture. So:

**Measure Nektulos and Befallen with T91's fix in place, before adding
anything.** Without that number you cannot attribute any improvement to this
unit rather than to T91.

If the post-T91 cost is already small, **say so and stop** — the negative
cache may be unnecessary, and that is a perfectly good result. Do not
implement a fix for a cost that no longer exists.

## Part 2 — The negative cache

If Part 1 shows the per-race search is still expensive:

Cache the negative result — keyed on whatever actually determines the lookup
(race id and gender at minimum; check whether variant or texture index
participates). On a hit, skip the search entirely.

**Invalidation is a non-issue and should be stated as such**: the S3D archives
are static for the life of the process, so a race that has no model at startup
has no model at shutdown. A permanent negative cache is correct here. Say so
in a comment rather than leaving a future reader wondering.

**Thread safety**: `RaceModelLoader` already uses locking (see T91's
`storeOtherChrCacheLocked`). Follow the existing pattern; do not invent a
second locking discipline.

**Fix the logging while you are there.** A missing race currently produces
noise proportional to entity count. Log it **once per race**, with the count
of entities affected — that turns 130 lines of noise into one useful line, and
it is how the race-457 class of gap should have been visible all along.

## Part 3 — Measure again, honestly

Before/after on **both** zones. State the numbers. If the improvement is
smaller than expected, say so — T91's caveat exists because an unverified
magnitude claim is worse than none.

**Do not claim the zone-load problem is solved.** This removes one dominant
cost; whether what remains is acceptable is a separate question, and the
async/imposter work Will described (already partly built in `SimWorker`) is a
separate arc.

## What you may not do

- Do not implement distance gating, lazy building, or imposters. Will has
  ruled on the architecture separately and this unit is not it.
- Do not suppress the missing-race signal entirely — we still need to know
  which races are unmatched. Once per race, not once per entity.
- Do not change how models are found, only how a known-absent result is
  remembered. **STOP and report** if a fix seems to require changing the
  resolution order.

## Verification

Full build and suite (expect 2080/2081 plus any new tests; sole pre-existing
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual
gate — goldens should not move (an entity with no model still gets a
placeholder cube); **if they do, that is a signal**: say so and explain.

New tests must be **proven able to fail** — break the behaviour, show red,
restore, show green, and report that you did it.

Measurements to `/mnt/projects/fable/review/T94/` with a README.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T94-negative-cache`, base fable tip `0636f67a`. Live eqemu re-measure
performed (lock free at session start, taken/used/released cleanly — see
`review/T94/README.md` for the exact commands and character-position
restore). Full raw logs and headline table: `review/T94/README.md`.

### Part 1 — the post-T91 baseline, measured live (mandatory, done first)

T91's Review explicitly flagged that its LRU-accounting fix's real-world
magnitude was never confirmed against the original 246.065s Nektulos
capture — headless replay showed no signal, most likely a warm-page-cache
confound. This unit closes that gap with a real live measurement, **before**
adding anything: T94's 5 changed files were `git stash`ed, the tree
rebuilt, and both zones zoned into fresh via eqemu.

| Zone | Entities | Step 8 wall time | `preload...success=false` lines | Time in failed searches |
|---|---|---|---|---|
| Befallen | 171 | 5.816s | 14 | 1038ms (18%) |
| Nektulos | 451 | **19.425s** | 130 | 6164ms (32%) |

**Headline finding: T91's fix alone took Nektulos from 246.065s to
19.425s** — a ~92% reduction, and the exact magnitude T91's Review said it
could not confirm. The `otherChrCaches_` LRU-accounting bug T91 fixed was
in fact the overwhelming majority of the original cost. This is not a
small caveat — it changes what T94 is actually fixing: not "the 230-second
problem," which T91 had already mostly solved, but the smaller remainder
T91's own root-cause analysis predicted would still exist (repeated
per-entity search of already-known-failing races). 19.425s for Nektulos
and 5.816s for Befallen are both far from negligible at 452/171-entity
scale, and the 130-line-of-noise / 32%-of-Step-8 signature T91 described
was still fully present (confirmed by the 130 distinct `success=false`
log lines, matching T91's/T82's original count exactly) — so Part 2 was
justified, just against a smaller baseline than the plan's framing implied.
Stated plainly per the plan's honesty requirement: **the negative cache
was warranted, but it is fixing a ~20-second problem, not a ~230-second
one** — T91 already fixed the ~230-second problem.

### Part 2 — the negative cache

Added to `RaceModelLoader` (`race_model_loader.h`/`.cpp`):

- `knownMissingRaceGenders_`: `std::unordered_map<uint32_t, size_t>`,
  guarded by the existing `preparedDataMutex_` (the same lock
  `preloadModelData()` already held for its `preparedModelData_`
  staging-cache checks — no second locking discipline introduced, per the
  plan's instruction). Key present ⇒ that race/gender has no model
  anywhere; value is a hit counter.
- `isRaceModelKnownMissing(uint32_t cacheKey) const`: public accessor,
  thread-safe, mirrors the existing `isModelDataCached()`.
- `preloadModelData()` itself gained an early-out check (skips the entire
  5-step search chain on a hit) **and** records the negative result with a
  single `LOG_WARN` the first time a race/gender's search exhausts all 5
  steps and finds nothing.
- Both call sites — `loadZoneStep8a_BaseModelPrep`
  (`renderer_zone_load.cpp`, the zone-load path T91/T94 both measure) and
  `EntityPrepWorker::processRequest` (`entity_prep_worker.cpp`, the
  post-load streaming path for newly-visible entities) — were updated to
  check `isRaceModelKnownMissing()` alongside the existing
  `isModelDataCached()` check, so a known-missing race skips calling
  `preloadModelData()` (and its own per-entity `LOG_INFO`) entirely, not
  merely making the call cheap.

**Cache key: `(raceId, gender)` only — variant/texture index do NOT
participate**, confirmed two ways: (1) every real caller of
`preloadModelData()` (both the two call sites above) passes the default
`textureVariant=0`; (2) reading the function shows `textureVariant` is
only ever consulted *after* `modelData` is already found (a post-success
composite-sheet lookup) — it cannot affect whether the 5-step search
itself succeeds or fails. `preloadVariantModel()` (a separate function,
its own `uint64_t` variant-aware key) is untouched — it is only ever
called when the base race model already succeeded (`if (ctx.entitySuccess)`
/ `if (success)` guards at both its call sites), so it is never on the
missing-race hot path this unit targets.

**Invalidation**: none by design — the S3D archives are static for the
process lifetime, so a race with no model at startup has none at
shutdown. Stated as a comment at the insertion site
(`race_model_loader.cpp`, the `if (!modelData)` branch) rather than left
for a future reader to wonder about, per the plan.

**Resolution order unchanged**: no STOP condition was hit — the fix is
purely "remember a negative result," the 5-step search chain itself
(`zoneCharacters_` → `otherChrCaches_` → JSON-specified S3D →
`global_chr.s3d` → `global2-7_chr.s3d`) is untouched.

### The logging fix

Before: one `LOG_INFO` per **entity** of a missing race (T91's Nektulos
capture: 130 lines for 130 entities; this unit's own Befallen baseline:
14 lines for 14 entities). After: exactly one `LOG_WARN` +  one `LOG_INFO`
per **race/gender**, live-confirmed — Befallen's after-run shows exactly
2 preload lines (races 415, 457) for its 14 skipped entities; Nektulos's
after-run shows exactly 7 (races 330, 415, 432, 436, 440, 468, 471) for
its 130 skipped entities. The existing end-of-Step-8 summary
(`Sequential: SKIPPED N entities ... (M distinct race/gender pairs...)`,
already present pre-T94) still fires unconditionally and is untouched —
it remains the "how many entities total" signal; T94's fix is entirely
about not repeating the *per-entity* line.

### Part 3 — measured again, honestly (live, back-to-back with Part 1 for a fair warm-cache comparison)

T94 was restored, rebuilt, and run immediately after each Part 1 capture
(same eqemu session, same host OS page-cache state — S3D-archive cache
warmth is a client-side host filesystem effect, independent of the eqemu
docker container lifecycle, so tearing the server down/up between zones
does not confound this pairing).

| Zone | Entities | Step 8 wall time | `preload...success=false` lines | Time in failed searches |
|---|---|---|---|---|
| Befallen (T91 only) | 171 | 5.816s | 14 | 1038ms (18%) |
| Befallen (T91+T94) | 171 | 5.673s | **2** | 914ms (16%) |
| Nektulos (T91 only) | 451 | 19.425s | 130 | 6164ms (32%) |
| Nektulos (T91+T94) | 452 | **14.447s** | **7** | 3951ms (27%) |

Befallen: ~2.5% wall-time improvement (5.816s → 5.673s) — small, as T91's
own Review predicted ("Befallen shows no such thrash"): only 14 entities
across 2 distinct missing races, so there were only 12 redundant searches
to eliminate, each already cheap (~8-12ms, warm cache) even before T94.

Nektulos: **19.425s → 14.447s, a ~25.6% reduction** on top of T91's own
~92% reduction from the original 246.065s. The redundant-search time
itself dropped from 6164ms (130 calls) to 3951ms (7 calls) — the 7
remaining calls are each a *legitimate* first-ever search for a genuinely
absent race, not eliminable by a memo. **This is smaller than the plan's
framing implied** (which read as if T94 would recover most of the
original 230-second cost) precisely because T91 had already recovered
nearly all of it — stated per the plan's explicit "if the improvement is
smaller than expected, say so" instruction.

**The zone-load problem is not solved.** 14.447s (Nektulos) / 5.673s
(Befallen) remain for Step 8 alone, on top of the rest of zone load (BSP,
geometry, textures, etc. — not measured by this unit). Of what remains in
Step 8: ~302 model-cache hits (near-zero cost) + ~20 disk loads (S3D reads
for races actually present, unavoidable — this is real work, not thrash)
+ 7 now-single negative searches (~3.95s, the genuine cost of discovering
7 races have no model, paid once) + mesh-build/texture-decode/upload for
all 452 entities (not touched by this unit at all). The async/imposter
work is out of scope per the plan and Will's separate ruling
(`SimWorker` already has async infrastructure; billboards don't help
zone-in since they require building the model first).

### Visual gate

PASS, goldens untouched (`ctest`'s `visual_gate` test green in every
verification pass; ONLY the 5 production/test files listed below are in
`git diff`). Expected and stated explicitly per the plan: an entity with
no model still gets the same placeholder-cube fallback
(`EntityRenderer::getMeshForRace()`'s existing designed behavior,
untouched by this unit) — this fix only changes how fast/how-loudly a
*known-absent* result is rediscovered, never what gets rendered.

### New test — proven able to fail

`tests/test_inventory_model_view.cpp`,
`RaceModelAnimationHeadlessTest.NegativeModelCache_KnownMissingRacePersists_Headless`
(headless, `EDT_NULL` device, real local EQ client data, no eqemu/DISPLAY
needed). Uses race=330 (already an established known-missing race on this
asset set per the file's own `LoadsForRaceAndGender_Headless` test and
`deferred.md`'s "gfaydark zone-in FATAL, missing race=330" entry), asserts
`isRaceModelKnownMissing()` is false before any search, calls
`preloadModelData()` (expects `false`), asserts the memo is now `true`,
and repeats the call 3 more times asserting both the `false` result and
the memo staying `true` throughout.

**Proven able to fail**: temporarily replaced the memo-insert with
`if (false) { ... }` (production-code edit, not test-code — the test
itself never changed), rebuilt, ran the test alone:

```
Value of: loader_->isRaceModelKnownMissing(key)
  Actual: false
  Expected: true
[  FAILED  ] RaceModelAnimationHeadlessTest.NegativeModelCache_KnownMissingRacePersists_Headless
```

(4 assertion failures — the initial check plus all 3 repeat-call checks).
Reverted the temporary break, rebuilt, reran: green. Full suite re-run
afterward confirmed 2081/2082 (only the documented ALSA exception),
matching the pre-break baseline exactly.

### Build/test

1. `cmake --build build -j24` — clean, twice (once mid-unit after the
   fix, once final after the stash/pop round-trips used for Part 1/3's
   paired live measurements).
2. `ctest --output-on-failure` — **2081/2082** (2080/2081 plan baseline +
   1 new test), sole failure the documented ALSA
   `AudioManagerTest.PreloadCommonSounds` — run twice, identical result
   both times.
3. `bash scripts/build-gles2-x86.sh` — clean, multiple times (baseline
   and after builds for both zones, plus a final verification build).
4. Visual gate — PASS (see above).
5. `bash scripts/build-arm-noble.sh` — clean cross-compile.
6. `bash scripts/refactor/check_file_sizes.sh fable` — this unit's own 5
   changed files: 5/5 ok. (The script reports additional files/WARNs
   because `fable` advanced past this branch's base commit during the
   session — those are pre-existing WARNs from already-merged units
   T92/T93/T90, not from this unit; confirmed by diffing against this
   unit's actual base commit `0636f67a` instead, which shows exactly the
   4 production files + 1 test file this unit touched.)

### Deviations

1. Part 1's baseline was measured via a **live eqemu session** (the plan
   offered this as the preferred path when the lock is free), not a
   headless replay — T91's Review explicitly noted headless replay had
   already failed to show a signal for its own fix, so a live
   before/after was necessary to get an honest number here too.
2. A `git stash`/rebuild/measure/`stash pop`/rebuild/measure cycle was
   used per zone (documented in `review/T94/README.md`) to get true
   before/after pairs on the *same* warm host page-cache state — running
   Part 1 and Part 3 as fully separate sessions would have reintroduced
   exactly the cache-warmth confound T91's Review flagged. This is more
   process than the plan's literal wording implies but stays within its
   "measure again, honestly" instruction.
3. Testchar's live position drifted slightly during the Befallen runs
   (`x=-77.875,y=21.875` vs the canonical `-77.9232,21.9086` — normal
   gravity/tick settling, no `/moveto` or movement issued) and was
   restored via the standard `UPDATE character_data` command before
   server shutdown; Testmelee's position was unaffected.
