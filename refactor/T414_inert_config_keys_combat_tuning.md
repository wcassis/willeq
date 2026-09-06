# T414 — inert config keys (B297); CombatManager dead tuning (B299); the census queue (B290)

Class B, slot3, no server. Read the three board entries and T407's
§Review Leg B (`scripts/refactor/check_write_only_fields.py`, the
exemptions), and T399's data-path census (what each config file feeds).

## Leg A — B297

22 JSON config keys are parsed into fields nothing reads: a user editing
them sees no effect. For each key: name the file, the field, and what it
promises (the comment/README); either wire it to the consumer the name
implies (only if that consumer exists and the wiring is mechanical) or
delete the key from the shipped JSON, the parser and the field, and
strike it from README/CLAUDE.md (DocClaims/DocCounts green). A key whose
consumer would be a feature → board it (B330+) and delete the inert
parse. Tests pre-fix failing: for each wired key, the value reaches the
consumer; for the deleted ones, the source-reader census reports zero
inert keys.

## Leg B — B299

CombatManager's dead tuning fields (T407 blocked on a test edit): read
the fields, the test that pins them, and what the official client's
combat timing does (the server enforces swing timers; the client only
animates) — delete the fields and fix the test, or wire them with a
measured reason. Strike B299.

## Leg C — B290

The census queue: re-run the script on your tip; every remaining
write-only row not exempted (wire/file structs, B296's payloads owned by
T413, B298's record fields) is classified here: delete (compiler proof),
board, or exempt with reason in the script's exemption table. Leave the
script green as a hygiene test (`WriteOnlyFields`, label `hygiene`,
planted fault shown) so a new write-only field fails ctest. Strike B290.

## Proof

Build (background + exit file), `ctest -j6` (no expected failures on
this tip), pre-fix/post-fix, file sizes; if a render path changes,
visual gate standalone + ARM (a moved image golden is a STOP).

STOP: none expected.

---

## Review

Branch `task/T414-inert-config-keys-combat-tuning`, base `53a89546`.

| leg | commit |
|---|---|
| A — inert config keys (B297) | `315619f5` |
| B — CombatManager dead tuning (B299) | `4ac66d13` |
| C — the census queue (B290) | this commit |

### Leg A — the per-key table

B297 named twenty-two keys. Twenty-nine were inert, and the eight that mattered
most were in a file B297 never named.

**WIRED** — `config/display_settings.json`. The file ships fifteen keys;
`renderer_init.cpp`'s `static loadDisplaySettingsFromFile()` read **seven**.
Every one of the other eight names a `DisplaySettings` field with a LIVE
consumer, so only the parse line was missing.

| key | field | promise | consumer that already existed | fate |
|---|---|---|---|---|
| `renderDistance` | `renderDistance` | view/fog distance | `renderer_entities.cpp:246` → `setRenderDistance()` | **wired** |
| `environmentEffects.quality` | `environmentQuality` | effect quality tier | `overlays_hud_env.cpp:271,325` → particle + boids quality | **wired** |
| `environmentEffects.density` | `environmentDensity` | effect density | `overlays_hud_env.cpp:291,328` → `setDensity()` ×2 | **wired** |
| `environmentEffects.reactiveFoliage` | `reactiveFoliage` | foliage disturbance | `overlays_hud_env.cpp:354` → `setFoliageDisturbanceConfig()` | **wired** |
| `environmentEffects.enableFireGlowLighting` | `enableFireGlowLighting` | fire glow lights | `overlays_hud_env.cpp:400` → `ctx_.fireGlowLightingEnabled` | **wired** |
| `environmentEffects.enableFireGlowIcospheres` | `enableFireGlowIcospheres` | glow icospheres | `overlays_hud_env.cpp:401` | **wired** |
| `environmentEffects.maxFireGlowLights` | `maxFireGlowLights` | light budget | `overlays_hud_env.cpp:402` → `eqe1_archive_component.cpp:269,305` | **wired** |
| `detailObjects.density` | `detailDensity` | detail density | `overlays_hud_env.cpp:347` → `DetailManager::setDensity()` | **wired** |
| `detailObjects.grass` | `detailGrass` | grass on/off | `overlays_hud_env.cpp:348` → `setCategoryEnabled(Grass, …)` | **wired** |
| `detailObjects.plants` | `detailPlants` | plants on/off | `overlays_hud_env.cpp:349` | **wired** |
| `detailObjects.rocks` | `detailRocks` | rocks on/off | `overlays_hud_env.cpp:350` | **wired** |
| `detailObjects.debris` | `detailDebris` | debris on/off | `overlays_hud_env.cpp:351` | **wired** |
| `detailObjects.viewDistance` | `detailViewDistance` | detail cull distance | none — `DetailManager` has no `setViewDistance` | **deleted** |

**DELETED** — no consumer, and none that wiring could reach.

| file | field | promise | why deleted |
|---|---|---|---|
| `environment_config.h` | `DetailSettings` (whole struct, 7 fields) | per-category detail toggles | `getDetailObjects()` has **zero callers**; `detail_objects.json` + `DetailManager` own this |
| `environment_config.h` | `BoidsSettings` (whole struct, 11 fields) | flock tuning | `getBoids()` has **zero callers**; `SimulationWorker`'s own knobs + `BoidsBudget::fromQuality()` + per-type `FlockConfig` own this |
| `boids_types.h` | `BoidsBudget::viewDistance` | creature visibility | not a config key; a dead 5th member of ten aggregate initialisers |
| `rain_overlay.h` | `layerDepthMin` / `layerDepthMax` | parallax layer distance | screen-space 2D tiled draw; no world depth. Parallax is `layerScale`/`layerSpeed` |
| `rain_overlay.h` + `snow_overlay.h` | `numLayers` | layer count | **not in B297** — hidden from the census by a local of the same name in both `render()` bodies. Clamped 1..5, logged, and overridden by a hardcoded intensity ladder |
| `snow_overlay.h` | `layerDepthMin` / `layerDepthMax` | as rain | as rain |
| `storm_cloud_layer.h` | `blendDuration` | crossfade seconds | the blend is **pre-baked** into `storm_cloud_blend_N.bmp`; `updateFrameAnimation()` hard-cuts |
| `weather_quality_preset.h` | `fogTransitionSpeed` | fog blend speed | weather fog is set outright, never interpolated |
| `detail_types.h` | `DetailType::atlasX/Y/Width/Height` + the legacy `"atlas"` parse | atlas pixel coords | superseded by `atlasTile` → `getTileUV()`; nothing converted the pixels to UVs, so a legacy config drew tile 0 |
| `detail_types.h` | `ZoneDetailConfig::isOutdoor` | indoor/outdoor | legacy-format key, no reader, two hardcoded writes |
| `constrained_presets.json` | `enableTextureAtlas` | zone atlas on/off | its field was removed by **E10**; the key outlived it in two presets |
| `detail_objects.json` | the whole `seasons` section | per-season tint/density | an **exact duplicate** of `SeasonalController`'s hardcoded table (snow tint `[255,200,220,255]` matches line for line). The class is now documented as the single source |
| `weather_effects.json` | `storm.fogDensityMultiplier`, `fogColorR/G/B`, `lightningIntensityThreshold` | storm fog tint, lightning gate | `WeatherEffectsConfig::Storm` has **no such members**; the loader's only nod was an `if (isMember(...)) { /*two comments*/ }` — boarded B335 |

**BOARDED** (parse deleted, feature named): B330 rain wind tilt
(`windTiltBase`/`windTiltIntensity` — needs a rotated-quad draw), B331 lightning
branch depth (`maxBranchLevel` + the shipped key — there is no branch generator,
and the code said so), B332 `enableItemIcons` (there is no icon atlas — the UI
draws abbreviated names with "placeholder until icon atlas" beside them), B333
`frontToBackSorting` (a dead second spelling of the live `manualZoneDrawEnabled_`
/`/sort`), B334 `DetailType::windHeightBias` (the crossed quad has only base and
top vertices), B335 the storm fog/threshold keys above.

**Behaviour neutrality.** The shipped `display_settings.json` held one machine's
saved slider positions from the DELETED options window — `renderDistance: 200`,
`quality: "low"`, `density: 0.0`, `maxFireGlowLights: 8`,
`enableFireGlowIcospheres: true`, `detailObjects.density: 0.171` — none of which
had ever been in effect, because none was parsed. Wiring the parse without
touching the file would have changed the render distance, the effect quality,
the particle density and the light budget in one commit and moved the visual
gate. The file is instead rewritten to the values that WERE in effect, so the
keys become live and nothing changes today.
`ConfigKeyReachability.ShippedDisplaySettingsIsBehaviourNeutral` pins that, and
names `atmosphericParticles: false` as the one deliberate non-default (it was
parsed before).

**Pre-fix / post-fix.**
- Wiring: the test compiled against a reconstruction of the old seven-key parse
  fails `DisplaySettingsParsesEveryKeyItShips` on twelve fields
  (`renderDistance`, quality, density, reactiveFoliage, the three fire-glow, and
  the five detail); against the new parser all five cases pass.
- Census: `EveryShippedConfigKeyIsNamedByCode` run against the base `config/`
  files reports **23 orphans** and fails; against the current tree it passes.

### Leg B — the combat fields

| field | fate | reason |
|---|---|---|
| `m_assist_range` + `SetAssistRange()` | deleted | `/assist` is a server round-trip (`cmdAssist` → `ActionDispatcher::assist` → packet); the server picks and returns the target |
| `m_combat_range` + `SetCombatRange()` | deleted | real reach is per-target/per-race `CalculateCombatRange(my_size, target_size, race)`, clamped 5..75 and halved; a flat 5.0f contradicted it |
| `m_spell_range` | deleted | range comes from the spell row (`spell.range`) |
| `m_is_resting` | deleted | duplicate of `SetCombatState(COMBAT_STATE_RESTING)` on the line above |
| `CombatTarget::last_considered` | deleted | stamped in the consider path; there is no re-consider throttle |
| `m_rest_mana_threshold` | **kept** | **B299 was wrong**: `ShouldRest()` reads it (`combat_hunting.cpp:226`). It was named from the census summary, not the census output |
| `m_aggro_radius` + `SetAggroRadius()` | kept | `GetNearbyHostiles()` and the hunt scan read it |

**The test edit T407 was blocked on.** The old block asserted `EXPECT_NO_THROW`
on three setters and explained, in a comment, that there were "NO public getters"
to round-trip against. That framing was wrong: the problem was not a missing
accessor but a missing decision — the values reached nothing. Two of the three
setters are gone with their members; the third now gets a real round-trip
through observable behaviour (`AggroRadiusIsHonouredByTheHostileScan`: at radius
0 the hostile scan comes back empty). The comment is replaced by the finding.

Pre-fix proof is compiler-by-deletion — the deleted block compiled against the
new header:
`error: 'class CombatManager' has no member named 'SetAssistRange'` and
`... no member named 'SetCombatRange'; did you mean 'SetCombatState'?`

### Leg C — the census queue

| classification | rows | what it means |
|---|---|---|
| `owned` | 9 | B296's event_bus payload fields — T413 (slot1) territory, untouched |
| `boarded` | 26 | B298 record fields (ItemInstance ×13, SpellData ×6, SkillData ×2, Entity ×3, RaceModelInfo ×2) |
| `boarded` | 1 | B295 `GraphicsArchiveIndex::lazyMode_` |
| `wire` | 8 | the struct mirrors bytes on disk/on the wire |
| `report` | 9 | diagnostic aggregates → new B337 |
| `boarded` | 30 | new B336, B338–B346 |
| **total** | **83** | every row classified; `--check` is clean |

The row count fell **111 → 83** across T414's own legs (Leg A took out 12
config-struct rows, Leg B 5 combat rows, and both exposed rows the deleted
fields' name-collisions had been hiding — `ZoneDetailConfig::viewDistance` is
the notable one, now B336).

`--check` fails **both ways** and the planted-fault proof shows each:

```
# 1. a NEW write-only field (DetailPlacement::t414PlantedFault, one write in
#    detail_chunk.cpp, no read):
NEW write-only field: DetailPlacement::t414PlantedFault   include/client/graphics/detail/detail_types.h:131
1 unlisted, 0 stale.                                                exit 1

# 2. a STALE exemption (a read added to an exempted field):
STALE exemption (no longer write-only): WeatherEffectsController::targetIntensity_
  -- B339: the smooth weather transition it was for does not exist
0 unlisted, 1 stale.                                                exit 1

# restored:
write-only census clean: 83 fields, all 83 classified in EXEMPT.    exit 0
```

The second half is the point: an exemption cannot outlive the row it excuses, so
the table cannot silently rot the way the three trackers K00 replaced did.
Registered as ctest `WriteOnlyFields`, label `hygiene`, 50 s.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** (background + exit file) |
| `ctest --test-dir build -j6` | **4253/4253, 0 failed**, 216.68 s |
| `FileSizeCeiling` / `DocClaims` / `DocCounts` / `DocCitations` | all Passed |
| `check_file_sizes.sh fable` | 18 files checked, 18 ok, 0 warn, 0 fail |
| new tests | `ConfigKeyReachability` ×5, `WriteOnlyFields` ×1 — all Passed |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/ci/visual_gate.sh` | **=== VISUAL GATE: PASS ===**, goldens untouched (`git status` shows no golden) |
| `scripts/build-arm-noble.sh` | **exit 0** |

The gate was run even though the intent was behaviour-neutrality, precisely
because Leg A makes `display_settings.json` reach the renderer for the first
time: the neutrality argument is only as good as the file rewrite, and the gate
is what checks it. It passed with no golden moved.

### Deviations and surprises

1. **The Irrlicht archive in slot3 was stale for T411 and the first build failed
   on it, not on my changes.** `libIrrlicht.a` had an mtime of 08:05 — *after*
   T411's 07:08 merge — so the dispatch's mtime test said "no rebuild needed".
   It was wrong: slot3 was on `task/T412-census-class-a`, cut before T411, so the
   08:05 build compiled pre-T411 sources. Eight targets failed with `undefined
   reference to gles2SetAlphaCompanion(void*, void*)`. **An mtime later than the
   merge does not mean the sources were the merged ones** — check the symbol
   (`nm -C third_party/.../libIrrlicht.a | grep gles2SetAlphaCompanion`), not
   the timestamp. Rebuilt with `--irrlicht-only`; the second build was exit 0.
2. **B297 undercounted by seven and missed the worst family entirely.** The
   `display_settings.json` eight are the most damaging inert keys in the tree —
   their consumers were live the whole time — and T407's field census could not
   see them, because it looks for fields nothing READS and these fields were
   read; what was missing was the WRITE. A field-level census and a key-level
   census catch disjoint halves of the same defect. That is why Leg A added the
   key-level one rather than extending the field-level one.
3. **The new census is a floor, and the floor is visible.** Run against the base
   config files it catches 23 of the 29 inert keys; it misses the six whose names
   are short enough to collide with an unrelated literal somewhere in `src/`
   (`"grass"`, `"density"`, `"quality"`, `"renderDistance"` …). Those six are
   pinned instead by the per-key `DisplaySettings` parse cases. Stated in the
   test's header comment so no one reads a green as a proof of completeness.
4. **`numLayers` was invisible to the field census for a textbook reason** —
   `int numLayers = 1;` as a local at the top of both `render()` bodies, four
   lines from the member it shadows. The census matches by identifier, so the
   local's reads counted as the member's. It was clamped, range-checked and
   logged, and did nothing.
5. **Leg A deleted config that had a working duplicate, three times**:
   `environment_effects.json`'s `detailObjects`/`boids` vs `detail_objects.json`
   + `SimulationWorker`, and `detail_objects.json`'s `seasons` vs
   `SeasonalController`. In each case the live path was the *hardcoded* one and
   the *configurable* one was dead — the opposite of the usual direction, and a
   reason to distrust "there is a JSON file for it" as evidence that something is
   configurable.
6. **Board strikes and the Review are in the Leg C commit**, not spread across
   the three, because a strike must name the hash of the commit it closes and a
   commit cannot name itself. B297 and B299 carry `315619f5` and `4ac66d13`;
   B290's strike names T414 Leg C.
7. **`FileSizeCeiling` is green at this tip** — the root `CLAUDE.md` still says
   it "is RED at this tip" over B185's two `tools/hwcaps` files. That claim is
   stale (both are under the cap now); not fixed here because it is outside this
   unit's territory, but it should be struck.
