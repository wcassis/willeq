# Dynamic Tree Movement Implementation Plan

## Overview

Implement wind-driven animation for trees where leaves and upper branches sway realistically while trunk bases remain fixed. Movement intensity scales with weather conditions (calm, rain, storm).

## Implementation Status

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Configuration System | **COMPLETE** |
| 2 | Tree Identification System | **COMPLETE** |
| 3 | Tree Wind Controller | **COMPLETE** |
| 4 | Animated Tree Mesh System | **COMPLETE** |
| 5 | Weather Integration | **COMPLETE** |
| 6 | Integration with Renderer | **COMPLETE** |
| 7 | Performance Optimizations | Pending |

## Current System Analysis

### Existing Wind System (Detail Objects)
- `WindController` in `include/client/graphics/detail/wind_controller.h`
- Uses sine waves with configurable frequency, strength, and turbulence
- Applied per-vertex based on `windInfluence` factor (0 at base, 1 at top)
- Called each frame in `DetailChunk::applyWind()`

### Tree Rendering
- Trees are loaded from S3D zone files as static meshes
- Rendered via `ZoneGeometry` in the Irrlicht scene graph
- Currently no vertex animation support for zone geometry

## Implementation Plan

### Phase 1: Configuration System [COMPLETE]

**Implemented Files:**
- `include/client/graphics/tree_wind_config.h` - TreeWindConfig struct, WeatherType enum, TreeWindConfigLoader class
- `src/client/graphics/tree_wind_config.cpp` - JSON loading/saving implementation
- `data/config/tree_wind.json` - Default configuration file
- Updated `CMakeLists.txt` to include new source file

**File: `include/client/graphics/tree_wind_config.h`**
```cpp
struct TreeWindConfig {
    // Base wind parameters
    float baseStrength = 0.3f;        // Base sway amplitude (units)
    float baseFrequency = 0.4f;       // Oscillations per second
    float gustStrength = 0.5f;        // Additional gust amplitude
    float gustFrequency = 0.1f;       // Gust cycle frequency
    float turbulence = 0.2f;          // Random variation factor

    // Height-based influence curve
    float influenceStartHeight = 0.3f; // Normalized height where sway begins (0-1)
    float influenceExponent = 2.0f;    // Power curve for influence falloff

    // Weather multipliers
    float calmMultiplier = 0.5f;
    float normalMultiplier = 1.0f;
    float rainMultiplier = 1.5f;
    float stormMultiplier = 2.5f;

    // Performance
    float updateDistance = 300.0f;    // Max distance to animate trees
    float lodDistance = 150.0f;       // Distance for reduced animation quality
    bool enabled = true;
};
```

**File: `data/config/tree_wind.json`** (default config)
```json
{
    "tree_wind": {
        "base_strength": 0.3,
        "base_frequency": 0.4,
        "gust_strength": 0.5,
        "gust_frequency": 0.1,
        "turbulence": 0.2,
        "influence_start_height": 0.3,
        "influence_exponent": 2.0,
        "weather_multipliers": {
            "calm": 0.5,
            "normal": 1.0,
            "rain": 1.5,
            "storm": 2.5
        },
        "update_distance": 300.0,
        "lod_distance": 150.0,
        "enabled": true
    }
}
```

### Phase 2: Tree Identification System [COMPLETE]

**Implemented Files:**
- `include/client/graphics/tree_identifier.h` - TreeIdentifier class declaration
- `src/client/graphics/tree_identifier.cpp` - Pattern matching implementation
- Updated `CMakeLists.txt` to include new source file

**Implementation Approach:** Option A (Texture Name Matching) + Option C (Zone Configuration)

Trees are identified using a combination of:
1. **Default patterns** for common tree/leaf naming conventions (tree, pine, oak, palm, leaf, foliage, etc.)
2. **Zone-specific overrides** loaded from `data/config/zones/<zonename>/tree_overrides.json`
3. **Explicit exclusion list** for false positives

**File: `include/client/graphics/tree_identifier.h`**
```cpp
class TreeIdentifier {
public:
    TreeIdentifier();

    bool isTreeMesh(const std::string& meshName, const std::string& textureName = "") const;
    bool isLeafMaterial(const std::string& materialName) const;
    bool loadZoneOverrides(const std::string& zoneName);

    void addTreePattern(const std::string& pattern);
    void addLeafPattern(const std::string& pattern);
    void addZoneTreeMesh(const std::string& meshName);
    void addExcludedMesh(const std::string& meshName);
    void clearZoneOverrides();

    struct Stats {
        size_t treePatternCount = 0;
        size_t leafPatternCount = 0;
        size_t zoneTreeMeshCount = 0;
        size_t excludedMeshCount = 0;
    };
    Stats getStats() const;

private:
    void initDefaultPatterns();
    bool matchesPattern(const std::string& name, const std::string& pattern) const;
    bool matchesAnyPattern(const std::string& name, const std::vector<std::string>& patterns) const;
    static std::string toLower(const std::string& str);

    std::vector<std::string> treePatterns_;
    std::vector<std::string> leafPatterns_;
    std::unordered_set<std::string> zoneTreeMeshes_;
    std::unordered_set<std::string> excludedMeshes_;
};
```

**Zone Override File Format:** `data/config/zones/<zonename>/tree_overrides.json`
```json
{
    "tree_meshes": ["specific_tree_mesh_name"],
    "excluded_meshes": ["false_positive_mesh"],
    "tree_patterns": ["*custom_tree*"],
    "leaf_patterns": ["*custom_leaf*"]
}
```

**Default Tree Patterns:** `*tree*`, `*pine*`, `*oak*`, `*palm*`, `*willow*`, `*birch*`, `*maple*`, `*cedar*`, `*fir*`, `*spruce*`, etc.

**Default Leaf Patterns:** `*leaf*`, `*leaves*`, `*foliage*`, `*frond*`, `*needle*`, `*branch*`, `*canopy*`, etc.

### Phase 3: Tree Wind Controller [COMPLETE]

**Implemented Files:**
- `include/client/graphics/tree_wind_controller.h` - TreeWindController class declaration
- `src/client/graphics/tree_wind_controller.cpp` - Wind calculation implementation
- Updated `CMakeLists.txt` to include new source file

**Key Features:**
- **Config loading** with fallback chain (cmdline -> zone-specific -> default -> built-in)
- **Smooth weather transitions** - wind intensity changes gradually when weather changes
- **Wind direction support** - configurable wind direction in XZ plane
- **Height-based influence** - base fixed, top moves most, power curve falloff
- **Multi-layer wind** - primary sway + gusts + turbulence for natural movement
- **Per-tree variation** - meshSeed provides unique phase offset per tree

**File: `include/client/graphics/tree_wind_controller.h`**
```cpp
class TreeWindController {
public:
    TreeWindController();

    bool loadConfig(const std::string& configPath = "", const std::string& zoneName = "");
    void setWeather(WeatherType weather);
    WeatherType getWeather() const;
    void update(float deltaTime);

    irr::core::vector3df getDisplacement(
        const irr::core::vector3df& worldPos,
        float normalizedHeight,  // 0 = base, 1 = top
        float meshSeed           // For variation between trees
    ) const;

    float getCurrentStrength() const;
    bool isEnabled() const;
    void setEnabled(bool enabled);
    const TreeWindConfig& getConfig() const;
    void setConfig(const TreeWindConfig& config);
    void setWindDirection(const irr::core::vector2df& direction);
    irr::core::vector2df getWindDirection() const;

private:
    float calculateInfluence(float normalizedHeight) const;
    void updateWeatherTransition(float deltaTime);

    TreeWindConfig config_;
    WeatherType currentWeather_ = WeatherType::Normal;
    float time_ = 0.0f;
    float weatherMultiplier_ = 1.0f;
    float targetMultiplier_ = 1.0f;
    float transitionSpeed_ = 0.5f;
    irr::core::vector2df windDirection_{1.0f, 0.0f};
};
```

**Wind Calculation Algorithm:**
1. **Height influence**: No movement below `influenceStartHeight`, power curve above
2. **Primary sway**: Slow, large sine wave oscillation based on `baseStrength` and `baseFrequency`
3. **Gust overlay**: Irregular pattern using nested sine waves based on `gustStrength` and `gustFrequency`
4. **Turbulence**: High-frequency position-based noise for small rapid movements
5. **Weather multiplier**: Scales all movement based on current weather conditions
6. **Direction**: Displacement follows configured wind direction in XZ plane

### Phase 4: Animated Tree Mesh System [COMPLETE]

**Implemented Files:**
- `include/client/graphics/animated_tree_manager.h` - AnimatedTreeManager class declaration
- `src/client/graphics/animated_tree_manager.cpp` - Full implementation with CPU vertex animation
- Updated `CMakeLists.txt` to include new source file

**Implementation Approach:** Option B (CPU Vertex Animation)
- Creates animated mesh copies of identified trees
- Stores base vertex positions and normalized heights
- Updates vertex positions each frame based on wind displacement
- Uses dynamic vertex buffer hints for efficient updates

**Key Features:**
- **Tree identification** - Uses TreeIdentifier to find tree meshes from zone geometry
- **Animated mesh copies** - Creates SMeshBuffer with S3DVertex for each tree
- **Per-vertex heights** - Calculates normalized height (0=base, 1=top) for influence
- **Unique tree seeds** - Position-based seed for wind phase variation between trees
- **Distance-based updates** - Only animates trees within `updateDistance`
- **Debug info** - Provides tree count, wind state, and weather info

**File: `include/client/graphics/animated_tree_manager.h`**
```cpp
class AnimatedTreeManager {
public:
    AnimatedTreeManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver);
    ~AnimatedTreeManager();

    void initialize(const std::shared_ptr<WldLoader>& wldLoader,
                    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures);
    void loadConfig(const std::string& configPath = "", const std::string& zoneName = "");
    void setWeather(WeatherType weather);
    void update(float deltaTime, const irr::core::vector3df& cameraPos);
    void cleanup();

    bool isEnabled() const;
    void setEnabled(bool enabled);
    size_t getAnimatedTreeCount() const;
    std::string getDebugInfo() const;

    TreeWindController& getWindController();
    TreeIdentifier& getTreeIdentifier();

private:
    struct AnimatedTree {
        irr::scene::IMeshSceneNode* node;
        irr::scene::SMesh* mesh;
        irr::scene::SMeshBuffer* buffer;
        std::vector<irr::core::vector3df> basePositions;
        std::vector<float> vertexHeights;
        float meshSeed;
        irr::core::vector3df worldPosition;
        irr::core::aabbox3df bounds;
        std::string name;
    };

    void identifyTrees(const std::shared_ptr<WldLoader>& wldLoader, ...);
    void createAnimatedTree(const std::shared_ptr<ZoneGeometry>& geometry, ...);
    void updateTreeAnimation(AnimatedTree& tree);
    float calculateVertexHeight(const irr::core::vector3df& vertex, float minY, float maxY) const;
    float generateTreeSeed(const irr::core::vector3df& position) const;
    irr::scene::SMesh* buildAnimatedMesh(const ZoneGeometry& geometry, ...);

    TreeWindController windController_;
    TreeIdentifier treeIdentifier_;
    std::vector<AnimatedTree> animatedTrees_;
    float updateDistance_, lodDistance_;
};
```

**Animation Update Flow:**
1. `update()` called each frame with deltaTime and camera position
2. Wind controller time accumulated
3. For each tree within update distance:
   - Get wind displacement for each vertex using world position, height, and seed
   - Apply displacement to base position
   - Mark mesh buffer dirty if changed

### Phase 5: Weather Integration [COMPLETE]

**Implemented Files:**
- `include/client/graphics/weather_system.h` - WeatherSystem class, IWeatherListener interface, ZoneWeatherConfig
- `src/client/graphics/weather_system.cpp` - Full implementation with simulation and transitions
- Updated `CMakeLists.txt` to include new source file

**Key Features:**
- **Weather state management** - Tracks current and target weather types
- **Smooth transitions** - Smoothstep interpolation between weather states
- **Zone-based configuration** - Load weather settings per zone from JSON
- **Weather simulation** - Random weather changes based on zone rain/snow chances
- **Listener interface** - IWeatherListener for notifications, plus callback support
- **Wind intensity** - Returns 0.0-1.0 based on weather type (Calm=0.3, Normal=0.6, Rain=0.8, Storm=1.0)
- **Debug info** - Current weather, transition state, wind intensity

**File: `include/client/graphics/weather_system.h`**
```cpp
class IWeatherListener {
public:
    virtual void onWeatherChanged(WeatherType newWeather) = 0;
};

struct ZoneWeatherConfig {
    uint8_t rainChance[4];
    uint8_t rainDuration[4];
    uint8_t snowChance[4];
    uint8_t snowDuration[4];
    WeatherType defaultWeather = WeatherType::Normal;
    bool enabled = true;
    float checkIntervalSeconds = 60.0f;
    std::string zoneName;
};

class WeatherSystem {
public:
    void update(float deltaTime);
    void setWeather(WeatherType type);
    void transitionToWeather(WeatherType type, float transitionTime = 5.0f);

    WeatherType getCurrentWeather() const;
    WeatherType getTargetWeather() const;
    bool isTransitioning() const;
    float getWindIntensity() const;

    void setZoneConfig(const ZoneWeatherConfig& config);
    void setWeatherFromZone(const std::string& zoneName);

    void addListener(IWeatherListener* listener);
    void removeListener(IWeatherListener* listener);
    void addCallback(std::function<void(WeatherType)> callback);

    static const char* getWeatherName(WeatherType type);
};
```

**Zone Weather Config Format:** `data/config/zones/<zonename>/weather.json`
```json
{
    "weather": {
        "rain_chance": [10, 20, 15, 5],
        "rain_duration": [5, 10, 15, 20],
        "default": "normal",
        "enabled": true,
        "check_interval": 60.0
    }
}
```

**Weather Simulation Flow:**
1. `update()` called each frame with deltaTime
2. Transitions interpolated using smoothstep
3. If simulation enabled, check for weather change at intervals
4. Roll for weather based on zone rain chances
5. Notify listeners when weather changes

### Phase 6: Integration with Renderer [COMPLETE]

**Modified Files:**
- `include/client/graphics/irrlicht_renderer.h` - Added member variables
- `src/client/graphics/irrlicht_renderer.cpp` - Integration code

**Implementation:**

1. **Member Variables** (in `irrlicht_renderer.h`):
```cpp
std::unique_ptr<AnimatedTreeManager> treeManager_;  // Tree wind animation
std::unique_ptr<WeatherSystem> weatherSystem_;  // Weather state management
```

2. **Creation** (in `loadGlobalAssets()`):
```cpp
// Create tree wind animation manager
if (!treeManager_) {
    treeManager_ = std::make_unique<AnimatedTreeManager>(smgr_, driver_);
}

// Create weather system with callback to tree manager
if (!weatherSystem_) {
    weatherSystem_ = std::make_unique<WeatherSystem>();
    weatherSystem_->addCallback([this](WeatherType weather) {
        if (treeManager_) {
            treeManager_->setWeather(weather);
        }
    });
}
```

3. **Zone Initialization** (in `loadZone()` after `setupZoneCollision()`):
```cpp
// Initialize tree wind animation system
if (treeManager_ && currentZone_ && currentZone_->wldLoader) {
    treeManager_->loadConfig("", zoneName);
    treeManager_->initialize(currentZone_->wldLoader, currentZone_->textures);
}

// Initialize weather system for this zone
if (weatherSystem_) {
    weatherSystem_->setWeatherFromZone(zoneName);
}
```

4. **Zone Cleanup** (in `unloadZone()`):
```cpp
// Clear tree wind animation system
if (treeManager_) {
    treeManager_->cleanup();
}
```

5. **Frame Update** (in `processFrame()` after detail system update):
```cpp
// Weather System Update
if (weatherSystem_) {
    weatherSystem_->update(deltaTime);
}

// Tree Wind Animation Update
if (treeManager_ && treeManager_->isEnabled()) {
    irr::core::vector3df cameraPos = camera_ ? camera_->getPosition() : irr::core::vector3df(0, 0, 0);
    treeManager_->update(deltaTime, cameraPos);
}
```

### Phase 7: Performance Optimizations

1. **Distance-based LOD**
   - Full animation within `lodDistance`
   - Reduced frequency animation beyond `lodDistance`
   - No animation beyond `updateDistance`

2. **Frustum Culling**
   - Skip animation for trees outside camera frustum

3. **Batch Updates**
   - Update trees in batches across frames if needed

4. **Vertex Buffer Hints**
   - Use `EHM_DYNAMIC` for animated mesh buffers

### File Structure

```
include/client/graphics/
├── tree_wind_config.h
├── tree_wind_controller.h
├── tree_identifier.h
├── animated_tree_manager.h
└── weather_system.h

src/client/graphics/
├── tree_wind_controller.cpp
├── tree_identifier.cpp
├── animated_tree_manager.cpp
└── weather_system.cpp

data/config/
└── tree_wind.json
```

### Implementation Order

1. **Config system** - TreeWindConfig, JSON loading
2. **Tree identification** - TreeIdentifier with texture pattern matching
3. **Wind controller** - TreeWindController with displacement calculation
4. **Animated mesh manager** - AnimatedTreeManager with CPU vertex animation
5. **Weather system** - WeatherSystem with zone-based weather
6. **Renderer integration** - Hook into IrrlichtRenderer update loop
7. **Testing & tuning** - Adjust parameters for natural look

### Testing Checklist

- [ ] Trees sway smoothly with no jitter
- [ ] Base of trees remains fixed
- [ ] Top branches/leaves have maximum movement
- [ ] Movement increases in rain/storm weather
- [ ] Config file changes take effect
- [ ] Performance acceptable with many trees visible
- [ ] No visual artifacts at LOD transitions
- [ ] Works correctly after zone transitions

### Configuration Loading

The system will check for config in this order:
1. Path passed via command line: `--tree-wind-config <path>`
2. Zone-specific: `data/config/zones/<zonename>/tree_wind.json`
3. Default: `data/config/tree_wind.json`
4. Built-in defaults if no file found

### Notes

- Reuse `WindController` concepts from detail system where applicable
- Consider sharing weather state between detail grass wind and tree wind
- May need to identify and exclude certain "tree-like" objects (poles, masts)
- Palm trees may need different sway characteristics than pine/oak
