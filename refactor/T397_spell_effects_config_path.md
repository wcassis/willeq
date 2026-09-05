# T397 — `spell_effects.json` resolved like every other data file (B221); `removeSpellEffect` (B220)

Class B ×2, slot2, no server. Read the B221 and B220 board text and
T395's §Review.

## Leg A — B221

`ParticleManager` loads `config/spell_effects.json` by a CWD-relative
path; started from anywhere but the repo root the load fails silently and
every preset falls back to a DIFFERENT effect (permanent 10/s emitter vs
a 36-particle 0.1 s burst). Resolve it the way `willeq.db` and the config
root are resolved (read `Application`/the config loader for the one
data-root resolution — reuse it, do not add a second); log at ERROR when
the file is missing (a silent fallback to a different effect is the
defect). Tests, pre-fix failing: constructing the manager with the CWD
elsewhere still loads the shipped config; a missing file logs and the
preset is the shipped one, not the struct default (or the load is fatal —
decide from how the other data files behave and say which). Census: any
other CWD-relative data path under `src/client` (grep for `"config/`,
`"data/`, `fopen(`, `std::ifstream(` with relative literals) — route or
board each. Strike B221.

## Leg B — B220

`ParticleManager::removeSpellEffect()` writes `preAssignedEffectID` while
the worker matches `effectID` (never set, defaults 0, ids start at 1) —
it can never remove anything, and it has zero callers (six directories).
Read T395's fix to the lifecycle: does anything NEED removal now (a
spell interrupted mid-burst; `removeCastGlow` on interrupt — how is the
glow removed today)? If a live path needs it, fix the id mismatch and
wire the caller with a test (pre-fix failing: an effect removed by id is
gone from the worker's set); if nothing needs it, delete it (K12 ff).
Say which. Strike B220.

## Proof

Build, `ctest -j6` (no expected failures), pre-fix/post-fix runs, file
sizes; graphics change → visual gate standalone + ARM (the gate runs from
the build dir — if the gate's zones show a DIFFERENT spell-free frame
after the path fix, that is the confound made visible: STOP with the
contact sheet).

STOP: the census finds a data path whose resolution changes the visual
gate's inputs.

## Review

Executed 2026-09-05 in `wt/slot2`, branch `task/T397-spell-effects-config-path`,
base `6cb7a202` (verified at checkout). No server lock taken; no live run.

| leg | board | commit |
|---|---|---|
| A | B221 struck, **B235** added | `8382268d` |
| B | B220 struck | this commit |

### Leg A — B221: there was no data-root resolution to reuse

The plan says "reuse the one data-root resolution, do not add a second."
**There is not one.** `willeq.db` is the literal `"data/willeq.db"` in
`application.cpp`; `willeq.json` itself defaults to the bare name
`"willeq.json"` (`include/client/application.h`); the census below found ~40
such literals under `src/client`. The client works only because everything
starts it from the repo root. So T397 wrote the first one:
`eqt::ResolveDataPath()` (`include/client/data_paths.h`,
`src/client/core/data_paths.cpp`, 80 lines).

**Search order, first existing file wins** — and the order is the whole safety
argument:

| # | candidate | why |
|---|---|---|
| 1 | the path itself, if absolute | never rewrite what a caller resolved |
| 2 | **the CWD-relative path** | pre-T397 behaviour, deliberately FIRST |
| 3 | `$WILLEQ_DATA_ROOT/<rel>` | a staged install whose data is not beside the binary |
| 4 | the executable's directory, then up to 3 parents | `<repo>/build/bin/willeq` → `<repo>/config/...` |
| — | otherwise the input, unchanged | so the caller's "could not open X" names X |

Because candidate 2 is first, **every run from the repo root reads exactly the
bytes it read before** — the visual gate included. That is what makes the STOP
condition unreachable rather than merely unmet.

**The second half of the defect was the silence.** `SpellEffectsConfig::load()`
logged a miss at WARN, said "using defaults", and **returned true**, so no
caller could distinguish "loaded" from "silently a different effect". It now
logs at ERROR naming both the resolved and the requested path, and returns
false; `ParticleManager::init()` logs that its presets are the fallback table.
**Not fatal**, matching the closest comparable: a missing `willeq.db` is a
`LOG_WARN` + continue (`EverQuest::OpenDatabase`). Only a missing *texture*
(`loadParticleAtlas`) is fatal, and that is a hard render dependency.

**Three corrections to B221's text, all measured, all on the board:**

1. The load is in `ParticleManager::init()`, not the constructor.
2. The missing-file fallback is `setDefaults()`'s built-in style table, whose
   `impact` burst is **12** against the shipped file's **36**. `EmitterConfig`'s
   struct defaults — the permanent 10/s emitter B221 describes — are what you
   get when `load()` is **never called at all**, which is the harness state
   T395 measured, not the production miss. Both are wrong; they are different
   wrongs and the test pins all three numbers (36 / 12 / 0).
3. "No other data file is reached this way" is **false**. See B235.

**Tests** — `tests/test_data_paths.cpp`, 7 tests, **2 red pre-fix**. Both
defect tests run with the CWD moved off the repo root (`chdir("/")`, restored
in `TearDown`, with an assertion that the literal really is unopenable there —
otherwise the test proves nothing):

| test | pre | post |
|---|---|---|
| `CwdElsewhere.TheShippedConfigIsFoundFromAWorkingDirectoryThatHasNone` | PASS | PASS |
| `CwdElsewhere.AnAbsolutePathIsReturnedUnchanged` | PASS | PASS |
| `CwdElsewhere.AnUnresolvablePathIsReturnedUnchanged` | PASS | PASS |
| `CwdElsewhere.TheProductionStringLoadsTheShippedConfigFromAnyDirectory` | **FAIL** (burst 12, want 36) | PASS |
| `CwdElsewhere.AMissingConfigFileIsReportedAsAFailure` | **FAIL** (returned true) | PASS |
| `DataPaths.TheCwdRelativePathWinsWhenItExists` | PASS | PASS |
| `DataPaths.ZzzRestoreTheShippedConfigForTheSingleton` | PASS | PASS |

The five green-pre-fix tests exercise the resolver, which is itself the fix;
the two that pin the defect call `load("config/spell_effects.json")` — the
exact string `ParticleManager::init()` uses.

#### Census (board B235) — CWD-relative shipped-data paths under `src/client`

`grep -rnE '"(config|data|assets)/[^"]*"' src/client`, 44 hits, ~40 distinct
files. Routed: **`config/spell_effects.json` only** (B221's closing
condition). Boarded as **B235**: `data/willeq.db`,
`config/constrained_presets.json`, `config/race_models.json`,
`config/debug.json`, `config/hotkeys.json`, `config/display_settings.json`,
`config/eqe1_archives.json`, `config/detail_objects.json`,
`config/tree_wind.json`, `config/music_events.json`,
`config/snd_index_cache.json`, `config/gfx_index_cache.json`,
`config/zones/<zone>/{weather,tree_wind,tree_overrides}.json`,
`data/item_models.json`, `data/eqe1_texture_index.json`,
`data/zone_lines*.json`, `data/region_maps`, `data/detail/zones`, and eight
`data/textures/*.bmp` loads. Plus `willeq.json`'s own default.

**Deliberately NOT routed:** `config/environment_effects.json` (loaded on the
line above the fixed one, in `particle_manager.cpp`) and
`config/weather_effects.json`. Their loaders are the weather/ambient files
T396 owns in the parallel wave. The call site carries a comment saying so, and
B235 names them.

**The STOP condition did not fire.** No data path's resolution changes the
visual gate's inputs: the gate runs from the repo root, and candidate 2 makes
that case byte-identical. Goldens untouched — see the verification table.

### Leg B — B220: delete the dead one, and the live one it was hiding

**Decision: DELETE `removeSpellEffect(uint32_t)`.** Re-confirmed zero callers
across `src/ include/ tests/ tools/ benchmarks/ docker/`. The stronger reason
is upstream of the caller count: **no caller anywhere keeps the id
`createSpellEffect()` returns.** `SpellVisualFX::createBuffAura()` is the only
function that even propagates one, and its two callers
(`spell_visual_fx.cpp`, `tools/model_viewer/model_viewer_spell_test.cpp`)
discard it. There is nothing to wire a by-id removal to, and T395's lifecycle
fix did not create one — a spell interrupted mid-burst is removed by *entity*
(`removeCastGlow`), which is the path that exists. Deleted with its
`ParticleCommand::RemoveSpellEffect` enumerator, its `sim_particles.cpp` arm
and the now-unreferenced `ParticleCommandData::effectID` field; four
tombstones name the recover-with hash.

**AND TRACING IT FOUND A LIVE CLASS-A DEFECT.** The plan asked "how is the
glow removed today?" — the answer is `removeSpellEffectsForEntity()`, and it
had the **identical wrong-field bug**, where it was not harmless:

    ParticleManager::removeSpellEffectsForEntity(id):  cmd.targetID = id;   // entityID left 0
    SimulationWorker RemoveSpellEffectsEntity arm:     casterEntityID == cmd.entityID
                                                    || targetEntityID == cmd.entityID

Every cast glow is created as `createSpellEffect(def, caster_id, 0)` and every
impact as `createSpellEffect(def, 0, target_id)` — so **one of the two entity
fields is always 0**, and `cmd.entityID == 0` matched *every spell effect
alive in the zone*. The call behaved as `clearAllSpellEffects()` while
removing nothing it had been asked to. `SpellVisualFX::createCastGlow()`
issues three of them before **every single cast**, so any player beginning to
cast wiped every other player's impacts and glows.

That is worth flagging beyond this unit: it is a plausible contributor to
B204's "the observer's pixels did not move" and to **B160's owed observer
pair (T389)** — in a two-client session the second client's cast deletes the
first client's impact. It is not a claim, it is a mechanism; the live pair is
still owed and still T389's.

**Tests** — `tests/test_spell_effect_removal.cpp`, 3 tests, **2 red pre-fix**.
The removal command is **drained from a real `ParticleManager`** and fed to a
real `SimulationWorker::computeOnce()`, so the producer field is under test,
not restated. The observable is
`SimulationOutput::particleOutput.positionRequestEntities`, which the worker
rebuilds every frame from its live `particleSpellEffects_` set:

| test | pre | post |
|---|---|---|
| `SpellEffectRemovalTest.BothEffectsAreLiveBeforeAnyRemoval` | PASS | PASS |
| `SpellEffectRemovalTest.RemovingOneEntityLeavesEveryOtherEffectAlone` | **FAIL** (`entityID` 0; impact also gone) | PASS |
| `SpellEffectRemovalTest.RemovingAnEntityWithNoEffectsRemovesNothing` | **FAIL** (2 effects → 0) | PASS |

No golden covers `particle_manager.cpp`, `sim_particles.cpp` or
`spell_effects_config.cpp` (`frame_render_order_g05` pins
`renderer_frame_render.cpp`, untouched), so no statement golden changed and
none was re-blessed.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** |
| `ctest --test-dir build -j6` | see final report — 0 failures |
| `bash scripts/build-gles2-x86.sh` | see final report |
| `scripts/ci/visual_gate.sh` standalone | see final report, goldens untouched |
| `bash scripts/build-arm-noble.sh` | see final report |
| `scripts/refactor/check_file_sizes.sh fable` | see final report |

### Deviations and surprises

1. **The plan's premise was wrong in the executor's favour.** "Reuse the one
   data-root resolution" — there is none, anywhere. `willeq.db` and
   `willeq.json` are both bare CWD-relative literals. The unit had to write
   the first resolution rather than reuse one, and the census (B235) is the
   evidence that B221 was one instance of a tree-wide convention, not a
   one-off.
2. **B221's stated fallback is not the production fallback.** The permanent
   10/s emitter it describes needs `load()` to have been *never called*; a
   missing file gives the built-in table's 12-particle burst. T395 measured
   the harness state and wrote it up as the production state.
3. **B220 was the smaller half of what it pointed at.** The by-id removal is
   dead code with no consumer, exactly as filed. Its sibling — the entity
   removal that every live caller uses — had the same defect and was
   destroying every spell effect in the zone on every cast. A "zero callers,
   nothing is broken today" item was sitting one line above a class-A defect.
4. **CWD-first ordering is load-bearing, not a nicety.** It is the reason a
   path change to a graphics data file could not move a golden pixel, and the
   reason the plan's STOP condition could not fire.
5. **`git`-recorded file mode.** The three new files landed as 100755 on
   first commit (the worktree's umask); corrected to 100644 before the Leg A
   commit was finalised, which is why Leg A's hash is `8382268d` and not the
   `ccff8d71` that appears in no artefact.
