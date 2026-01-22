# Weather Effects Implementation Plan

## Overview

Implement comprehensive weather-based particle and lighting effects that respond to server weather events. The system should create immersive rain, snow, and storm effects with appropriate visual and environmental changes.

## Implementation Status

### Completed
- [x] **Phase 1: Rain Particles** - RainEmitter with wind influence, intensity scaling
- [x] **Phase 1: Rain Splashes** - RainSplashEmitter with ground-level splash effects
- [x] **Phase 2: Storm Sky Effects** - Ambient light darkening, fog color/density modification
- [x] **Phase 3: Lightning** - Random bolt generation with branching, flash effect
- [x] **Phase 4: Snow Particles** - SnowEmitter with swaying motion, wind influence
- [x] **Weather Packet Integration** - EverQuest::ZoneProcessWeather forwards to renderer
- [x] **Weather Effects Controller** - Central controller integrating all weather systems

### Planned Enhancements
- [x] **Phase 5: JSON Configuration** - External config file for all weather settings
- [x] **Phase 6: Quality Presets** - Low/medium/high/ultra particle budgets and effects
- [x] **Phase 7: Water Surface Ripples** - Rain impact ripples on water planes
- [x] **Phase 8: Storm Cloud Overlay** - Dynamic cloud texture during storms
- [x] **Phase 9: Snow Accumulation** - Heightmap-based surface snow buildup

## Implemented Files

### New Files Created
```
include/client/graphics/
    weather_effects_controller.h          - Central weather effects controller
    weather_config_loader.h               - JSON config loader singleton (Phase 5)
    weather_quality_preset.h              - Quality preset enum and manager (Phase 6)
    environment/emitters/
        rain_emitter.h                    - Rain drop particles
        rain_splash_emitter.h             - Ground splash particles
        snow_emitter.h                    - Snow flake particles
    environment/
        water_ripple_manager.h            - Water surface ripples (Phase 7)
        storm_cloud_layer.h               - Storm cloud overlay (Phase 8)
        accumulation_heightmap.h          - Snow accumulation grid (Phase 9)
        snow_accumulation_system.h        - Snow accumulation manager (Phase 9)

src/client/graphics/
    weather_effects_controller.cpp        - Weather controller implementation
    weather_config_loader.cpp             - Config loading with validation (Phase 5)
    weather_quality_preset.cpp            - Preset definitions and application (Phase 6)
    environment/emitters/
        rain_emitter.cpp                  - Rain particles with wind, intensity
        rain_splash_emitter.cpp           - Splash particles at ground level
        snow_emitter.cpp                  - Snow with swaying, wind drift
    environment/
        water_ripple_manager.cpp          - Ripple pool and rendering (Phase 7)
        storm_cloud_layer.cpp             - Storm cloud overlay implementation (Phase 8)
        accumulation_heightmap.cpp        - Snow accumulation grid implementation (Phase 9)
        snow_accumulation_system.cpp      - Snow accumulation manager implementation (Phase 9)

config/
    weather_effects.json                  - Weather configuration file (Phase 5)
```

### Modified Files
```
CMakeLists.txt                            - Added new source files
include/client/graphics/irrlicht_renderer.h - Added weatherEffects_ member
src/client/graphics/irrlicht_renderer.cpp   - Weather integration (lighting, fog)
src/client/eq.cpp                           - Weather packet forwarding
include/client/graphics/environment/particle_manager.h - External emitter support
src/client/graphics/environment/particle_manager.cpp   - External emitter rendering
```

## Weather Packet Format

```cpp
// From server OP_Weather packet (8 bytes)
// type: 0=rain off, 1=snow off, 2=snow on
// intensity: 1-10

// Decoding:
// - type 0 + intensity > 0 = Rain ON
// - type 0 + intensity 0 = Weather OFF (was rain)
// - type 1 = Snow OFF
// - type 2 = Snow ON (intensity from packet or default 5)
```

## Testing

### GM Commands
```
#weather 3 1 5    - Set rain intensity 5
#weather 3 2 7    - Set snow intensity 7
#weather 3 0 0    - Turn off weather
```

### Visual Verification
1. Rain: Drops fall from cylinder above player, drift with wind
2. Rain splashes: Small particles burst up from ground level
3. Snow: Flakes fall slowly with swaying motion, drift with wind
4. Storm: Sky darkens based on intensity, fog becomes grey-blue
5. Lightning: Random branching bolt appears during intense rain (intensity >= 3)
6. Flash: Brief scene brightening during lightning strike

## Architecture

```
IrrlichtRenderer
    |
    +-- weatherEffects_ (WeatherEffectsController)
    |       |
    |       +-- rainEmitter_       (RainEmitter, registered with ParticleManager)
    |       +-- rainSplashEmitter_ (RainSplashEmitter, registered with ParticleManager)
    |       +-- snowEmitter_       (SnowEmitter, registered with ParticleManager)
    |       +-- Lightning bolt generation and rendering
    |       +-- Sky darkening, fog modification
    |
    +-- particleManager_ (ParticleManager)
            |
            +-- externalEmitters_ (renders weather particles)
            +-- Internal biome emitters (dust, pollen, etc.)
```

## Performance Considerations

1. **Particle Budget**: Weather emitters share rendering with other particles
   - Rain: 500 max particles
   - Rain Splash: 200 max particles
   - Snow: 600 max particles

2. **Distance Culling**: Particles beyond budget.cullDistance are not rendered

3. **Indoor Zones**: Weather effects disabled in dungeons/caves

4. **Update Rate**: Emitters update per-frame via particle manager environment state

## Configuration (Hardcoded, Future Enhancement)

Current settings are hardcoded in `*Settings` structs:
- `RainSettings` - drop speed, spawn radius, wind influence
- `RainSplashSettings` - splash speed, gravity, lifetime
- `SnowSettings` - fall speed, sway amplitude/frequency
- `WeatherEffectsConfig` - storm darkening, lightning timing

Future: Load from `configs/weather_effects.json`

## Dependencies

- Existing: ParticleManager, SkyRenderer, WeatherSystem
- Particle textures: Uses existing atlas (Snowflake, WaterDroplet tiles)
- No new textures required for basic implementation

---

## Phase 5: JSON Configuration

### Overview
Externalize all weather settings to a JSON configuration file, allowing runtime tweaking without recompilation.

### New Files
```
config/weather_effects.json              - Main configuration file
include/client/graphics/weather_config_loader.h   - Config parser
src/client/graphics/weather_config_loader.cpp
```

### Configuration Structure
```json
{
  "rain": {
    "max_particles": 500,
    "spawn_radius": 30.0,
    "drop_speed_min": 8.0,
    "drop_speed_max": 12.0,
    "drop_length": 0.5,
    "wind_influence": 0.3,
    "color": [180, 200, 255, 200]
  },
  "rain_splash": {
    "max_particles": 200,
    "splash_speed": 2.0,
    "gravity": 5.0,
    "lifetime_min": 0.2,
    "lifetime_max": 0.4
  },
  "snow": {
    "max_particles": 600,
    "spawn_radius": 35.0,
    "fall_speed_min": 1.0,
    "fall_speed_max": 2.0,
    "sway_amplitude": 0.5,
    "sway_frequency": 1.5,
    "wind_influence": 0.5,
    "size_min": 0.08,
    "size_max": 0.15
  },
  "storm": {
    "ambient_darken_factor": 0.4,
    "fog_density_multiplier": 1.5,
    "fog_color": [100, 110, 130],
    "lightning_min_interval": 5.0,
    "lightning_max_interval": 15.0,
    "lightning_flash_duration": 0.15,
    "lightning_intensity_threshold": 3
  },
  "quality_preset": "high"
}
```

### Implementation Tasks
1. [x] Create `WeatherConfigLoader` class to parse JSON
2. [x] Set up reload callback for hot-reload (file watcher deferred to future)
3. [x] Migrate hardcoded `*Settings` structs to use loaded config
4. [x] Add `/reloadweather` command to reload config at runtime
5. [x] Validate config values with sensible min/max bounds (clampFloat/clampInt)

### Modified Files
```
weather_effects_controller.h      - Added reloadConfig(), applyConfigFromLoader()
weather_effects_controller.cpp    - Config loading integration, apply settings to emitters
CMakeLists.txt                    - Added weather_config_loader.cpp
src/client/eq.cpp                 - Added /reloadweather command, included header
```

---

## Phase 6: Quality Presets

### Overview
Provide predefined quality levels that automatically scale particle counts, effects complexity, and visual fidelity based on user preference or hardware capability.

### Preset Definitions

| Setting | Low | Medium | High | Ultra |
|---------|-----|--------|------|-------|
| Rain particles | 150 | 300 | 500 | 800 |
| Rain splash particles | 50 | 100 | 200 | 400 |
| Snow particles | 200 | 400 | 600 | 1000 |
| Lightning enabled | No | Yes | Yes | Yes |
| Lightning branches | - | 2 | 4 | 6 |
| Ripples enabled | No | No | Yes | Yes |
| Cloud overlay | No | No | Yes | Yes |
| Snow accumulation | No | No | No | Yes |
| Fog transitions | Instant | Fast | Smooth | Smooth |

### New Files
```
include/client/graphics/weather_quality_preset.h   - Preset definitions
src/client/graphics/weather_quality_preset.cpp     - Preset loading/application
```

### Implementation Tasks
1. [x] Define `WeatherQualityPreset` enum (Low, Medium, High, Ultra, Custom)
2. [x] Create `WeatherPresetValues` struct with all tunable values
3. [x] Create `WeatherQualityManager` singleton with preset definitions
4. [x] Add `/weatherquality <preset>` command (aliases: /wq, /weatherpreset)
5. [x] Integrate with WeatherConfigLoader to apply preset overrides
6. [x] Presets override particle counts unless Custom mode
7. [ ] Store quality setting in user preferences (deferred)
8. [ ] Auto-detect based on frame time (deferred - optional)

### Integration Points
```cpp
// WeatherQualityManager (singleton)
void setPreset(WeatherQualityPreset preset);
WeatherQualityPreset getCurrentPreset() const;
void applyToRainSettings(RainSettings& settings) const;
void applyToSnowSettings(SnowSettings& settings) const;

// WeatherConfigLoader
bool setQualityPreset(const std::string& presetName);
void setQualityPreset(WeatherQualityPreset preset);
void applyQualityPreset();  // Called after loading JSON
```

---

## Phase 7: Water Surface Ripples

### Overview
Generate animated ripple rings on water surfaces where raindrops impact, creating realistic water interaction effects.

### New Files
```
include/client/graphics/environment/water_ripple_manager.h
src/client/graphics/environment/water_ripple_manager.cpp
```

### Ripple Data Structure
```cpp
struct WaterRipple {
    irr::core::vector3df position;  // Impact point on water
    float age;                       // Time since spawn
    float maxAge;                    // Lifetime
    float radius;                    // Current ring radius
    float maxRadius;                 // Maximum expansion
    float alpha;                     // Fades out over time
};
```

### Visual Approach
1. **Decal-based ripples**: Project circular ring textures onto water mesh
2. **Animated expansion**: Ring radius grows from 0 to maxRadius over lifetime
3. **Alpha fade**: Opacity decreases as ripple expands and ages
4. **Multiple concurrent ripples**: Pool of 50-100 active ripples

### Implementation Tasks
1. [x] Create `WaterRippleManager` class with ripple pool (100 ripples default)
2. [x] Add procedural RippleRing texture to particle atlas (tile 10)
3. [x] Use SurfaceMap for water surface detection (SurfaceType::Water)
4. [x] Sample random water positions within spawn radius when rain is active
5. [x] Render ripples as horizontal alpha-blended quads on water surface
6. [x] Coordinate spawn rate with rain intensity (0-10 scale)
7. [x] Integrate with WeatherEffectsController and quality preset system
8. [x] Pass SurfaceMap from renderer to weather effects controller

### Water Surface Detection
Uses existing `SurfaceMap` class from detail system:
```cpp
// Query surface map for water type
Detail::SurfaceType surface = surfaceMap_->getSurfaceType(x, y);
if (surface == Detail::SurfaceType::Water) { ... }

// Get water height
float height = surfaceMap_->getHeight(x, y);
```

### Performance Considerations
- [x] Ripple pool size controlled by WaterRippleSettings::maxRipples
- [x] Ripples only spawn within spawnRadius of player
- [x] Respects quality preset ripplesEnabled flag (High/Ultra only)
- [x] Debug info shows active ripple count

---

## Phase 8: Storm Cloud Overlay

### Overview
Add a dynamic cloud layer texture that appears during storms, with animated movement and opacity based on storm intensity.

### New Files
```
include/client/graphics/environment/storm_cloud_layer.h
src/client/graphics/environment/storm_cloud_layer.cpp
```

### Visual Design
1. **Cloud dome**: Hemisphere mesh above player with cloud texture
2. **Animated UV scrolling**: Clouds drift based on wind direction
3. **Opacity control**: Clouds fade in/out with storm intensity
4. **Layered effect**: 2-3 cloud layers at different heights/speeds

### Cloud Layer Structure
```cpp
struct CloudLayer {
    irr::scene::IMeshSceneNode* mesh;
    float height;           // Distance above player
    float uvScrollSpeed;    // Texture animation speed
    float opacity;          // Current alpha (0-1)
    irr::core::vector2df uvOffset;  // Current texture offset
};
```

### Implementation Tasks
1. [x] Create hemisphere mesh for cloud dome
2. [x] Load or generate cloud texture (tiling, seamless)
3. [x] Implement UV scrolling shader or material animation
4. [x] Fade in clouds when storm intensity >= threshold
5. [x] Sync scroll direction with wind direction
6. [x] Procedural cloud texture generation using Perlin noise

### Cloud Texture Options
- **Option A**: Use existing skybox texture with alpha masking
- **Option B**: Generate perlin noise-based cloud texture [IMPLEMENTED]
- **Option C**: Load pre-made cloud atlas from assets

### Implementation Notes (Phase 8)
The storm cloud layer was implemented using Option B - procedural Perlin noise generation.

Key features:
- Hemisphere dome mesh that follows the player position
- Procedurally generated cloud texture using multi-octave Perlin noise
- UV scrolling animation synchronized with wind direction
- Opacity fades in/out based on storm intensity threshold (default: intensity >= 3)
- Integrated with quality preset system (enabled at High/Ultra)
- Disabled in indoor zones (dungeons, caves)

### Shader Considerations
```glsl
// Simple cloud layer fragment shader
uniform sampler2D cloudTexture;
uniform vec2 uvOffset;
uniform float opacity;

void main() {
    vec2 scrolledUV = texCoord + uvOffset;
    vec4 cloudColor = texture2D(cloudTexture, scrolledUV);
    gl_FragColor = vec4(cloudColor.rgb, cloudColor.a * opacity);
}
```

### Integration
- `WeatherEffectsController` manages cloud layer lifecycle
- Cloud layer rendered after skybox, before scene geometry
- Disabled in indoor zones (same check as other weather)

---

## Phase 9: Snow Accumulation

### Overview
Implement a heightmap-based snow accumulation system where snow particles build up on surfaces during snowfall, gradually covering terrain and objects.

### New Files
```
include/client/graphics/environment/snow_accumulation_system.h
src/client/graphics/environment/snow_accumulation_system.cpp
include/client/graphics/environment/accumulation_heightmap.h
src/client/graphics/environment/accumulation_heightmap.cpp
```

### Heightmap Structure
```cpp
class AccumulationHeightmap {
    // Grid covering visible area around player
    static constexpr int GRID_SIZE = 128;
    static constexpr float CELL_SIZE = 2.0f;  // 256m x 256m coverage

    float accumulation_[GRID_SIZE][GRID_SIZE];  // Snow depth per cell
    float terrain_height_[GRID_SIZE][GRID_SIZE]; // Base terrain height

    void addSnow(int x, int y, float amount);
    float getSnowDepth(float worldX, float worldY) const;
    void melt(float deltaTime, float meltRate);
};
```

### Visual Rendering Approaches

**Approach A: Texture Blending**
- Blend snow texture over terrain based on accumulation depth
- Requires terrain shader modification
- Smooth transitions, performance-friendly

**Approach B: Displacement Mesh**
- Generate mesh layer above terrain representing snow surface
- More accurate visual, higher performance cost
- Better for deep snow drifts

**Approach C: Decal System**
- Place snow decals on surfaces based on accumulation
- Works on all geometry including objects
- Moderate complexity and performance

### Implementation Tasks
1. [x] Create `AccumulationHeightmap` class with grid-based storage
2. [x] Create `SnowAccumulationSystem` class with decal-based rendering
3. [x] Accumulate snow depth based on snowfall rate and time
4. [x] Implement gradual melt when snow stops
5. [x] Render snow overlay using ground decals (Approach C)
6. [x] Add SnowPatch tile to particle atlas for decal texture
7. [x] Shelter detection via upward raycast (optional, uses RaycastMesh)
8. [x] Integrate with WeatherEffectsController and quality preset system

### Accumulation Rules
```cpp
// Per-frame accumulation update
void SnowAccumulationSystem::update(float dt) {
    if (snowIntensity_ > 0) {
        float rate = baseAccumulationRate_ * snowIntensity_ * dt;
        for (auto& cell : heightmap_) {
            if (!isSheltered(cell)) {
                cell.depth += rate;
                cell.depth = std::min(cell.depth, maxDepth_);
            }
        }
    } else {
        // Gradual melting
        float meltRate = baseMeltRate_ * dt;
        for (auto& cell : heightmap_) {
            cell.depth = std::max(0.0f, cell.depth - meltRate);
        }
    }
}
```

### Shelter Detection
- Raycast upward to detect overhead geometry
- Indoor areas have no accumulation
- Under trees/overhangs: reduced accumulation
- Steep slopes: snow slides off (angle threshold)

### Visual Thresholds
| Depth (units) | Visual Effect |
|---------------|---------------|
| 0.0 - 0.1 | Light dusting, ground texture visible |
| 0.1 - 0.3 | Partial coverage, texture blend |
| 0.3 - 0.5 | Full snow cover, footprint depth |
| 0.5+ | Deep snow, movement slowdown (optional) |

### Performance Considerations
- Heightmap updates at reduced rate (4 Hz, not every frame)
- Only render accumulation within view distance
- LOD: high detail near player, simplified far away
- Optional: save accumulation state per zone to disk

### Optional Enhancements
- Player footprints in snow (coordinate with footprint system)
- Snow sliding off steep slopes
- Object accumulation (snow on roofs, fences)
- Temperature-based melt rate variation

### Implementation Notes (Phase 9)
The snow accumulation system was implemented using Approach C - decal-based rendering.

Key features:
- 64x64 grid heightmap (256x256 world units coverage) that follows the player
- Uniform accumulation during snowfall based on intensity
- Gradual melting when snow stops
- Ground decals rendered as horizontal quads with SnowPatch texture
- Decal alpha and size scales with snow depth (dusting -> partial -> full coverage)
- Optional shelter detection via upward raycast (disabled when no RaycastMesh)
- Integrated with quality preset system (enabled only at Ultra quality)
- Disabled in indoor zones (dungeons, caves)

Configuration options (via `SnowAccumulationSettings`):
- `accumulationRate` / `meltRate` - Snow buildup and melting speed
- `maxDepth` - Maximum snow depth (default 0.5 units)
- `dustingThreshold` / `partialThreshold` / `fullThreshold` - Visual depth thresholds
- `maxDecals` / `viewDistance` - Rendering limits
- `shelterDetectionEnabled` / `shelterRayHeight` - Overhead cover detection
