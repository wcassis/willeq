# Environmental Particles Implementation Plan

## Overview

This plan covers the implementation of environmental particle effects and the Options window UI required to control them.

## Design Decisions (from user input)

| Decision | Value |
|----------|-------|
| Performance target | 30 FPS minimum |
| Effect scope | All zones (biome-appropriate) |
| User control | Quality slider + individual toggles |
| Sound | Visual only for now |
| Time variation | Time-of-day and weather (no seasons) |
| First priority | Environmental Particles |

---

## Phase 1: Options Window ✅ COMPLETE

**Status:** Completed on 2026-01-21

**Summary:**
- Created `OptionsWindow` class with Display tab UI
- Implemented tab system (Display, Audio, Controls, Game)
- Added Display settings with Environment Effects and Detail Objects sections
- Integrated with `WindowManager`
- Added `ToggleOptions` hotkey action (O key, available in all modes)
- Settings persistence via `config/display_settings.json`

**Files Created/Modified:**
- `include/client/graphics/ui/options_window.h` (NEW)
- `src/client/graphics/ui/options_window.cpp` (NEW)
- `include/client/graphics/ui/window_manager.h` (MODIFIED - added options window support)
- `src/client/graphics/ui/window_manager.cpp` (MODIFIED - added toggle methods)
- `include/client/input/hotkey_manager.h` (MODIFIED - added ToggleOptions action)
- `src/client/input/hotkey_manager.cpp` (MODIFIED - added action mapping and O key binding)
- `include/client/graphics/irrlicht_renderer.h` (MODIFIED - added optionsToggleRequested_)
- `src/client/graphics/irrlicht_renderer.cpp` (MODIFIED - handle options toggle)
- `CMakeLists.txt` (MODIFIED - added options_window.cpp)

### 1.1 OptionsWindow Base Class

Create a new UI window accessible in all modes (Player, Admin, Repair, etc.).

**File:** `include/client/graphics/ui/options_window.h`

```cpp
class OptionsWindow : public WindowBase {
public:
    OptionsWindow(IrrlichtDevice* device, WindowManager* manager);

    void show();
    void hide();
    bool isVisible() const;

    // Tab management
    void selectTab(const std::string& tabName);

private:
    // Tabs
    enum class Tab { Display, Audio, Controls, Game };
    Tab currentTab_ = Tab::Display;

    void renderDisplayTab();
    void renderAudioTab();      // Future
    void renderControlsTab();   // Future
    void renderGameTab();       // Future

    // Display settings
    void renderEnvironmentEffectsSection();
    void renderDetailObjectsSection();
    void renderGraphicsSection();
};
```

### 1.2 Display Tab - Environment Effects Section

**Controls:**
```
Environment Effects
├── Quality: [Off] [Low] [Medium] [High]
│
├── Individual Toggles:
│   ├── [ ] Atmospheric Particles (dust, pollen, mist)
│   ├── [ ] Ambient Creatures (birds, insects)
│   ├── [ ] Shoreline Waves
│   ├── [ ] Reactive Foliage
│   └── [ ] Rolling Objects (tumbleweeds)
│
└── Density: [────●────] 50%
```

### 1.3 Display Tab - Detail Objects Section

Move existing detail controls here:
```
Detail Objects (Grass, Plants, Debris)
├── Enable: [On/Off]
├── Density: [────●────] 100%
├── View Distance: [────●────] 150 units
│
└── Category Toggles:
    ├── [ ] Grass
    ├── [ ] Plants
    ├── [ ] Rocks
    └── [ ] Debris
```

### 1.4 Keybinding

- **O** or **Escape → Options**: Open Options window
- Available in all modes

### 1.5 Settings Persistence

**File:** `config/display_settings.json`

```json
{
  "environmentEffects": {
    "quality": "medium",
    "atmosphericParticles": true,
    "ambientCreatures": true,
    "shorelineWaves": true,
    "reactiveFoliage": true,
    "rollingObjects": true,
    "density": 0.5
  },
  "detailObjects": {
    "enabled": true,
    "density": 1.0,
    "viewDistance": 150,
    "grass": true,
    "plants": true,
    "rocks": true,
    "debris": true
  }
}
```

---

## Phase 2: Particle System Core ✅ COMPLETE

**Status:** Completed on 2026-01-21

**Summary:**
- Created `particle_types.h` with enums (ParticleType, ZoneBiome, WeatherType, EffectQuality) and structures (Particle, ParticleBudget, EnvironmentState)
- Implemented `ParticleEmitter` base class with particle pool, spawn rate control, wind response, and lifetime management
- Implemented `ParticleManager` for coordinating all emitters, handling zone transitions, and rendering particles as billboards
- Created `ZoneBiomeDetector` with comprehensive lookup table for all EverQuest zones
- Implemented procedural particle texture atlas generation (8 particle types: dust, firefly, mist, pollen, sand, leaf, snowflake, ember)
- Implemented billboard rendering with camera-facing quads, alpha blending, and atlas UV mapping

**Files Created:**
- `include/client/graphics/environment/particle_types.h` (enums and structures)
- `include/client/graphics/environment/particle_emitter.h` (base emitter class)
- `src/client/graphics/environment/particle_emitter.cpp` (emitter implementation)
- `include/client/graphics/environment/particle_manager.h` (particle manager)
- `src/client/graphics/environment/particle_manager.cpp` (manager + rendering)
- `include/client/graphics/environment/zone_biome.h` (biome detection)
- `src/client/graphics/environment/zone_biome.cpp` (zone lookup table)
- `CMakeLists.txt` (MODIFIED - added environment source files)

### 2.1 ParticleManager

Central manager for all particle effects.

**File:** `include/client/graphics/environment/particle_manager.h`

```cpp
class ParticleManager {
public:
    ParticleManager(irr::scene::ISceneManager* smgr);

    void update(float deltaTime, const glm::vec3& playerPos);
    void render();

    // Zone transitions
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome);
    void onZoneLeave();

    // Settings
    void setQuality(EffectQuality quality);
    void setDensity(float density);
    void setEnabled(ParticleType type, bool enabled);

    // Environment state
    void setTimeOfDay(float hour);  // 0-24
    void setWeather(WeatherType weather);
    void setWindDirection(const glm::vec3& dir);
    void setWindStrength(float strength);

private:
    std::vector<std::unique_ptr<ParticleEmitter>> emitters_;

    // Budget management
    static constexpr int MAX_PARTICLES = 500;
    int activeParticleCount_ = 0;
};
```

### 2.2 ParticleEmitter Base

**File:** `include/client/graphics/environment/particle_emitter.h`

```cpp
class ParticleEmitter {
public:
    virtual ~ParticleEmitter() = default;

    virtual void update(float deltaTime) = 0;
    virtual void render(irr::video::IVideoDriver* driver) = 0;

    virtual int getActiveCount() const = 0;
    virtual ParticleType getType() const = 0;

    void setEnabled(bool enabled);
    void setDensityMultiplier(float mult);

protected:
    bool enabled_ = true;
    float densityMult_ = 1.0f;
    glm::vec3 windDirection_;
    float windStrength_ = 0.0f;
};
```

### 2.3 Particle Structure

```cpp
struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    float lifetime;
    float maxLifetime;
    float size;
    float alpha;
    glm::vec4 color;
    int textureIndex;  // Atlas index
};
```

---

## Phase 3: Particle Types ✅ COMPLETE

**Status:** Completed on 2026-01-21

**Summary:**
- Implemented all five particle emitter types as subclasses of ParticleEmitter
- DustMoteEmitter: Floating dust particles for dungeons/interiors with Brownian motion
- PollenEmitter: Yellow-green particles in forests/plains during daytime with upward drift
- FireflyEmitter: Glowing night particles with pulsing glow and erratic movement
- MistEmitter: Large translucent mist particles for swamps and water areas
- SandDustEmitter: Fast-moving sand particles for deserts with strong wind response
- Integrated ParticleManager with IrrlichtRenderer (zone enter/leave, update, render)
- Connected to OptionsWindow settings via DisplaySettingsChangedCallback
- Settings control quality level, density, and enable/disable state

**Files Created:**
- `include/client/graphics/environment/emitters/dust_mote_emitter.h`
- `src/client/graphics/environment/emitters/dust_mote_emitter.cpp`
- `include/client/graphics/environment/emitters/pollen_emitter.h`
- `src/client/graphics/environment/emitters/pollen_emitter.cpp`
- `include/client/graphics/environment/emitters/firefly_emitter.h`
- `src/client/graphics/environment/emitters/firefly_emitter.cpp`
- `include/client/graphics/environment/emitters/mist_emitter.h`
- `src/client/graphics/environment/emitters/mist_emitter.cpp`
- `include/client/graphics/environment/emitters/sand_dust_emitter.h`
- `src/client/graphics/environment/emitters/sand_dust_emitter.cpp`

**Files Modified:**
- `src/client/graphics/environment/particle_manager.cpp` (setupEmittersForBiome implementation)
- `include/client/graphics/irrlicht_renderer.h` (added particleManager_ member)
- `src/client/graphics/irrlicht_renderer.cpp` (init, update, render, zone transitions, settings callback)
- `CMakeLists.txt` (added emitter source files)

### 3.1 Atmospheric Particles

**Types by Biome:**

| Biome | Day Particles | Night Particles | Weather Modifiers |
|-------|---------------|-----------------|-------------------|
| Forest | Pollen, floating seeds | Fireflies | +Leaves when windy |
| Swamp | Mist, marsh gas | Fireflies, will-o-wisps | +Thick mist in rain |
| Desert | Dust, sand | Dust | ++Dust when windy |
| Snow | Ice crystals, snow | Ice crystals | +Snow in storm |
| Plains | Pollen, dandelion seeds | Fireflies | +Dust when windy |
| Dungeon | Dust motes, cobwebs | Dust motes | - |
| Urban | Dust | - | +Leaves in autumn wind |

### 3.2 DustMoteEmitter

Floating dust particles visible in light, drifting slowly.

```cpp
class DustMoteEmitter : public ParticleEmitter {
    // Spawn in cylinder around player
    // Very slow movement, affected by wind
    // More visible near light sources
    // Density increases when windy

    static constexpr int BASE_COUNT = 50;
    static constexpr float SPAWN_RADIUS = 30.0f;
    static constexpr float PARTICLE_SIZE = 0.05f;
    static constexpr float LIFETIME = 10.0f;
};
```

### 3.3 PollenEmitter

Larger floating particles in forests/plains during day.

```cpp
class PollenEmitter : public ParticleEmitter {
    // Only active during daytime (6:00 - 20:00)
    // Gentle upward drift + wind
    // Spawn near grass/plant surfaces

    static constexpr int BASE_COUNT = 30;
    static constexpr float PARTICLE_SIZE = 0.1f;
};
```

### 3.4 FireflyEmitter

Glowing particles at night near water/forests.

```cpp
class FireflyEmitter : public ParticleEmitter {
    // Only active at night (20:00 - 6:00)
    // Erratic movement pattern (not just wind)
    // Glow pulse animation
    // Spawn near water, in forests

    static constexpr int BASE_COUNT = 20;
    static constexpr float PARTICLE_SIZE = 0.08f;
    float glowPhase_ = 0.0f;
};
```

### 3.5 MistEmitter

Low-lying fog particles in swamps, near water.

```cpp
class MistEmitter : public ParticleEmitter {
    // Spawn at ground level
    // Very slow horizontal drift
    // Density increases during rain/night
    // Larger, more transparent particles

    static constexpr int BASE_COUNT = 40;
    static constexpr float PARTICLE_SIZE = 2.0f;
    static constexpr float SPAWN_HEIGHT = 1.0f;
};
```

### 3.6 SandDustEmitter

Desert dust clouds, especially when windy.

```cpp
class SandDustEmitter : public ParticleEmitter {
    // Active in desert biomes
    // Density scales with wind strength
    // Low to ground, horizontal movement
    // Sandy color tint

    static constexpr int BASE_COUNT = 60;
};
```

---

## Phase 4: Rendering ✅ COMPLETE

**Status:** Completed as part of Phase 2 (procedural generation approach)

**Summary:**
- Used procedural texture atlas generation instead of external file
- Implemented in `ParticleManager::createDefaultAtlas()` function
- All 8 tile types rendered with appropriate shapes and colors
- Full billboard rendering with distance culling, rotation, and alpha blending

**Implementation Details:**

### 4.1 Particle Atlas (Procedural)

Instead of loading an external `particle_atlas.png`, the atlas is generated procedurally at runtime via `createDefaultAtlas()`. This provides:
- No external file dependencies
- Consistent rendering across all systems
- Easy to modify colors and shapes in code

**Generated Tiles (64x32, 4x2 layout of 16x16 tiles):**

| Tile | Content | Color | Shape |
|------|---------|-------|-------|
| 0 | Soft circle (dust) | White | Soft-edge circle |
| 1 | Star shape (firefly) | Yellow-green | 4-point star |
| 2 | Wispy cloud (mist) | White, very soft | Large soft circle |
| 3 | Spore shape (pollen) | Yellow-green | Medium-soft circle |
| 4 | Grain shape (sand) | Tan/brown | Crisp circle |
| 5 | Leaf shape | Green | Oval circle |
| 6 | Snowflake | White | 6-point star |
| 7 | Ember/spark | Orange-red | Glowing circle |

### 4.2 Billboard Rendering

Implemented in `ParticleManager::render()` and `ParticleManager::renderBillboard()`:

```cpp
void ParticleManager::render() {
    // Get camera for billboard orientation
    irr::scene::ICameraSceneNode* camera = smgr_->getActiveCamera();
    irr::core::vector3df cameraPos = camera->getAbsolutePosition();
    irr::core::vector3df cameraUp = camera->getUpVector();

    driver_->setMaterial(particleMaterial_);
    driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

    for (const auto& emitter : emitters_) {
        for (const Particle& p : emitter->getParticles()) {
            if (p.isAlive()) {
                // Distance culling
                float distSq = glm::dot(diff, diff);
                if (distSq > budget_.cullDistance * budget_.cullDistance) continue;

                renderBillboard(p, cameraPos, cameraUp);
            }
        }
    }
}
```

**Billboard Features:**
- Coordinate conversion: EQ (Z-up) → Irrlicht (Y-up)
- Camera-facing orientation with proper up/right vectors
- Per-particle rotation support
- Atlas UV coordinate calculation via `getAtlasUVs()`
- Alpha blending with particle color

---

## Phase 5: Integration ✅ COMPLETE

**Status:** Completed as part of Phase 3

**Summary:**
- ParticleManager fully integrated with IrrlichtRenderer
- Zone biome detection implemented via ZoneBiomeDetector singleton
- Settings connected to OptionsWindow via callback

### 5.1 Renderer Integration

**Files Modified:** `irrlicht_renderer.h`, `irrlicht_renderer.cpp`

Integration points:
- `init()` / `initLoadingScreen()`: Creates ParticleManager
- `loadZone()`: Calls `particleManager_->onZoneEnter()` with biome
- `unloadZone()`: Calls `particleManager_->onZoneLeave()`
- `processFrame()`: Updates player position, time of day, and calls update/render
- `setInventoryManager()`: Sets up OptionsWindow settings callback

### 5.2 Zone Biome Detection

**Files:** `zone_biome.h`, `zone_biome.cpp`

Implemented as `ZoneBiomeDetector` singleton with comprehensive zone lookup:

```cpp
ZoneBiome ZoneBiomeDetector::getBiome(const std::string& zoneName) const {
    auto it = zoneBiomes_.find(zoneName);
    if (it != zoneBiomes_.end()) {
        return it->second;
    }
    return ZoneBiome::Unknown;
}
```

**Zone mapping examples:**
- "qeynos" → Urban
- "gfaydark" → Forest
- "oasis" → Desert
- "innothule" → Swamp
- "everfrost" → Snow
- "befallen" → Dungeon

---

## File Structure

```
include/client/graphics/
├── ui/
│   └── options_window.h          # NEW
├── environment/
│   ├── particle_manager.h        # NEW
│   ├── particle_emitter.h        # NEW
│   ├── particle_types.h          # NEW
│   ├── emitters/
│   │   ├── dust_mote_emitter.h   # NEW
│   │   ├── pollen_emitter.h      # NEW
│   │   ├── firefly_emitter.h     # NEW
│   │   ├── mist_emitter.h        # NEW
│   │   └── sand_dust_emitter.h   # NEW
│   └── zone_biome.h              # NEW

src/client/graphics/
├── ui/
│   └── options_window.cpp        # NEW
├── environment/
│   ├── particle_manager.cpp      # NEW
│   ├── emitters/
│   │   ├── dust_mote_emitter.cpp # NEW
│   │   ├── pollen_emitter.cpp    # NEW
│   │   ├── firefly_emitter.cpp   # NEW
│   │   ├── mist_emitter.cpp      # NEW
│   │   └── sand_dust_emitter.cpp # NEW
│   └── zone_biome.cpp            # NEW

config/
└── display_settings.json         # NEW

data/textures/
└── particle_atlas.png            # NEW
```

---

## Implementation Order

### Step 1: Options Window Foundation
1. Create `OptionsWindow` class extending `WindowBase`
2. Add tab system (Display tab only for now)
3. Add keybinding (O key)
4. Register with `WindowManager`
5. Test opening/closing in all modes

### Step 2: Display Settings UI
1. Add Environment Effects section with placeholder controls
2. Add Detail Objects section (move existing /detail commands to UI)
3. Implement settings persistence to JSON
4. Load settings on startup

### Step 3: Particle System Core
1. Create `ParticleManager` class
2. Create `ParticleEmitter` base class
3. Create `Particle` structure
4. Implement basic billboard rendering
5. Create particle texture atlas

### Step 4: First Emitter - Dust Motes
1. Implement `DustMoteEmitter`
2. Test in a single zone
3. Add wind response
4. Tune spawn rates and movement

### Step 5: Additional Emitters
1. `PollenEmitter` (day only)
2. `FireflyEmitter` (night only)
3. `MistEmitter` (swamps, weather)
4. `SandDustEmitter` (desert, wind)

### Step 6: Zone Integration
1. Create zone → biome mapping
2. Auto-select emitters based on biome
3. Handle zone transitions
4. Test across multiple zones

### Step 7: Polish
1. Connect Options UI to particle system
2. Implement quality presets
3. Performance profiling
4. Adjust particle budgets

---

## Performance Considerations

- **Max 500 particles** active at once
- **Hybrid quality settings**:
  - Low: 25% density, 100 max particles
  - Medium: 50% density, 300 max particles
  - High: 100% density, 500 max particles
- **Distance culling**: Don't render particles beyond 50 units
- **LOD**: Reduce update frequency for distant particles
- **Batch rendering**: Single draw call for all particles of same type

---

## Testing Checklist

### Phase 1 (Options Window)
- [x] Options window opens with O key in Player mode
- [x] Options window opens with O key in Admin mode
- [x] Display tab shows environment effects controls
- [x] Quality selector (Off/Low/Medium/High) is rendered
- [x] Individual toggles for all effect types
- [x] Detail Objects section with density slider and toggles
- [ ] Settings persist across sessions (JSON save/load implemented, needs runtime testing)

### Phase 2 (Particle System Core)
- [x] ParticleManager class created with init/update/render methods
- [x] ParticleEmitter base class with particle pool and spawn logic
- [x] Particle structure with position, velocity, lifetime, color, alpha
- [x] Procedural particle texture atlas (8 particle types)
- [x] Billboard rendering with camera-facing quads
- [x] ZoneBiomeDetector with zone lookup table
- [x] Quality settings affect particle budget
- [x] Build succeeds with no errors

### Phase 3 (Particle Emitters - Complete)
- [x] DustMoteEmitter created with Brownian motion, wind response
- [x] PollenEmitter created with daytime-only activation, upward drift
- [x] FireflyEmitter created with night-only activation, pulsing glow
- [x] MistEmitter created for swamps/water, large translucent particles
- [x] SandDustEmitter created for deserts, strong wind response
- [x] ParticleManager creates biome-appropriate emitters
- [x] Integration with IrrlichtRenderer (init, update, render)
- [x] Zone enter/leave events update emitter sets
- [x] OptionsWindow settings connected to particle manager
- [x] Quality settings affect particle budget
- [x] Build succeeds with no errors

### Phase 4 (Rendering - Complete)
- [x] Procedural particle atlas generates all 8 tile types
- [x] createDefaultAtlas() generates 64x32 atlas with 4x2 tiles
- [x] Soft circle (dust), star (firefly), wispy cloud (mist) shapes
- [x] Spore (pollen), grain (sand), leaf, snowflake, ember shapes
- [x] Billboard rendering with camera-facing quads
- [x] Coordinate conversion (EQ Z-up → Irrlicht Y-up)
- [x] Per-particle rotation support
- [x] Atlas UV coordinate calculation
- [x] Distance culling for performance
- [x] Alpha blending with particle color

### Phase 5 (Integration - Complete)
- [x] ParticleManager created in init() and initLoadingScreen()
- [x] onZoneEnter() called in loadZone() with biome detection
- [x] onZoneLeave() called in unloadZone()
- [x] Player position/heading updated each frame
- [x] Time of day updated each frame
- [x] render() called in processFrame()
- [x] OptionsWindow callback updates particle settings

### Runtime Testing (Pending)
- [ ] Dust motes appear visually in dungeons/interiors
- [ ] Pollen appears in forests during day
- [ ] Fireflies appear at night near water/forests
- [ ] Mist appears in swamps
- [ ] Sand dust appears in deserts
- [ ] Wind affects particle movement appropriately
- [ ] Quality slider changes particle density
- [ ] Individual toggles enable/disable specific effects
- [ ] FPS stays above 30 with all effects on High
