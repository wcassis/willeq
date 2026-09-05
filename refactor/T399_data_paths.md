# T399 — every shipped data file resolves through one path rule (B235)

Class B (reliability: the client works only when started from the repo
root), slot2, no server. Read B235 (the ~40-file census), T397's §Review
Leg A (`eqt::ResolveDataPath()` — CWD first, `$WILLEQ_DATA_ROOT`, the
executable's parents), `include/client/data_paths.h`.

1. Re-take the census on your tip (`grep -rnE '"(config|data|assets)/[^"]*"'
   src/client include/client tools`), one row per literal: file, loader,
   what happens on a miss today (silent default / WARN / fatal).
2. Route every load through `ResolveDataPath()` — including the two
   weather files T397 left (T396 has merged; they are yours now) and
   `data/willeq.db` (the version check stays). One commit per family
   (core/app, config/*.json loaders, textures, audio/index, others). The
   miss behaviour does not change in this unit unless it is SILENT — a
   silent default becomes an ERROR log (say which ones).
3. Tests: extend `CwdElsewhere` (T397) to a table over every routed
   literal: from a temp CWD the file resolves and its bytes equal the
   repo's (pre-fix failing for each family); plus a source-reader pin:
   no bare `"config/…"`/`"data/…"` literal outside `ResolveDataPath`
   call sites (grep-based, the census as a test).
4. `willeq --help` / a headless `--script` run started from `/tmp` must
   reach zone load with the same log as from the repo root (compare the
   two logs' data-file lines).
5. Strike B235; update `CLAUDE.md`'s startup notes if they say "run from
   the repo root" (DocClaims/DocCounts stay green).

## Proof

Build, `ctest -j6` (no expected failures), pre-fix/post-fix per family,
file sizes; textures/graphics loads change → visual gate standalone +
ARM (CWD-first keeps the gate byte-identical — a moved golden is a STOP).

STOP: a load site cannot take a resolved path (a library API that opens
by name itself — name it and board it).

## Review

Executed 2026-09-05 in `wt/slot2`, branch `task/T399-data-paths`, base
`4e351e21` (verified at checkout). No server lock taken (T389 holds it); no
live run.

| family | commit | literal sites |
|---|---|---|
| 1 core/app | `3128a1fa` | 12 |
| 2 config/*.json loaders | `239b51c9` | 14 |
| 3 data/textures/*.bmp | `2061b4a0` | 8 |
| 4 audio + index caches | `76c1371c` | 5 |
| 5 the rest | `35d26d9a` | 2 |
| tests | `99a61191` | — |
| docs + board + this review | this commit | — |

**41 sites, 0 STOPs.** No load site refused a resolved path; the plan's STOP
(a library API that opens by name itself) never fired.

### The census, before and after

`grep -rnE '"(config|data|assets)/[^"]*"' src/client include/client tools`,
re-taken on the tip. `tools/` is covered for the record and deliberately NOT
routed — T398 owns two of those files and the rest are build-time utilities
run from the repo root by hand.

| file (one row per literal) | loader | miss behaviour BEFORE | AFTER |
|---|---|---|---|
| `config/constrained_presets.json` | `ConstrainedRendererConfig::loadJsonOverrides` (x3 call sites) | **returned false, NO log** | resolved, **ERROR** |
| `config/debug.json` | `loadDebugOverrides` | **returned false, NO log** | resolved, **ERROR** |
| `config/race_models.json` | S03 `checkFile` + `renderer_init` | FATAL | resolved, FATAL |
| `data/item_models.json` | S03 `checkFile` + `EntityRenderer` | FATAL | resolved, FATAL |
| `data/willeq.db` | `Application::initEverQuestClient` | WARN + legacy loading | resolved, WARN (version check unchanged) |
| `config/hotkeys.json` | `HotkeyManager::loadFromFile` (x2 call sites) | **DEBUG** | resolved, **ERROR** |
| `config/skill_caps.json` | `SkillCapTable::load` | ERROR | resolved, ERROR |
| `data/zone_lines/<zone>.json` | `ZoneLines::loadFromZone` | WARN | resolved, WARN |
| `data/zone_lines.json` | `ZoneLines::loadFromZone` | WARN | resolved, WARN |
| `willeq.json` | `Application::loadConfigFile` | ERROR | resolved, ERROR |
| `config/display_settings.json` | `loadDisplaySettingsFromFile` | **INFO "using defaults"** | resolved, **ERROR** |
| `config/eqe1_archives.json` | `Eqe1ArchiveComponent::loadEQE1ArchiveConfig` | **INFO**, built-in archive list | resolved, **ERROR** |
| `config/detail_objects.json` | `DetailConfigLoader` (x3) | nullptr / **INFO x2** | resolved, nullptr / **ERROR x2** |
| `config/tree_wind.json` | `TreeWindConfigLoader::findConfigFile` | **silent, returns ""** | resolved, **ERROR** |
| `config/zones/<z>/tree_wind.json` | same | silent | resolved, silent (optional) |
| `config/zones/<z>/tree_overrides.json` | `TreeIdentifier` | DEBUG | resolved, DEBUG (optional) |
| `config/zones/<z>/weather.json` | `loadZoneWeatherConfig` | silent | resolved, silent (optional) |
| `data/config/weather.json` | `loadZoneWeatherConfig` | silent | resolved, silent (optional) |
| `config/weather_effects.json` | `WeatherConfigLoader::load` | **WARN, returned TRUE** | resolved, **ERROR** |
| `config/environment_effects.json` | `EnvironmentEffectsConfig::load` | **WARN, returned TRUE** | resolved, **ERROR** |
| `config/spell_effects.json` | `ParticleManager::init` | ERROR (T397) | call site resolves too |
| `data/textures/spell_particle_fallback.bmp` | `SpellVisualFX` | FATAL + `exit(1)` | resolved, unchanged |
| `data/textures/detail_atlas.bmp` | `DetailManager` | FATAL + `exit(1)` | resolved, unchanged |
| `data/textures/<rain>.bmp` (16) | `RainOverlay` | FATAL + `exit(1)` | resolved, unchanged |
| `data/textures/<snow>.bmp` (14) | `SnowOverlay` | FATAL + `exit(1)` | resolved, unchanged |
| `data/textures/tumbleweed.bmp` | `TumbleweedManager` | FATAL + `exit(1)` | resolved, unchanged |
| `data/textures/creature_atlas.bmp` | `BoidsManager` | FATAL + `exit(1)` | resolved, unchanged |
| `data/textures/storm_cloud_blend_<n>.bmp` (32) | `StormCloudLayer` | FATAL + `exit(1)` | resolved, unchanged |
| `data/textures/software_cursor.bmp` | `createSoftwareCursor` | FATAL + `exit(1)` | resolved, unchanged |
| `data/textures/particle_atlas.bmp` | `ParticleManager` (x2) | FATAL + `exit(1)` | resolved, unchanged |
| `config/snd_index_cache.json` | `AudioManager` load **and save** | silent (a cache) | resolved, silent — correct |
| `config/music_events.json` | `AudioManager` | **DEBUG "using defaults"** | resolved, **ERROR** |
| `config/gfx_index_cache.json` | `GraphicsArchiveIndex` load **and save** | silent (a cache) | resolved, silent — correct |
| `data/eqe1_texture_index.json` | `renderer_zone_load.cpp` | ERROR | resolved, ERROR |
| `data/detail/zones/<z>_surface.map` | `DetailManager::loadSurfaceMap` | silent | resolved on the complete path, silent |
| `data/region_maps/<z>.json` | `DiagnosticsComponent::loadIndoorRegionMap` | DEBUG | resolved on the complete path, DEBUG |

**Eleven silent-or-DEBUG defaults became ERROR logs** (bold "AFTER" rows). Two
of them logged *nothing at all* — `loadJsonOverrides` and `loadDebugOverrides`
just returned `false` into a caller that ignored the return, so the renderer
could be running on built-in preset numbers with no trace anywhere.

**Three kept quiet, on purpose, and the code says why.** The `config/zones/*`
and `data/config/weather.json` candidates are per-zone overrides and **neither
directory exists in the tree** — an ERROR per zone load would be pure noise.
The two index caches are caches: a miss means "rescan the archives", which is
the correct, cheap answer. Both caches are resolved on the WRITE side too, so
a rewrite lands on the file the read side found; with no such file
`ResolveDataPath` returns its input unchanged and the write goes exactly where
it always did.

**Directory knobs are resolved on the COMPLETE path at the open**, not at the
knob — `setSurfaceMapsPath("data/detail/zones")` and
`config.regionMapsPath` stay configurable, and an absolute override is returned
untouched. Those four header defaults plus two call sites are the waivers in
the pin test, each with its reason.

### Tests

`tests/test_data_path_routing.cpp` (**new, 6 tests**) is the census as a test:
a source reader with no link dependencies that strips comments and fails on any
`"config/…"` / `"data/…"` literal not on a `ResolveDataPath` line. Five tests
are the five families; the sixth walks **all** of `src/client` +
`include/client` (2,000+ files enumerated, `ASSERT_GT(files.size(), 200)` so an
empty walk cannot pass silently), which is the part that catches a literal in a
file nobody has thought about yet. Ten waivers, each with a written reason.

Pre-fix reds were produced by exporting each family's source diff and
`git apply -R` (never `git stash` — it is shared across slots):

| family reverted | test | pre | post |
|---|---|---|---|
| 1 | `DataPathRouting.CoreAndAppLoadsAreRouted` | **FAIL**, 12 unrouted | PASS |
| 2 | `DataPathRouting.ConfigJsonLoadersAreRouted` | **FAIL**, 14 unrouted | PASS |
| 3 | `DataPathRouting.TextureLoadsAreRouted` | **FAIL**, 8 unrouted | PASS |
| 4 | `DataPathRouting.AudioAndIndexCacheLoadsAreRouted` | **FAIL**, 5 unrouted | PASS |
| 5 | `DataPathRouting.RemainingDataLoadsAreRouted` | **FAIL**, 2 unrouted | PASS |
| any | `DataPathRouting.TheWholeClientTreeIsRouted` | **FAIL** | PASS |

In every case the other four family tests stayed green, so the split is real
and not a single shared assertion.

`tests/test_data_paths.cpp` (**7 -> 14 tests**) is the behavioural half. Every
new test runs with the CWD moved to `/` (the fixture asserts the literal really
is unopenable there, so a pass cannot be an accident):

| test | pre | post |
|---|---|---|
| `CwdElsewhere.EveryShippedLiteralResolvesToTheRepoBytes` (30 files) | PASS (resolver is T397's) | PASS |
| `CwdElsewhere.ConstrainedPresetOverridesLoadFromAnyDirectory` | **FAIL** — "the shipped preset overrides were not applied" | PASS |
| `CwdElsewhere.HotkeyBindingsLoadFromAnyDirectory` | **FAIL** — "the shipped key bindings were not loaded" | PASS |
| `CwdElsewhere.SkillCapTableLoadsFromAnyDirectory` | **FAIL** — "skill possession gate DISABLED" | PASS |
| `CwdElsewhere.ZoneLinesLoadFromAnyDirectory` | **FAIL** | PASS |
| `CwdElsewhere.WeatherAndEnvironmentConfigsLoadFromAnyDirectory` | **FAIL** — both settled on an unopenable path | PASS |
| `CwdElsewhere.DetailObjectConfigLoadsFromAnyDirectory` | **FAIL** — hard-coded default in effect | PASS |

The bytes-equal table matters more than it looks: an existence check would
pass on *a* file named `race_models.json` somewhere else and still be the wrong
client.

### /tmp vs repo root (plan step 4)

`build/bin/willeq --help` exits 0 from either directory. A headless run
(`-c willeq.json --no-graphics -d 5`, 40 s, no server — T389 holds the lock) was
taken from the repo root and from `/tmp/t399_cwd`. Normalising away timestamps
and the absolute repo prefix, **the two logs are identical except for timing
numbers and one timing-dependent `SendPositionUpdate skipped` DEBUG line.** The
data-file lines:

```
repo root:  HotkeyManager: Loaded 94 bindings from config/hotkeys.json
            data/willeq.db (x2)
/tmp:       HotkeyManager: Loaded 94 bindings from <repo>/config/hotkeys.json
            <repo>/data/willeq.db (x2)
```

Same 94 bindings, same database, from a directory that has neither. Both runs
stop at the login connect, as expected with no server.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **PASS** (exit 0) |
| `ctest --test-dir build -j6` | 4,120 / 4,122. Two failures, both handled below |
| `scripts/build-gles2-x86.sh` | **PASS** (exit 0) |
| visual gate standalone | **FAIL — not this branch**, see below |
| `scripts/build-arm-noble.sh` | **PASS** (exit 0), ELF 32-bit ARM produced |
| `check_file_sizes.sh fable` | **42 checked, 42 ok, 0 warn, 0 fail** |
| pixel goldens | untouched |

`ApplicationInitOrderTest.ComposedStartupOrderMatchesThePreG04Golden` failed on
statement text, not order: eight `application.cpp` statements gained a
`eqt::ResolveDataPath(...)` wrapper. Per G-correction 32 the statement-text
golden **learns a reviewed edit in place**; `tests/goldens/application_init_order_g04.txt`
was edited on those eight lines only (315 entries before and after, same
sequence) and the test is green.

**The visual gate FAIL is not this branch's.** It is `eqe1MappedBytes`, off by
**exactly +239 bytes in all three zones** — the signature of one shared file
every zone maps, not of anything zone-dependent. `/mnt/projects/EverQuest_ETC1/willeq.eqe1`
is the only file in that shared tree modified on 2026-09-05 (21:20, during this
unit), and T398 Leg B (`23378528`, merged to `fable` as `3f328966`) is what put
the four coin decals in it. `tests/goldens/manifest.json` has not changed since
T303 (`a32600a4`, Will's blessing). Every other manifest field matches; all
three CPU p95 numbers are inside their ceilings (6.93/10.11, 6.45/10.67,
4.71/10.95). Nothing in this branch touches EQE1 byte accounting, and the gate
runs from the repo root where `ResolveDataPath`'s CWD-first candidate makes
every read byte-identical to before. **The gate is therefore red on the `fable`
tip itself**; boarded as **B247**, and only Will re-blesses.

### Deviations and surprises

1. **The plan's step 5 doc job was inverted.** `CLAUDE.md` and `README.md` say
   "from project root" only about `cmake` configure, which is still true. There
   was no stale startup claim to strike — there was a *missing* one, so
   `CLAUDE.md` gained a "Where the client's shipped data comes from" section
   with the four-candidate search order and the rule that a new shipped-data
   load must call `ResolveDataPath()`.

2. **Twenty-four build targets hand-pick their sources** and needed
   `src/client/core/data_paths.cpp` added, across `CMakeLists.txt` and four
   `tests/cmake/*.cmake` files. The orchestrator hit the same shape once
   already (`9f093662`, T389's spell-stage target). This is the standing cost
   of a new TU in `src/client/core/` and is worth knowing before the next one.

3. **`loadRaceMappings()` ignores its argument entirely** — the body is
   `(void)jsonPath;` and race data comes from SQLite. Yet S03 still refuses to
   start without `config/race_models.json` and `renderer_init` still LOG_FATALs
   on a false return that only ever means "no database". A shipped JSON file is
   a hard startup requirement for a loader that never opens it. Boarded **B246**;
   T399 routed the literal rather than change behaviour mid-unit.

4. **Two shipped config files nothing reads**: `config/chat_settings.json` and
   `config/ui_settings.json` are git-tracked, `CLAUDE.md` documented the first
   as live user settings, and neither name appears in any `.cpp` or `.h` under
   `src/`, `include/` or `tools/`. They are invisible to B235's grep precisely
   because no literal names them. Boarded **B245**; the `CLAUDE.md` line now
   says they are dead.

5. **`ResolveDataPath` is idempotent**, which is what makes "resolve at the
   call site AND at the leaf open" safe: an absolute path is returned
   unchanged, and a relative one that exists is returned unchanged. That is why
   the pin can demand the literal and the `ResolveDataPath` on the same line
   without forcing a choice between the two layers.

6. **A plain client run dirties the working tree.** `config/snd_index_cache.json`
   is git-tracked and `AudioManager` rewrites it on every run; the 40 s headless
   run in step 4 left it modified (identical content — 17 archives, 2,088 index
   entries, same `eqPath` — only the serialisation differs) and it had to be
   restored from `HEAD` by hand before committing. `config/gfx_index_cache.json`
   is the same shape. Boarded **B248**: this is a live trap for any executor who
   runs the client and then `git add -A`.
