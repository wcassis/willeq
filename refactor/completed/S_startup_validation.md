# Batch S — Startup Validation & Pre-allocation

## Goal

Enforce strict resource control at application startup. All mandatory settings are
validated, all buffers and caches are pre-allocated, and any failure is FATAL. After
startup completes, willeq must not allocate additional buffers during gameplay.

This batch is a prerequisite for Batch D (game state threading). It establishes:
- Single source of truth for configuration (preset defines everything)
- Fail-fast on invalid config, missing files, or insufficient resources
- Clean separation of game-state objects from renderer objects (doors)
- All resource allocation happens before any network I/O

## Principles

1. **No silent fallbacks.** Invalid config or missing files = FATAL + quit.
2. **No post-startup allocation.** All buffers, caches, and managers are created
   at startup with fixed capacities.
3. **Preset is the single source of truth.** Every resource limit comes from the
   preset. CLI flags override preset values but follow the same validation.
4. **Framebuffer budget is absolute.** If the requested resolution + display features
   exceed `framebufferMemoryBytes`, FATAL — don't silently clamp.
5. **All game files must exist.** Any file I/O failure for gameplay-critical data
   is FATAL with a clear error message (path, what was being loaded, why it failed).

## Process

Each unit follows: **Plan -> Implement -> Review -> Commit**

1. Write plan with numbered steps and acceptance criteria to unit file
2. Implement, re-reading the plan file during work
3. Review: re-read plan, check each step, note deviations in review section
4. Commit only after review passes
5. Update progress table in `progress.md`

---

## S01: Mandatory Config Validation

### Context

Connection credentials (`host`, `user`, `pass`, `server`, `character`) and
`eqClientPath` are currently optional with empty-string defaults. If missing,
the application silently fails deep in the connection or loading pipeline with
no clear error. The `constrainedPreset` defaults silently to OrangePi if empty.

### Steps

1. In `Application::initialize()` (application.cpp:60), after config is stored
   (line 61), add a validation block before any object creation:
   - `host`: must not be empty. FATAL if empty.
   - `port`: must be 1-65535. FATAL if out of range.
   - `user`: must not be empty. FATAL if empty.
   - `pass`: must not be empty. FATAL if empty.
   - `server`: must not be empty. FATAL if empty.
   - `character`: must not be empty. FATAL if empty.
   - `eqClientPath` (when `graphicsEnabled`): must not be empty. FATAL if empty.
     Must be a valid directory (check with `stat()` or equivalent). FATAL if not
     a directory.
   - `constrainedPreset`: if empty, log that defaulting to "orangepi" (existing
     behavior is fine, just make it explicit in the log).

2. Remove the silent fallback to headless mode at application.cpp:304-310.
   If graphics are enabled and eqClientPath is set but graphics init fails,
   that's FATAL — not a silent downgrade to headless.

3. Remove the `if (!config.eqClientPath.empty())` guard at application.cpp:191
   that wraps the entire graphics init block. eqClientPath was already validated
   in step 1, so the guard is redundant.

### Acceptance Criteria

- Launching with empty `host`, `user`, `pass`, `server`, or `character` prints
  a clear FATAL error and exits immediately
- Launching with a nonexistent `eqClientPath` prints a clear FATAL error and exits
- Launching with valid config proceeds normally (no behavioral change)
- No silent fallback to headless mode

---

## S02: Preset Value Validation

### Context

The constrained preset has ~44 fields. All are loaded from JSON with zero
validation. Invalid values (zero, negative, non-power-of-2 texture dimensions)
cause undefined behavior deep in the rendering pipeline.

### Steps

1. Add a `validate()` method to `ConstrainedRendererConfig` that returns
   `bool` and populates a `std::string& errors` with all validation failures.
   Check all fields in a single pass — report ALL errors, not just the first.

2. Validation rules:

   **Framebuffer (FATAL if invalid):**
   - `colorDepthBits`: must be 16 or 32
   - `framebufferMemoryBytes`: must be > 0

   **Texture memory (FATAL if invalid):**
   - `textureMemoryBytes`: must be > 0
   - `maxTextureDimension`: must be power of 2, range [64, 2048]

   **Rendering (FATAL if invalid):**
   - `clipDistance`: must be > 0
   - `entityRenderDistance`: must be > 0
   - `maxVisibleEntities`: must be > 0
   - `maxPolygonsPerFrame`: must be > 0
   - `fogStartRatio`: must be in [0, 1]
   - `fogEndRatio`: must be in [0, 1], must be > `fogStartRatio`
   - `targetFps`: must be > 0
   - `backgroundThreadCount`: must be >= 1

   **PVS/occlusion (FATAL if invalid):**
   - `entityPrepMaxPvsDepth`, `terrainPrepMaxPvsDepth`, `objectPrepMaxPvsDepth`:
     must be in [0, 255]
   - `pvsNeighborhoodHops`: must be >= 0
   - `antiAliasLevel`: must be 0 or power of 2
   - `anisotropicFilterLevel`: must be 0 or power of 2

   **Derived limits (FATAL if invalid):**
   - `totalMemoryBudgetBytes`: if > 0, must be >= `textureMemoryBytes + framebufferMemoryBytes`
   - `meshMemoryBytes`: if `totalMemoryBudgetBytes` > 0, must be > 0

3. Add framebuffer vs resolution validation. After preset is built and resolution
   is known (application.cpp, after line 246):
   - Compute actual framebuffer usage:
     `width * height * bytesPerPixel * bufferCount` where bufferCount = 2
     (front+back), bytesPerPixel includes depth and optionally stencil
   - If actual usage > `framebufferMemoryBytes`: FATAL with a message showing
     the requested resolution, the computed usage, and the budget.
   - Do NOT clamp resolution. The user must fix their preset or resolution.

4. Call `validate()` in `Application::initialize()` immediately after building
   the constrained config (after application.cpp:246). FATAL if validation fails.

### Acceptance Criteria

- `validate()` catches all invalid field combinations in a single pass
- Launching with `maxTextureDimension: 100` (not power of 2) prints FATAL
- Launching with 1920x1080 resolution and 10MB framebuffer budget prints FATAL
  showing the math
- Launching with valid preset proceeds normally
- All validation errors include the field name, the invalid value, and what's expected

---

## S03: Global File Validation

### Context

Several global files are required for gameplay but are loaded with silent
fallbacks or warnings. Missing files cause broken rendering with no clear
diagnostic path. The user has confirmed that all client files exist — any
file load failure is a code bug and should be loud.

### Steps

1. After `eqClientPath` is validated (S01) and before graphics init, validate
   that required global files exist:
   - `config/constrained_presets.json` — FATAL if missing (preset JSON overrides)
   - `config/race_models.json` — FATAL if missing (race/gender model mappings)
   - `data/item_models.json` — FATAL if missing (equipment model mappings)
   - `{eqClientPath}/eqstr_us.txt` — FATAL if missing (string database)
   - `{eqClientPath}/dbstr_us.txt` — FATAL if missing (string database)
   - Spell DB files in `{eqClientPath}` — FATAL if the expected spell file
     (spells_us.txt or equivalent) is missing

2. Implement as a simple validation function that checks `stat()` for each
   file and collects all missing files before reporting. Report ALL missing
   files in one FATAL message, not one at a time.

3. Remove the multi-path fallback logic for race_models.json
   (irrlicht_renderer.cpp:507-511 tries 3 paths) and item_models.json
   (irrlicht_renderer.cpp:515-519 tries 2 paths). These files have known
   locations. If they're not there, FATAL — don't hunt.

4. Remove silent fallback in `loadDisplaySettingsFromFile()`
   (irrlicht_renderer.cpp:502). If display_settings.json is missing, use
   defaults but log INFO (not silent). This file is optional (display
   preferences, not gameplay-critical).

### Acceptance Criteria

- Launching with missing `race_models.json` prints FATAL listing the file
- Launching with missing EQ client files prints FATAL listing all missing files
- All error messages include the full expected path
- Optional files (display_settings.json) log INFO when missing, not FATAL
- No multi-path hunting — each file has exactly one expected location

---

## S04: Pre-allocate Game State Managers

### Context

Several game-state managers are allocated lazily during zone connection or
gameplay. `inventory_manager` and `buff_manager` are allocated in
`ConnectToZone()` (eq.cpp:1825, 1842). Spell DB is loaded during zone connect
(eq.cpp:1833). `command_registry` is allocated on first chat input
(eq.cpp:7462). These should all exist at startup.

### Steps

1. Move `m_inventory_manager` creation from `ConnectToZone()` (eq.cpp:1825) to
   `EverQuest` constructor (eq.cpp:936). Remove the `if (!m_inventory_manager)`
   guard in ConnectToZone. Remove the duplicate creation in InitGraphics
   (eq.cpp:18416).

2. Move spell database initialization (`m_spell_manager->initialize()`) from
   `ConnectToZone()` (eq.cpp:1833) to `Application::initialize()`, after
   `eqClientPath` is validated (S01) and global files are validated (S03).
   FATAL if spell DB load fails (file was already validated to exist in S03).
   Remove the `if (!m_spell_manager->isInitialized())` guard in ConnectToZone.
   Remove the duplicate initialization in InitGraphics (eq.cpp:18447).

3. Move `m_buff_manager` creation from `ConnectToZone()` (eq.cpp:1842) to
   immediately after spell DB initialization in `Application::initialize()`.
   Depends on spell DB being loaded. Remove the `if (!m_buff_manager)` guard
   in ConnectToZone. Remove the duplicate creation in InitGraphics
   (eq.cpp:18456).

4. Move `m_command_registry` creation from `ProcessChatInput()` (eq.cpp:7462)
   to `EverQuest` constructor. Remove the lazy-init guard.

5. Move `m_spell_effects` and `m_spell_type_processor` creation from
   InitGraphics (eq.cpp:18496, 18503) to `Application::initialize()`, after
   spell DB and buff manager are ready.

6. Verify: after these changes, `ConnectToZone()` creates only the zone
   connection manager (transport + DaybreakConnectionManager) — no other
   allocations.

### Acceptance Criteria

- All 6 managers exist immediately after `Application::initialize()` returns
- `ConnectToZone()` does not allocate any managers
- `InitGraphics()` does not allocate any managers
- No lazy-init guards remain for these managers
- Spell DB load failure during startup is FATAL
- Headless mode (no graphics) still creates all game-state managers
- All existing tests pass

---

## S05: Pre-allocate Renderer-Lifetime Subsystems

### Context

Several renderer subsystems are created lazily during zone loading:
`entityRenderer_` (irrlicht_renderer.cpp:2767 in createEntityRenderer()),
`skyRenderer_` (4739), `detailManager_` (4938), `simulationWorker_` (5346),
`renderPassTimer_` (3614), `animatedTextureManager_` (5710). These are
renderer-lifetime objects that survive zone changes. They should be created
in `initLoadingScreen()` so the loading thread never allocates.

### Steps

1. Move `entityRenderer_` creation to `initLoadingScreen()`, after the
   constrained texture cache is created (after line 608). Remove the
   `if (!entityRenderer_)` guard in `createEntityRenderer()` — it should
   already exist. Remove the `createEntityRenderer()` calls from
   `setupInstantScene()` (line 2803) and `loadZoneSequential()` step 5.
   Keep `createEntityRenderer()` as a private setup method called once
   from `initLoadingScreen()`.

2. Move `skyRenderer_` creation to `initLoadingScreen()`. Currently created
   conditionally in step 10 (line 4739). Create unconditionally at startup.
   The `skyRendering` preset flag controls whether sky *renders*, not whether
   the object exists.

3. Move `detailManager_` creation to `initLoadingScreen()`. Currently created
   conditionally in step 11 (line 4938) based on display settings. Create
   unconditionally. The `detailObjectsEnabled` flag controls whether detail
   *renders*.

4. Move `simulationWorker_` creation to `initLoadingScreen()`. Currently
   created in step 12b (line 5346). Create at startup, configure per-zone
   data later via existing `installZoneData()` pattern.

5. Move `renderPassTimer_` creation to `initLoadingScreen()`. Currently
   raw `new` at line 3614. Create as `std::unique_ptr` at startup.

6. Move `animatedTextureManager_` creation to `initLoadingScreen()`. Currently
   created in step 11 fixup (line 5710).

7. Audit `loadZoneSequential()` for any remaining `make_unique` or `new` calls
   that create renderer-lifetime objects. All should be in `initLoadingScreen()`.
   Zone-scoped temporary data (`PendingZoneComputations`, per-zone atlas, etc.)
   is acceptable.

### Acceptance Criteria

- All 6 subsystems exist after `initLoadingScreen()` returns
- `loadZoneSequential()` does not create any renderer-lifetime objects
- Zone re-load (zoning between zones) works — subsystems are reused, not recreated
- Loading thread never calls `make_unique` for renderer subsystems
- No `if (!subsystem_)` lazy-init guards remain in `loadZoneSequential()`
- All existing functionality preserved

---

## S06: Door State Separation

### Context

`DoorManager` (include/client/graphics/door_manager.h) mixes game state and
rendering. It stores door positions, open/closed state, and opentype alongside
scene nodes, meshes, and animation state. Doors are game-state objects — they
restrict passage, affect pathing/LOS, and are server-authoritative. The existing
`EverQuest::m_doors` (std::map<uint8_t, Door>) already holds the authoritative
game state. DoorManager duplicates this into `DoorVisual` structs.

The door game state needs to be accessible to future pathing, LOS, and collision
systems independent of any renderer. "Doors" include static placeables, dynamic
doors, elevators, triggered platforms, revolving doors, and sliding doors.

### Steps

1. Create `include/client/door_state.h` with a `DoorState` struct (game-state
   only, no renderer dependencies):
   ```
   struct DoorState {
       uint8_t doorId;
       std::string name;          // Model name (matches zone S3D object)
       float x, y, z;             // Position (EQ coordinates)
       float closedHeading;       // Heading when closed (degrees)
       float openHeading;         // Heading when open (degrees)
       uint16_t size;             // Scale (100 = normal)
       uint8_t opentype;          // Behavior type
       bool isOpen;               // Current state
       bool invertState;          // Spawn state inversion
       uint32_t doorParam;        // Lock type / key item ID
   };
   ```

2. Create `include/client/door_state_manager.h` and
   `src/client/door_state_manager.cpp` with a `DoorStateManager` class:
   - `void addDoor(const DoorState& door)` — called from SpawnDoor packet handler
   - `void setDoorState(uint8_t doorId, bool open)` — called from MoveDoor handler
   - `const DoorState* getDoor(uint8_t doorId) const` — lookup by ID
   - `const std::map<uint8_t, DoorState>& getAllDoors() const` — full read access
   - `void clear()` — zone change cleanup
   - `size_t getDoorCount() const`
   - `std::vector<const DoorState*> getDoorsInRadius(float x, float y, float z, float radius) const` — for pathing/collision queries
   - Pre-allocate internal storage with a fixed capacity matching the game maximum
     (255 doors, since door_id is uint8_t)

3. Replace `EverQuest::m_doors` (std::map<uint8_t, Door>) with
   `DoorStateManager`. The existing `Door` struct (eq.h:615-625) is replaced
   by `DoorState`. Update all packet handlers that read/write `m_doors`:
   - `ZoneProcessSpawnDoor()` (eq.cpp:5585-5650): use `doorStateManager.addDoor()`
   - `ZoneProcessMoveDoor()` (eq.cpp:12389-12446): use `doorStateManager.setDoorState()`
   - `ZoneProcessClickDoor()` and door interaction logic: use `doorStateManager.getDoor()`

4. Create `DoorStateManager` in `EverQuest` constructor (pre-allocated at startup,
   per S04 pattern). It's a game-state object, not a renderer object.

5. Modify `DoorManager` (renderer side) to take a `const DoorStateManager&`
   reference for initial door registration during zone loading. `DoorVisual`
   retains only rendering-specific fields:
   - Scene nodes (`pivotNode`, `sceneNode`)
   - Animation state (`animProgress`, `isAnimating`, `isSpinning`, `spinAngle`)
   - Mesh state (`meshBuilt`, `usePlaceholder`, mesh cache)
   - Culling state (`bspRegion`, `inSceneGraph`, `boundingBox`)
   - Remove duplicated game-state fields (`x`, `y`, `z`, `opentype`, `size`)
     from `DoorVisual` — read from `DoorStateManager` when needed

6. Update the renderer's `registerDoor()` / `createDoor()` to accept a
   `const DoorState&` reference instead of individual parameters. The
   `LoadZoneGraphicsOnLoadingThread()` function (eq.cpp:19191) iterates
   `doorStateManager.getAllDoors()` instead of `m_doors`.

7. Update door interaction methods (`getDoorAtScreenPos()`, `getNearestDoor()`)
   to read position data from `DoorStateManager` rather than `DoorVisual`.

### Acceptance Criteria

- `DoorStateManager` is a game-state class with no renderer includes
- `DoorStateManager` is created in `EverQuest` constructor with pre-allocated storage
- `DoorVisual` contains only rendering/animation fields
- Door position, state, and type queries work without a renderer (headless mode)
- All door interactions (click, U-key, MoveDoor packets) work as before
- Door animation (open/close/spin) works as before
- Pathing/LOS queries can access door positions via `DoorStateManager`
  without touching the renderer
- `m_doors` map in EverQuest is replaced by `DoorStateManager`

---

## S07: File I/O Failure Enforcement

### Context

Many file I/O operations during zone loading log warnings and continue with
degraded rendering. Per project requirements, all client files must exist.
Any file load failure is a code bug and must be FATAL with enough context
to diagnose the issue.

### Steps

1. `S3DLoader::loadZone()` (irrlicht_renderer.cpp:3084): already logs ERROR
   and returns false. Change the caller to FATAL instead of returning silently.
   Include the full path in the error message.

2. Atlas file loading (irrlicht_renderer.cpp:3409, 3416):
   `TextureAtlas::preloadFromFile()` returns an invalid preload on failure.
   If `enableTextureAtlas` is true in the preset and the atlas file is missing,
   FATAL. If `enableTextureAtlas` is false, skip atlas loading entirely (don't
   attempt the read).

3. Sky file loading (irrlicht_renderer.cpp:4601):
   `skyLoader->load(eqPath)` for sky.s3d. If `skyRendering` is true in the
   preset and sky.s3d is missing, FATAL. If `skyRendering` is false, skip.

4. Race model loading (`RaceModelLoader::preloadModelData()`): when loading
   a race S3D archive, FATAL if the file doesn't exist. Include the race code,
   expected S3D filename, and full path.

5. Equipment model loading (`EquipmentModelLoader::extractEquipmentModelOffThread()`):
   FATAL if the gequip S3D archive doesn't exist.

6. Texture decode operations (DDS/BMP decode in step 8c/8d of the sequential
   loader): FATAL if a texture referenced by the model's WLD data cannot be
   found in the S3D archive. Include the texture name and archive name.

7. Zone weather config, zone indoor region map: these are optional data files
   (not all zones have weather or indoor regions). Log INFO if missing, not
   FATAL. Document which files are optional vs mandatory.

8. Create a consistent error format for all FATAL file I/O errors:
   ```
   [FATAL] [IO] Failed to load <what>: <filepath> (<reason>)
   ```
   Where `<what>` is human-readable ("zone S3D archive", "race model archive",
   "texture atlas", etc.), `<filepath>` is the full absolute path attempted,
   and `<reason>` is the OS error or parse error.

### Acceptance Criteria

- Missing zone S3D file → FATAL with path
- Missing atlas file (when atlas enabled) → FATAL with path
- Missing race model S3D → FATAL with race code and path
- Missing equipment archive → FATAL with path
- Missing texture in archive → FATAL with texture name and archive name
- Optional files (weather config, indoor regions) → INFO log, no FATAL
- All FATAL messages follow the consistent format
- All FATAL messages include the full file path that was attempted
- Normal operation (all files present) is unchanged

---

## Execution Order

```
S01 (Mandatory config validation)        — standalone, first
    ↓
S02 (Preset value validation)            — depends on S01 (config validated first)
    ↓
S03 (Global file validation)             — depends on S01 (eqClientPath validated)
    ↓
S04 (Pre-allocate game state managers)   — depends on S03 (spell DB file validated)
    ↓
S05 (Pre-allocate renderer subsystems)   — depends on S04 (game state managers exist)
    ↓
S06 (Door state separation)              — depends on S04 (game state manager pattern)
    ↓
S07 (File I/O failure enforcement)       — depends on S05 (renderer subsystems exist)
```

S01-S03 are pure validation (no structural changes). S04-S06 are structural
(move allocations, create new classes). S07 is enforcement (change error handling).

## Relationship to Other Batches

- **Batch L (Loading Thread)**: Batch S strengthens Batch L's assumptions. After S05,
  the loading thread never allocates renderer subsystems — they already exist. The
  loading thread becomes purely a zone-data consumer, not an object creator.

- **Batch D (Game State Threading)**: S04 creates game-state managers at startup,
  independent of any renderer. S06 separates door state from rendering. Both directly
  enable the bridge architecture where game state publishes events and renderers
  consume them. `DoorStateManager` becomes a game-state object that publishes
  `DoorStateChanged` events through the bridge.

- **Entity Texture Atlas** (docs/future/entity_texture_atlas.md): S05 pre-creates
  `entityRenderer_` at startup. The entity atlas work (Chunk 3-5) modifies how
  `entityRenderer_` loads textures but doesn't change when it's created.

- **Custom Renderer** (docs/future/custom_renderer.md): S05 and S06 reduce coupling
  between game state and Irrlicht. When Irrlicht is removed, `DoorStateManager`
  is unchanged — only `DoorManager` (renderer side) is replaced.

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| FATAL on missing files blocks development/testing | Provide clear error messages with exact file paths. Developer knows which file to fix. |
| Pre-allocating all subsystems increases startup memory | Subsystems are lightweight shells until zone data arrives. The memory cost is the objects themselves (~KB), not their data (~MB). Zone data is still loaded per-zone. |
| DoorStateManager introduces a new abstraction layer | The abstraction is thin (struct + map + accessors). DoorState is simpler than the existing Door struct. No complex logic moves — just data ownership. |
| Removing resolution clamping breaks configs that relied on it | Users must fix their preset to match their resolution. The FATAL message shows exactly what's wrong and what the budget allows. |
| Spell DB load at startup adds to startup time | Spell DB load is ~50-100ms. Currently happens during zone connect anyway. Moving it to startup is a negligible time shift. |
