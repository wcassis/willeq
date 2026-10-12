# B606 (#189): delete the 18 renderer/asset/data functions with no production caller

Issue #189, board B606 (class B, unreached). Found by T458's zero-caller census.
Two commits on `task/kb-b606-renderer-asset-data-zero-callers`, on top of
origin/fable 7e17067e (merged in first):

- a27bd28a — the deletions (code, db, sha256, tests, cmake)
- 0f141f65 — the ratchet rows (`scripts/refactor/check_zero_callers.py`)

The split is deliberate and is what makes the RED faithful: at commit 1 the
ratchet still lists the B606 rows, so its own `--check` FAILS "stale" until
commit 2 removes them. A third commit (the docs/README one) fixes a
compile error the inherited working tree had in `test_string_database.cpp`
(the `sqlite3_prepare_v2` tail parameter is `const char**`; the new fixture
helpers declared `char* err`) — caught by the first full local build
(`fullbuild.log`); the green build is `fullbuild2.log`.

## The rows

All 18 rows deleted. Every row was tree-wide grepped BEFORE deletion
(`rg -n -w <name> src include tests tools benchmarks docker scripts`): the
only surviving occurrences of any deleted name are comments.

| # | owner::name | decision | notes |
|---|---|---|---|
| 1 | `IrrlichtRenderer::setNewUIEnabled` | deleted | the only writer of `FrameComponent::newUIEnabled_` was this setter; the field stays and is toggled live by the `/newui` slash command (`src/client/graphics/slash/renderer_slash_commands.cpp:439`) and read by 8 render/input sites. No player loses the toggle |
| 2 | `ZoneLifecycleComponent::applyStoredZoneEnvironment` | deleted | plus its sole caller `setZoneEnvironment` — see "applyStoredZoneEnvironment decision" below |
| 3 | `RDPServer::setCertificate` | deleted | plus `getCertPath`, `getKeyPath`, the `certPath_`/`keyPath_` members and the unreachable file branch in the ctor. No config key or CLI flag reaches it — see "RDP certificate decision" below |
| 4 | `BackgroundWorkQueue::pollAll` | deleted | `pollOne()` (the live API) stays |
| 5 | `BackgroundWorkQueue::setBatchHooks` | deleted | plus `onBatchBegin_`/`onBatchEnd_` members and their two call sites in the worker loop (write-only members, the last readers) |
| 6 | `StringDatabase::loadDbStrFile` | deleted | legacy text loader, superseded by the SQLite path. Plus the members it fed (`eq_strings_`/`db_strings_` — write-only after the deletion, see below) |
| 7 | `StringDatabase::loadEqStrFile` | deleted | same |
| 8 | `WilleqDatabase::getLoginString` | deleted | plus the `login_strings` TABLE — see "the login_strings trap" below. (The `loadLoginStrings` builder-side loader in `tools/willeq_db/db_loaders_text.cpp` went with the table.) |
| 9 | `(free)::reloadRaceMappings` | deleted | a one-line delegate over `loadRaceMappings()` (which stays; it has callers) |
| 10 | `(free)::LiftTravelSeconds` | deleted | only callers were its own tests — deleted with them |
| 11 | `(free)::TargetTypeRestrictsCreature` | deleted | same |
| 12 | `(free)::toString(SpellVisualSource)` | deleted | same (the three `SpellVisualSelectorToStringTest` cases went with it) |
| 13 | `(free)::parseFormattedMessageText` | deleted | only callers were its tests. The structured `parseFormattedMessage()` LOST its only caller to the same deletion, so it went too (with `ParsedFormattedMessage`); `parseFormattedMessageArgs()` — the one the display-text path calls — is untouched. `tests/test_formatted_message.cpp` gained a test-local `joinArgs()` helper (18 join blocks replaced); no production helper was added (a production helper would be a NEW zero-caller) |
| 14 | `(free)::optionsClipPlaneStep` | deleted | the Options page never converts a distance back to a step — it only stores the step (`optionsClipPlaneDistance`, step→distance, stays with its callers) |
| 15 | `(free)::optionsVolumeFraction` | deleted | zero-call wrapper over `eqt::audio::volumeFractionFromStep`; callers use the audio-side function directly |
| 16 | `(free)::optionsVolumeStep` | deleted | same, over `volumeStepFromFraction` |

Card rows NOT touched, per the card's own instruction:

| owner::name | decision | why |
|---|---|---|
| `EntityRenderer::loadEquipmentModels` | kept, tool caller | `tools/hwcaps/held_item_candidate_search.cpp:210` and `tools/hwcaps/impostor_pixel_compare.cpp:279` call both it and `loadGlobalCharacters`. A tool caller is a caller. `entity_renderer.*` is untouched by this card (another card owns it); these two are no longer census rows (they left the tests-only bucket when T485's tool landed), so there is no EXEMPT row to move |

## The cascades (what the RED names that is not a B606 row)

The RED at commit 1 names **21** stale EXEMPT_TESTS rows, not 16. The other 5:

**B605 `HotkeyBinding::toString`** — deleted with commit 1, row struck in
commit 2. Mechanism [measured, probe:
`/home/user/kb-evidence/b606-renderer-asset-data-zero-callers/probe_tostring_buckets.py`]:
the ratchet counts token-level call sites per NAME. At origin/fable the name
`toString` had exactly three use-sites, all in
`tests/test_spell_visual_selection.cpp:62-64`, all calling the FREE
`toString(SpellVisualSource)` (row 12 above). Because the name has a use-site
under tests/, BOTH declarations named `toString` — the free one and the
`HotkeyBinding` member — sit in the tests-only bucket; the B605 row is the
exemption that kept the member classified there. The member is a one-line
delegate, `return HotkeyManager::bindingToString(*this);` (live function,
`src/client/input/hotkey_manager.cpp:364`, which the tests call directly —
`tests/test_hotkey_manager.cpp:159-184`), and it has zero true callers of its
own. Deleting row 12 and its tests empties the name's use-set, the member
leaves the bucket, and its B605 row goes stale. The dead delegate is deleted
(rather than the row re-aimed) because keeping it would leave a function whose
entire existence is a name collision.

**Four observer rows whose subjects commit 1 deleted:**

| stale row | subject that went |
|---|---|
| `RDPServer::getCertPath` (observer) | `certPath_` — deleted with `setCertificate` |
| `RDPServer::getKeyPath` (observer) | `keyPath_` — same |
| `StringDatabase::isDbStrLoaded` (observer) | `db_strings_` — last reader was `getDbStrCount`, itself deleted (its only caller was the `LOG_INFO` in `src/client/core/eq_strings.cpp:487-488`, which went with it) |
| `StringDatabase::isEqStrLoaded` (observer) | `eq_strings_` — same, via `getEqStrCount` |

## applyStoredZoneEnvironment decision: DELETE (the live path is a strict superset)

`ZoneLifecycleComponent::applyStoredZoneEnvironment()`
(origin/fable `src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp:770`)
is a 9-line pass-through: if `storedZoneEnvironment_.pending`, call
`setZoneEnvironment()` (same file, :378) and clear the flag. Its header
already called it a superseded duplicate of the zone-load apply path,
"boarded for a measured wire-or-delete decision." Field-by-field, the live
path (`ZoneLoadComponent::loadZoneStep10_Sky`,
`src/client/graphics/zone_load/renderer_zone_load_env.cpp:237-332`) applies a
strict superset:

| field | `setZoneEnvironment` (dead) | Step 10 (live) |
|---|---|---|
| `skyType` | `skyRenderer->setSkyType(...)` :383 | `skyRenderer->prepareSkyType(...)` :244 |
| `zoneType==2` → `isIndoorZone` | :390 | :245-246 |
| sky enabled (dungeon × user setting × constrained) | :395 | :248 |
| `fogMaxClip[0]` → `zoneMaxClip_` (default 99999) | :401 | :255-256 |
| `setRenderDistance(userRenderDistance_)` re-apply | :403 | :257 |
| fog color `fogR/G/B[0]`, linear, from renderDistance | :408-423 | :259-265 |
| — | — | weather config / zone fallback :267-278 |
| — | — | sky texture upload, dome finalize, celestial bodies :283-331 |

Nothing `applyStoredZoneEnvironment` applies survives unapplied by Step 10.
It was also the SOLE caller of `setZoneEnvironment`, so the pair goes
together. `storedZoneEnvironment` (the pending struct the zone packet writes)
stays — Step 10 reads it. No behaviour a player reaches changes: Step 10 runs
on every zone load; the dead function's only call site was a path that no
production code took (zero production callers is exactly why the row was
boarded).

## RDP certificate decision: no documented option is silently ignored

Card requirement: grep `cert` in the RDP config parsing before deleting.
`src/client/application_config.cpp:480-490` reads exactly two RDP keys —
`rdp.enabled` and `rdp.port`. There is no `rdp.cert`/`rdp.key` key and no
CLI flag. `docs/` names no certificate option: the only hit outside
review/completed is
`docs/standards/rdp_implementation_guide.md:942` ("Custom certificate support
can be added if needed (B606 deleted the unreachable
`RDPServer::setCertificate`…)") — a forward-looking
sentence, now updated to say B606 deleted the unreachable setter. No defect:
nothing documents an option that silently does nothing.

## The login_strings trap (orchestrator's census warning)

Card trap: `WilleqDatabase::getLoginString` is the ONLY reader of the shipped
`login_strings` table, so deleting it alone turns
`WilleqDatabaseShippedContent.EveryTableTheDatabaseShipsIsOneTheClientQueries`
RED. Resolution: option (a) — delete the function AND drop the table, in the
SAME commit, no version bump.

- `tools/willeq_db/db_builder.h` + `tools/willeq_db/db_loaders_text.cpp`: the
  `login_strings` table creation and its `eqlsstr_us.txt` loader are removed
  (56 lines in the loader).
- `scripts/build_willeq_db.sh`: `eqlsstr_us.txt` out of the input set (nine
  inputs → eight).
- `data/willeq.db` rebuilt with the script, committed with its new
  `data/willeq.db.sha256` line. `metadata.version` stays 7 (a row edit is not
  a schema change; a bump would invalidate every build tree — T304/T355 rule).

Census arithmetic [measured, `check_db.py` in the evidence dir]:

| | tables | rows (excl. metadata + sqlite_stat1) | login_strings | input_digest |
|---|---|---|---|---|
| origin/fable db | 10 | 28,791 | 470 | 956f69a5686c98bb |
| this commit's db | 9 | 28,321 | — | 10ec9bedbe6c7c5e |

28,791 − 470 = 28,321. 28,791 − 28,321 = 470. The digest chain documented in
`data/willeq.db.sha256`: c3f8ff61… (T450, ten inputs) → 956f69a5 (T456, nine)
→ 10ec9bed (B606, eight: `eqlsstr_us.txt` left the set).
`WilleqDatabaseShippedContent.*` (both directions) green at the tip.

## RED → GREEN (ratchet `scripts/refactor/check_zero_callers.py --repo . --check`)

Baseline at origin/fable 7e17067e [measured, detached worktree]:

```
zero-caller census clean: 364 functions, all classified in EXEMPT (boarded 364).
tests-only census clean: 292 functions, all classified in EXEMPT_TESTS (boarded 71, observer 169, seam 52).
```

RED at commit 1 (a27bd28a — deletions in, ratchet rows still present)
[measured, `RED_zero_callers.txt`; the script at that commit is byte-identical
to the one at origin/fable — `diff` verified] — exit 1:

```
zero-caller census clean: 364 functions, all classified in EXEMPT (boarded 364).
STALE EXEMPT_TESTS exemption (no longer tests-only): `LiftTravelSeconds` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `TargetTypeRestrictsCreature` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `optionsClipPlaneStep` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `optionsVolumeFraction` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `optionsVolumeStep` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `parseFormattedMessageText` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `reloadRaceMappings` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `toString` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `BackgroundWorkQueue::pollAll` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `BackgroundWorkQueue::setBatchHooks` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `HotkeyBinding::toString` -- … B605
STALE EXEMPT_TESTS exemption (no longer tests-only): `IrrlichtRenderer::setNewUIEnabled` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `RDPServer::getCertPath` -- (observer)
STALE EXEMPT_TESTS exemption (no longer tests-only): `RDPServer::getKeyPath` -- (observer)
STALE EXEMPT_TESTS exemption (no longer tests-only): `RDPServer::setCertificate` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `StringDatabase::isDbStrLoaded` -- (observer)
STALE EXEMPT_TESTS exemption (no longer tests-only): `StringDatabase::isEqStrLoaded` -- (observer)
STALE EXEMPT_TESTS exemption (no longer tests-only): `StringDatabase::loadDbStrFile` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `StringDatabase::loadEqStrFile` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `WilleqDatabase::getLoginString` -- … B606
STALE EXEMPT_TESTS exemption (no longer tests-only): `ZoneLifecycleComponent::applyStoredZoneEnvironment` -- … B606
tests-only: 0 unlisted, 21 stale.
```

(16 × B606 + 1 × B605 + 4 × observer = 21; the RED reason text is elided here
— the full lines are in `RED_zero_callers.txt`.)

GREEN at commit 2 (0f141f65 — the rows strike) [measured, `GREEN_zero_callers.txt`]:

```
zero-caller census clean: 364 functions, all classified in EXEMPT (boarded 364).
tests-only census clean: 271 functions, all classified in EXEMPT_TESTS (boarded 54, observer 165, seam 52).
```

Accounting: 292 − 21 = 271; boarded 71 − 17 = 54 (the 16 B606 rows + B605's
`HotkeyBinding::toString`); observer 169 − 4 = 165 (the four subjects above);
seam 52 unchanged. The zero-caller census is untouched at 364 — every B606
row was tests-only, not zero-caller (each had test callers until the tests
went with the functions).

The gate is live: re-running the HEAD-at-commit-1 script against the tip tree
reproduces the RED exactly, so a regression that resurrects a deleted function
(or a test that calls one) fails `ZeroCallers` again. `ZeroCallersSelfTest`
untouched. `check_write_only_fields.py --check` green at tip
(`write_only_fields.txt`; was also green at baseline — the cascade members
went, not reclassified).

## Build and tests

- `cmake --build build` (Release, GLES2, tests on, -j3) green at tip —
  `fullbuild2.log`, `build2 exit: 0` (the first full build, `fullbuild.log`,
  caught the `const char**` error fixed in commit 3).
- Test runs at tip (rebuilt binaries, `./build/bin/<test>`):

| test | PASSED |
|---|---|
| test_formatted_message | 36 |
| test_string_database | 30 |
| test_willeq_database | 20 |
| test_background_work_queue | 20 |
| test_rdp_server | 24 |
| test_options_window | 29 |
| test_spell_visual_selection | 7 |
| test_spell_body_type_targeting | 9 |
| test_race_names | 19 |
| test_door_motion | 16 |
| test_display_text_from_db | 9 |
| test_frame_component | 21 |
| test_zone_lifecycle_component | 13 |

  `test_willeq_database --gtest_filter='*ShippedContent*:*Reproducible*'`:
  `WilleqDatabaseShippedContent.EveryTableTheClientQueriesExists` and
  `…EveryTableTheDatabaseShipsIsOneTheClientQueries` both OK (the two
  shipped-content tests the login_strings drop would have turned red),
  `DbReproducible` OK (the rebuilt db matches its new sha256 pin).
- `ctest --test-dir build -L hygiene`: **100% tests passed, 0 tests failed
 out of 28** (`hygiene_tip.txt` in the evidence dir).

 ## CI verification

 MR pipeline 833 (head 2a6a20d8, test job 4175) failed exactly one test,
 `TmpPathCensus.EveryTempPathInTestsHasAPerProcessComponent` (5725/5726 passed;
 `job4175_trace.txt` in the evidence dir) — a defect of THIS branch that local
 verification missed: the rewritten `tests/test_string_database.cpp` (line 31)
 and `tests/test_display_text_from_db.cpp` (line 73) both build their
 throwaway SQLite dbs under `tmp ? tmp : "/tmp"`, the same pattern already
 carrying precedent waivers for `test_willeq_database.cpp` and
 `test_item_model_mapping.cpp`, without their own. The census flags the bare
 `"/tmp"` literal; the joined file names carry a per-process component
 (getpid / `this`), so the two missing waiver rows are the fix. Added to
 `tests/test_tmp_path_census.cpp` (this commit). After the fix, locally:
 `ctest -R TmpPathCensus` 2/2 Passed and `ctest -L hygiene` 28/28 Passed;
 `test_string_database` 30 and `test_display_text_from_db` 9 cases also
 re-passed on the tip binaries (the waiver change touches only the census
 test, so those runs stand).

Pipeline 844 (head 36dfd68c) then failed on INFRASTRUCTURE, not code — the
B1012 runner-split that the 833 wait was warned about (job 4250 hygiene and
4251 build ran on gitlab-runner-1, both success; job 4252 test landed on
gitlab-runner-2 and died before running a single test: `Failed to change
working directory to ".../build/ref/task-kb-b606-renderer-asset-data-zero-
callers": No such file or directory` — the warm build tree exists only on
runner-1's disk; `job4252_trace.txt` in the evidence dir). The test job ran
zero tests, so it cannot speak to this branch's code. The code question was
already answered by 833: 5725/5726 passed with exactly the one failure now
fixed. Fix is on the CI side: untag runner-2 from the x86-build tag (or fold
the stages into one job), per `.gitlab-ci.yml`'s own comment and board B1012.

Pipeline 846 (head e43bf1a3, MR pipeline on `refs/merge-requests/145/head`)
is FULLY GREEN: hygiene 4264, build 4265 (29m10s), test 4277
(`100% tests passed, 0 tests failed out of 5726`, incl. both
`WilleqDatabaseShippedContent.*` directions, `DbReproducible`,
`TmpPathCensus` 2/2), gate 4268 (visual frames identical). Job 4277's trace
is `ci_test_job_4277_trace.txt` in the evidence dir; per-suite `Passed` lines
in `ci_trace_b606_suites.txt`.

Run 4 (post-merge of 46 fable commits, origin/fable 06852f31 → 657c456c)
re-verified everything at merge tip d00bc8d4: `check_zero_callers.py --check`
clean (364 / tests-only 271), `check_write_only_fields.py --check` clean,
`ctest -L hygiene` 29/29 (one test more than run 2's 28 — the count drifted
with fable's own merges; label green either way), all 14 touched test
binaries rebuilt and passing (`affected_tests_mergetip.txt`), and the
fable-range diff re-introduced none of the 18 deleted names (probe:
`check_fable_overlap.sh`).

Pipeline 848 (head ff7e6a16, this README commit — a docs-only diff on top of
the code 846 graded) is the closing gate and is FULLY GREEN: hygiene 4279,
build 4280, test 4281, gate 4283. Test job 4281: `100% tests passed, 0 tests
failed out of 5733` (5733 = 5726 + 7 tests fable's own 46 merged commits
added; trace `ci_test_job_4281_trace.txt` in the evidence dir).

## Docs touched

- `refactor/BOARD.md` — the B606 bullet struck (every mention: the bullet is
  the only live one; the other two hits are historical prose in other
  bullets' close-out notes).
- `docs/standards/rdp_implementation_guide.md` — the "Custom certificate
  support" paragraph rewritten (now at line 942; see RDP certificate
  decision); the embedded class snapshot (line 113) labelled a
  pre-deletion snapshot.
- `data/willeq.db.sha256` header — nine tables / 28,321 rows / 470-row
  login_strings drop / digest chain.
- Stale in-code comments: `src/client/willeq_database.cpp` (T256 paragraph),
  `tools/willeq_db_builder.cpp` (taint paragraph),
  `tests/test_string_database.cpp` / `tests/test_background_work_queue.cpp` /
  `tests/test_rdp_server.cpp` / `tests/test_spell_visual_selection.cpp`
  tombstone comments naming the deletions.
- `docs/module_map.md` — re-measured line counts for the seven TUs this card
  shrank (see "module_map" below).

## module_map

`wc -l` after the last edit (the card's measure-don't-quote rule):

```
766  src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp
1019 src/client/graphics/rdp/rdp_server.cpp
202  src/client/string_database.cpp
215  src/client/formatted_message.cpp
679  src/client/willeq_database.cpp
505  tools/willeq_db/db_loaders_text.cpp
472  tests/test_string_database.cpp
```

## Not verified / left open

- `tests/test_string_database.cpp` and `test_display_text_from_db` now build
  throwaway SQLite dbs (the legacy-file mode is gone); they have been run
  locally but a live-server string check is out of scope for this
  offline card.
- The `BOARD_REASONS["B606"]` entry in `check_zero_callers.py` is inert now
  (no EXEMPT row references it — `grep -c '"B606"'` = 0 apart from the key
  itself) but stays until #189's issue closes on merge; striking it is the
  orchestrator's post-merge step, same as the issue relabel.
