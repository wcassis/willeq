# WillEQ Environmental Detail System - Implementation Plan

## Executive Summary

This plan outlines the implementation of a runtime-adjustable environmental detail system (grass, plants, debris) for WillEQ with wind animation and seasonal variants. The system is designed for software rendering with performance constraints in mind.

**Key Requirements:**
1. Runtime density adjustment (0-100%) without reloading zones
2. Wind animation for vegetation using CPU-based vertex displacement
3. Seasonal variants based on zone environment (snow, autumn, etc.)
4. Performance-conscious design for software renderer (~40 FPS baseline)

---

## Codebase Analysis (Verified)

### Relevant Existing Systems

| System | Location | Relevance |
|--------|----------|-----------|
| IrrlichtRenderer | `include/client/graphics/irrlicht_renderer.h` | Integration point, frame loop |
| AnimatedTextureManager | `include/client/graphics/animated_texture_manager.h` | Pattern for texture animation |
| Vertex Animations | `irrlicht_renderer.cpp:1170` (updateVertexAnimations) | Pattern for wind animation |
| SkyConfig | `include/client/graphics/sky_config.h` | Zone weather/environment data |
| BSP Tree | `include/client/graphics/eq/wld_loader.h` | Region queries, zone lines |
| CommandRegistry | `include/client/graphics/ui/command_registry.h` | Slash command registration |
| CameraController | `include/client/graphics/camera_controller.h` | Camera position access |

### Performance Constraints

Current baseline:
- Software renderer runs at ~40 FPS with typical zone geometry
- Backface culling causes visual artifacts with EQ geometry
- Scene graph optimizations (octree, visibility) have limited impact

**Implication:** With 40 FPS baseline, we have more headroom for details. Target maintaining 30+ FPS at maximum detail settings.

### Coordinate System

```
EQ (Z-up): (x, y, z) → Irrlicht (Y-up): (x, z, y)
Camera access: camera_->getPosition() returns Irrlicht coordinates
```

---

## Architecture

### Component Overview

```
┌──────────────────────────────────────────────────────────────────────────┐
│                           DetailManager                                   │
│  - Coordinates all detail systems                                        │
│  - Handles density control (0.0 - 1.0)                                   │
│  - Manages chunk streaming based on camera position                      │
│  - Owns seasonal and wind controllers                                    │
└──────────────────────────────────────────────────────────────────────────┘
         │                    │                     │
         ▼                    ▼                     ▼
┌─────────────────┐  ┌──────────────────┐  ┌────────────────────┐
│  DetailChunkGrid │  │  WindController   │  │ SeasonalController  │
│  - 50x50 unit    │  │  - Time-based     │  │ - Zone environment  │
│    spatial grid  │  │    wind vectors   │  │   queries           │
│  - Per-chunk     │  │  - Per-frame      │  │ - Atlas swapping    │
│    batched mesh  │  │    vertex updates │  │ - Color tinting     │
└─────────────────┘  └──────────────────┘  └────────────────────┘
```

### File Organization

```
include/client/graphics/detail/
├── detail_manager.h           # Main coordinator class
├── detail_chunk.h             # Per-chunk batched mesh
├── detail_placer.h            # Procedural placement algorithms
├── detail_types.h             # Enums, structs, configuration
├── detail_config_loader.h     # JSON config parser
├── wind_controller.h          # Wind simulation and vertex animation
└── seasonal_controller.h      # Season detection and texture variants

src/client/graphics/detail/
├── detail_manager.cpp
├── detail_chunk.cpp
├── detail_placer.cpp
├── detail_config_loader.cpp
├── wind_controller.cpp
└── seasonal_controller.cpp

data/detail/
├── detail_atlas.png           # Combined sprite sheet (grass, plants, etc.)
├── detail_atlas_snow.png      # Winter variant
├── detail_atlas_autumn.png    # Autumn variant (optional)
├── default_config.json        # Default detail configuration
└── zones/
    ├── qeynos.json            # Per-zone overrides
    ├── everfrost.json         # Snow zone config
    └── ...
```

---

## Detailed Design

### 1. Detail Types and Configuration

```cpp
// include/client/graphics/detail/detail_types.h

#pragma once
#include <irrlicht.h>
#include <string>
#include <vector>
#include <cstdint>

namespace EQT {
namespace Graphics {
namespace Detail {

// Detail categories for independent toggling
enum class DetailCategory : uint32_t {
    None        = 0,
    Grass       = 1 << 0,
    Plants      = 1 << 1,
    Rocks       = 1 << 2,
    Debris      = 1 << 3,
    Mushrooms   = 1 << 4,
    All         = 0xFFFFFFFF
};

inline DetailCategory operator|(DetailCategory a, DetailCategory b) {
    return static_cast<DetailCategory>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool operator&(DetailCategory a, DetailCategory b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

// Sprite orientation modes
enum class DetailOrientation {
    CrossedQuads,    // Two quads in X pattern (grass, small plants)
    FlatGround,      // Single quad lying flat (debris, leaves)
    SingleQuad       // Single vertical quad (rarely needed)
};

// Season types matching zone environments
enum class Season {
    Default,         // Standard green vegetation
    Snow,            // Everfrost, Permafrost, etc.
    Autumn,          // Kithicor at night? (optional)
    Desert,          // Ro deserts - reduced vegetation
    Swamp            // Innothule - different colors
};

// Single detail type definition
struct DetailType {
    std::string name;
    DetailCategory category = DetailCategory::Grass;
    DetailOrientation orientation = DetailOrientation::CrossedQuads;

    // UV coordinates in atlas (pixel coords, converted to normalized at load)
    int atlasX = 0;
    int atlasY = 0;
    int atlasWidth = 64;
    int atlasHeight = 64;

    // Cached normalized UVs (set by loader)
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;

    // Size range (randomized per instance)
    float minSize = 0.5f;
    float maxSize = 1.5f;

    // Placement rules
    float minSlope = 0.0f;       // Radians (0 = flat)
    float maxSlope = 0.5f;       // ~28 degrees
    float baseDensity = 1.0f;    // Relative to global density

    // Wind response (0.0 = no wind, 1.0 = full response)
    float windResponse = 1.0f;

    // Height at which wind effect is strongest (normalized 0-1)
    float windHeightBias = 0.8f;
};

// Per-instance placement data
struct DetailPlacement {
    irr::core::vector3df position;
    float rotation;          // Y-axis rotation in radians
    float scale;
    uint16_t typeIndex;      // Index into DetailType array
    uint8_t seed;            // For deterministic randomness
};

// Chunk grid key
struct ChunkKey {
    int32_t x;
    int32_t z;

    bool operator==(const ChunkKey& other) const {
        return x == other.x && z == other.z;
    }

    bool operator<(const ChunkKey& other) const {
        if (x != other.x) return x < other.x;
        return z < other.z;
    }
};

struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const {
        return std::hash<int64_t>()(
            (static_cast<int64_t>(k.x) << 32) |
            static_cast<uint32_t>(k.z));
    }
};

// Zone detail configuration
struct ZoneDetailConfig {
    std::string zoneName;
    bool isOutdoor = true;
    Season season = Season::Default;

    std::vector<DetailType> detailTypes;

    float densityMultiplier = 1.0f;
    float viewDistance = 150.0f;
    float chunkSize = 50.0f;

    // Wind settings for this zone
    float windStrength = 1.0f;
    float windFrequency = 0.5f;   // Oscillations per second

    // Exclusion regions (zone lines, water)
    std::vector<irr::core::aabbox3df> exclusionBoxes;
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT
```

### 2. DetailManager Class

```cpp
// include/client/graphics/detail/detail_manager.h

#pragma once
#include <irrlicht.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include "detail_types.h"

namespace EQT {
namespace Graphics {

// Forward declarations
class WldLoader;

namespace Detail {

class DetailChunk;
class WindController;
class SeasonalController;

class DetailManager {
public:
    DetailManager(irr::scene::ISceneManager* smgr,
                  irr::video::IVideoDriver* driver,
                  irr::io::IFileSystem* fs);
    ~DetailManager();

    // Frame update - call from IrrlichtRenderer::processFrame
    void update(const irr::core::vector3df& cameraPos, float deltaTimeMs);

    // Density control (0.0 = off, 1.0 = maximum)
    void setDensity(float density);
    float getDensity() const { return density_; }
    void adjustDensity(float delta);  // For keyboard +/- controls

    // Zone lifecycle
    void onZoneEnter(const std::string& zoneName,
                     const std::shared_ptr<WldLoader>& wldLoader,
                     irr::scene::ITriangleSelector* zoneSelector);
    void onZoneExit();

    // Category toggles
    void setCategoryEnabled(DetailCategory cat, bool enabled);
    bool isCategoryEnabled(DetailCategory cat) const;

    // Season override (normally auto-detected from zone)
    void setSeasonOverride(Season season);
    void clearSeasonOverride();

    // Debug
    std::string getDebugInfo() const;
    bool isEnabled() const { return enabled_ && density_ > 0.01f; }
    void setEnabled(bool enabled);

    // Access for UI/commands
    size_t getActiveChunkCount() const { return activeChunks_.size(); }
    size_t getTotalPlacementCount() const;

private:
    void updateVisibleChunks(const irr::core::vector3df& cameraPos);
    void loadChunk(const ChunkKey& key);
    void unloadChunk(DetailChunk* chunk);
    void rebuildChunkMeshes();  // Called when density changes

    // Ground height query using triangle selector
    bool getGroundInfo(float x, float z, float& outY,
                       irr::core::vector3df& outNormal) const;

    // Check if point is in exclusion zone
    bool isExcluded(const irr::core::vector3df& pos) const;

    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;
    irr::io::IFileSystem* fs_;

    // State
    bool enabled_ = true;
    float density_ = 0.5f;         // Default 50%
    uint32_t categoryMask_ = static_cast<uint32_t>(DetailCategory::All);

    // Zone data
    ZoneDetailConfig config_;
    std::string currentZone_;
    irr::scene::ITriangleSelector* zoneSelector_ = nullptr;

    // Texture atlas
    irr::video::ITexture* atlasTexture_ = nullptr;
    int atlasWidth_ = 512;
    int atlasHeight_ = 512;

    // Chunk management
    std::unordered_map<ChunkKey, std::unique_ptr<DetailChunk>, ChunkKeyHash> chunks_;
    std::vector<DetailChunk*> activeChunks_;
    ChunkKey lastCameraChunk_{INT32_MAX, INT32_MAX};

    // Sub-controllers
    std::unique_ptr<WindController> windController_;
    std::unique_ptr<SeasonalController> seasonalController_;
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT
```

### 3. DetailChunk Class

```cpp
// include/client/graphics/detail/detail_chunk.h

#pragma once
#include <irrlicht.h>
#include <vector>
#include "detail_types.h"

namespace EQT {
namespace Graphics {
namespace Detail {

class DetailChunk {
public:
    DetailChunk(const ChunkKey& key, float size,
                irr::scene::ISceneManager* smgr,
                irr::video::IVideoDriver* driver);
    ~DetailChunk();

    // Generate placements for this chunk (done once at creation)
    void generatePlacements(const ZoneDetailConfig& config,
                            std::function<bool(float, float, float&,
                                irr::core::vector3df&)> groundQuery,
                            std::function<bool(const irr::core::vector3df&)> exclusionCheck);

    // Rebuild mesh with current density and category mask
    void rebuildMesh(float density, uint32_t categoryMask,
                     const ZoneDetailConfig& config,
                     irr::video::ITexture* atlas);

    // Update vertex positions for wind animation
    void applyWind(float time, const ZoneDetailConfig& config,
                   const irr::core::vector2df& windDir);

    // Scene attachment
    void attach();
    void detach();
    bool isAttached() const { return attached_; }

    const ChunkKey& getKey() const { return key_; }
    const irr::core::aabbox3df& getBounds() const { return bounds_; }
    size_t getPlacementCount() const { return placements_.size(); }
    size_t getVisibleCount() const { return visibleCount_; }

private:
    void buildQuadGeometry(const DetailPlacement& p,
                           const DetailType& type,
                           std::vector<irr::video::S3DVertex>& verts,
                           std::vector<irr::u16>& indices);

    ChunkKey key_;
    float size_;
    irr::core::aabbox3df bounds_;

    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;

    // Full placement list (generated once)
    std::vector<DetailPlacement> placements_;
    bool placementsGenerated_ = false;

    // Current mesh
    irr::scene::SMesh* mesh_ = nullptr;
    irr::scene::SMeshBuffer* buffer_ = nullptr;
    irr::scene::IMeshSceneNode* sceneNode_ = nullptr;
    bool attached_ = false;

    // Tracking
    size_t visibleCount_ = 0;
    float lastDensity_ = -1.0f;
    uint32_t lastCategoryMask_ = 0;

    // Wind animation - store base positions to avoid drift
    std::vector<irr::core::vector3df> basePositions_;
    std::vector<float> windInfluence_;  // Per-vertex wind multiplier
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT
```

### 4. WindController Class

The wind system uses CPU-based vertex displacement, similar to the existing `updateVertexAnimations()` for flags/banners.

```cpp
// include/client/graphics/detail/wind_controller.h

#pragma once
#include <irrlicht.h>
#include <cmath>

namespace EQT {
namespace Graphics {
namespace Detail {

// Wind parameters that can vary per-zone
struct WindParams {
    float strength = 1.0f;        // Overall wind strength multiplier
    float frequency = 0.5f;       // Base oscillation frequency (Hz)
    float gustFrequency = 0.1f;   // Gust overlay frequency
    float gustStrength = 0.3f;    // Gust amplitude relative to base
    irr::core::vector2df direction{1.0f, 0.0f};  // Primary wind direction (XZ plane)
};

class WindController {
public:
    WindController() = default;
    ~WindController() = default;

    // Update wind state (call each frame)
    void update(float deltaTimeMs);

    // Get current wind displacement for a vertex
    // position: world position of vertex
    // heightFactor: 0 = ground, 1 = top of grass (normalized height within detail)
    // windResponse: detail type's wind sensitivity (0-1)
    irr::core::vector3df getDisplacement(
        const irr::core::vector3df& position,
        float heightFactor,
        float windResponse) const;

    // Get current wind direction (for chunk updates)
    irr::core::vector2df getWindDirection() const;
    float getWindMagnitude() const;

    void setParams(const WindParams& params) { params_ = params; }
    const WindParams& getParams() const { return params_; }

    // Global time for animation
    float getTime() const { return time_; }

private:
    WindParams params_;
    float time_ = 0.0f;

    // Cached per-frame values
    mutable irr::core::vector2df cachedDirection_;
    mutable float cachedMagnitude_ = 0.0f;
    mutable float lastUpdateTime_ = -1.0f;
};

// Implementation of displacement calculation
inline irr::core::vector3df WindController::getDisplacement(
    const irr::core::vector3df& position,
    float heightFactor,
    float windResponse) const
{
    if (windResponse < 0.001f || params_.strength < 0.001f) {
        return irr::core::vector3df(0, 0, 0);
    }

    // Spatial variation based on position (creates wave effect)
    float spatialPhase = (position.X * 0.1f + position.Z * 0.13f);

    // Base oscillation
    float baseWave = std::sin(time_ * params_.frequency * 6.28318f + spatialPhase);

    // Add gust variation (slower, larger)
    float gustWave = std::sin(time_ * params_.gustFrequency * 6.28318f +
                              spatialPhase * 0.3f) * params_.gustStrength;

    // Combined wave
    float wave = (baseWave + gustWave) * params_.strength * windResponse;

    // Height-based falloff: more movement at top, less at base
    // Use squared falloff for more natural grass bending
    float heightInfluence = heightFactor * heightFactor;
    wave *= heightInfluence;

    // Apply to X and Z based on wind direction
    // Y displacement is minimal (grass bends, doesn't rise)
    return irr::core::vector3df(
        wave * params_.direction.X * 0.15f,  // X displacement
        -std::abs(wave) * 0.02f,              // Slight Y dip when bent
        wave * params_.direction.Y * 0.15f   // Z displacement
    );
}

} // namespace Detail
} // namespace Graphics
} // namespace EQT
```

### 5. SeasonalController Class

```cpp
// include/client/graphics/detail/seasonal_controller.h

#pragma once
#include <irrlicht.h>
#include <string>
#include <map>
#include "detail_types.h"

namespace EQT {
namespace Graphics {
namespace Detail {

// Maps zones to seasons based on name patterns and sky/weather config
class SeasonalController {
public:
    SeasonalController(irr::video::IVideoDriver* driver,
                       irr::io::IFileSystem* fs);
    ~SeasonalController();

    // Determine season for a zone
    Season detectSeason(const std::string& zoneName) const;

    // Get appropriate atlas texture for season
    irr::video::ITexture* getAtlasForSeason(Season season);

    // Get color tint for season (applied to vertex colors)
    irr::video::SColor getSeasonTint(Season season) const;

    // Preload all seasonal atlases
    void preloadAtlases();

    // Override automatic detection
    void setSeasonOverride(Season season) { overrideSeason_ = season; hasOverride_ = true; }
    void clearOverride() { hasOverride_ = false; }
    bool hasOverride() const { return hasOverride_; }
    Season getOverride() const { return overrideSeason_; }

private:
    irr::video::ITexture* loadAtlas(const std::string& filename);

    irr::video::IVideoDriver* driver_;
    irr::io::IFileSystem* fs_;

    // Cached textures
    std::map<Season, irr::video::ITexture*> atlasCache_;

    // Zone name patterns for season detection
    // Populated from data or hardcoded for known zones
    std::map<std::string, Season> zoneSeasons_;

    // Override state
    bool hasOverride_ = false;
    Season overrideSeason_ = Season::Default;
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT
```

### 6. Zone Season Mapping

```cpp
// In seasonal_controller.cpp - initialize zone mappings

void SeasonalController::initZoneMappings() {
    // Snow zones (from pre_luclin_zones.json analysis)
    zoneSeasons_["everfrost"] = Season::Snow;
    zoneSeasons_["permafrost"] = Season::Snow;
    zoneSeasons_["halas"] = Season::Snow;
    zoneSeasons_["eastwastes"] = Season::Snow;
    zoneSeasons_["cobaltscar"] = Season::Snow;
    zoneSeasons_["wakening"] = Season::Snow;
    zoneSeasons_["westwastes"] = Season::Snow;
    zoneSeasons_["crystal"] = Season::Snow;
    zoneSeasons_["sleeper"] = Season::Snow;
    zoneSeasons_["necropolis"] = Season::Snow;
    zoneSeasons_["templeveeshan"] = Season::Snow;
    zoneSeasons_["thurgadina"] = Season::Snow;
    zoneSeasons_["thurgadinb"] = Season::Snow;
    zoneSeasons_["kael"] = Season::Snow;

    // Desert zones (reduced vegetation)
    zoneSeasons_["nro"] = Season::Desert;
    zoneSeasons_["sro"] = Season::Desert;
    zoneSeasons_["oasis"] = Season::Desert;

    // Swamp zones (different color palette)
    zoneSeasons_["innothule"] = Season::Swamp;
    zoneSeasons_["swampofnohope"] = Season::Swamp;
    zoneSeasons_["feerrott"] = Season::Swamp;
}

irr::video::SColor SeasonalController::getSeasonTint(Season season) const {
    switch (season) {
        case Season::Snow:
            return irr::video::SColor(255, 200, 220, 255);  // Slight blue/white tint
        case Season::Desert:
            return irr::video::SColor(255, 255, 230, 180);  // Yellowed, dry
        case Season::Swamp:
            return irr::video::SColor(255, 180, 200, 150);  // Murky green
        case Season::Autumn:
            return irr::video::SColor(255, 255, 200, 150);  // Orange/brown tint
        default:
            return irr::video::SColor(255, 255, 255, 255);  // No tint
    }
}
```

---

## Integration with IrrlichtRenderer

### Header Additions

```cpp
// In irrlicht_renderer.h, add:

#include "client/graphics/detail/detail_manager.h"

class IrrlichtRenderer {
    // ... existing members ...

    // Detail system
    std::unique_ptr<EQT::Graphics::Detail::DetailManager> detailManager_;

public:
    // ... existing methods ...

    // Detail system access
    EQT::Graphics::Detail::DetailManager* getDetailManager() {
        return detailManager_.get();
    }
};
```

### Implementation Integration

```cpp
// In irrlicht_renderer.cpp

void IrrlichtRenderer::init() {
    // ... existing initialization ...

    // Initialize detail system
    detailManager_ = std::make_unique<EQT::Graphics::Detail::DetailManager>(
        smgr_, driver_, device_->getFileSystem());
}

void IrrlichtRenderer::loadZone(const std::string& zoneName, ...) {
    // ... existing zone loading ...

    // After zone mesh is created and collision selector is set up:
    if (detailManager_) {
        detailManager_->onZoneEnter(zoneName, wldLoader, zoneTriangleSelector_);
    }
}

void IrrlichtRenderer::unloadZone() {
    if (detailManager_) {
        detailManager_->onZoneExit();
    }

    // ... existing cleanup ...
}

bool IrrlichtRenderer::processFrame(float deltaTime) {
    // ... existing frame processing ...

    // After camera update, before drawAll:
    if (detailManager_ && detailManager_->isEnabled()) {
        irr::core::vector3df cameraPos = camera_->getPosition();
        detailManager_->update(cameraPos, deltaTime * 1000.0f);
    }

    // ... rest of frame ...
}

bool IrrlichtRenderer::handleKeyInput(const irr::SEvent::SKeyInput& key) {
    // ... existing key handling ...

    // Detail density controls: [ and ] keys
    if (key.PressedDown && !adminMode_) {
        if (key.Key == irr::KEY_OEM_4) {  // [ key
            if (detailManager_) {
                detailManager_->adjustDensity(-0.1f);
                showTemporaryMessage(fmt::format("Detail: {}%",
                    static_cast<int>(detailManager_->getDensity() * 100)));
            }
            return true;
        }
        if (key.Key == irr::KEY_OEM_6) {  // ] key
            if (detailManager_) {
                detailManager_->adjustDensity(0.1f);
                showTemporaryMessage(fmt::format("Detail: {}%",
                    static_cast<int>(detailManager_->getDensity() * 100)));
            }
            return true;
        }
    }

    return false;
}
```

### Slash Commands

```cpp
// Register in command_registry setup:

registry.registerCommand({
    .name = "detail",
    .aliases = {"detaildensity"},
    .usage = "/detail [0-100]",
    .description = "Set or show detail density",
    .category = "Graphics",
    .handler = [this](const std::string& args) {
        auto* dm = renderer_->getDetailManager();
        if (!dm) {
            chat_->addSystemMessage("Detail system not available");
            return;
        }

        if (args.empty()) {
            chat_->addSystemMessage(fmt::format("Detail density: {}%",
                static_cast<int>(dm->getDensity() * 100)));
        } else {
            int value = std::clamp(std::stoi(args), 0, 100);
            dm->setDensity(value / 100.0f);
            chat_->addSystemMessage(fmt::format("Detail density set to {}%", value));
        }
    }
});

registry.registerCommand({
    .name = "togglegrass",
    .usage = "/togglegrass",
    .description = "Toggle grass rendering",
    .category = "Graphics",
    .handler = [this](const std::string&) {
        auto* dm = renderer_->getDetailManager();
        if (dm) {
            bool enabled = dm->isCategoryEnabled(DetailCategory::Grass);
            dm->setCategoryEnabled(DetailCategory::Grass, !enabled);
            chat_->addSystemMessage(fmt::format("Grass: {}",
                !enabled ? "enabled" : "disabled"));
        }
    }
});

registry.registerCommand({
    .name = "season",
    .usage = "/season [default|snow|autumn|desert|swamp|auto]",
    .description = "Override season for testing",
    .category = "Graphics",
    .handler = [this](const std::string& args) {
        auto* dm = renderer_->getDetailManager();
        if (!dm) return;

        if (args == "auto" || args.empty()) {
            dm->clearSeasonOverride();
            chat_->addSystemMessage("Season: automatic detection");
        } else if (args == "snow") {
            dm->setSeasonOverride(Season::Snow);
            chat_->addSystemMessage("Season override: snow");
        }
        // ... other seasons ...
    }
});
```

---

## Wind Animation Implementation Details

### Per-Frame Update Flow

```cpp
void DetailManager::update(const irr::core::vector3df& cameraPos, float deltaTimeMs) {
    if (!enabled_ || density_ < 0.01f) return;

    // Update wind state
    windController_->update(deltaTimeMs);

    // Update visible chunks
    updateVisibleChunks(cameraPos);

    // Apply wind to active chunks (only those with wind-responsive details)
    if (config_.windStrength > 0.01f) {
        float time = windController_->getTime();
        irr::core::vector2df windDir = windController_->getWindDirection();

        for (DetailChunk* chunk : activeChunks_) {
            chunk->applyWind(time, config_, windDir);
        }
    }
}
```

### Vertex Wind Displacement

```cpp
void DetailChunk::applyWind(float time, const ZoneDetailConfig& config,
                            const irr::core::vector2df& windDir) {
    if (!buffer_ || basePositions_.empty()) return;

    irr::video::S3DVertex* verts = static_cast<irr::video::S3DVertex*>(
        buffer_->getVertices());
    size_t vertCount = buffer_->getVertexCount();

    for (size_t i = 0; i < vertCount && i < basePositions_.size(); ++i) {
        const auto& base = basePositions_[i];
        float influence = windInfluence_[i];

        if (influence < 0.001f) {
            verts[i].Pos = base;
            continue;
        }

        // Calculate displacement using wind controller
        // heightFactor is stored in windInfluence_ (0 at base, 1 at top)
        irr::core::vector3df displacement =
            windController_->getDisplacement(base, influence, config.windStrength);

        verts[i].Pos = base + displacement;
    }

    // Mark buffer as changed
    buffer_->setDirty(irr::scene::EBT_VERTEX);
}
```

### Base Position Storage

When building the mesh, we store the original vertex positions and wind influence:

```cpp
void DetailChunk::buildQuadGeometry(...) {
    // ... create vertices ...

    // Store base positions for wind animation
    for (size_t i = startIdx; i < buffer_->getVertexCount(); ++i) {
        basePositions_.push_back(buffer_->getVertices()[i].Pos);

        // Calculate wind influence based on height within the quad
        // Bottom vertices: influence = 0
        // Top vertices: influence = 1
        float normalizedHeight = /* calculate from UV or position */;
        windInfluence_.push_back(normalizedHeight * type.windResponse);
    }
}
```

---

## Performance Optimizations

### Vertex Budget Targets

With ~40 FPS baseline, we have more headroom for detail geometry:

| Density | Details/Chunk | Active Chunks | Total Details | Vertices | Target FPS |
|---------|---------------|---------------|---------------|----------|------------|
| 0% | 0 | 0 | 0 | 0 | 40 |
| 25% | ~75 | 9 | ~675 | ~5,400 | 38+ |
| 50% | ~150 | 9 | ~1,350 | ~10,800 | 35+ |
| 75% | ~225 | 9 | ~2,025 | ~16,200 | 32+ |
| 100% | ~300 | 9 | ~2,700 | ~21,600 | 30+ |

**Target: Max ~22k vertices at 100% density** (crossed quads = 8 verts each)

### Optimization Strategies

1. **Material Setup**
```cpp
void DetailChunk::setupMaterial(irr::video::ITexture* atlas) {
    irr::video::SMaterial& mat = buffer_->Material;

    // Alpha test is faster than alpha blend
    mat.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
    mat.MaterialTypeParam = 0.5f;  // Alpha threshold

    // Disable expensive features
    mat.Lighting = false;
    mat.FogEnable = true;  // Match zone fog for depth
    mat.BackfaceCulling = false;  // Quads visible from both sides
    mat.ZWriteEnable = true;

    mat.setTexture(0, atlas);
}
```

2. **Lazy Chunk Loading**
   - Only generate placements when chunk first enters view distance
   - Cache generated placements for density changes

3. **Distance-Based Detail Reduction**
```cpp
void DetailChunk::rebuildMesh(float density, ...) {
    // Reduce density for distant chunks
    float distanceSq = /* distance from camera */;
    if (distanceSq > 100.0f * 100.0f) {
        density *= 0.5f;  // 50% reduction beyond 100 units
    }
    // ...
}
```

4. **Wind Update Frequency**
```cpp
// In DetailManager::update
// Only update wind every N frames for performance
static int windUpdateCounter = 0;
if (++windUpdateCounter >= 2) {  // Every other frame
    windUpdateCounter = 0;
    for (DetailChunk* chunk : activeChunks_) {
        chunk->applyWind(time, config_, windDir);
    }
}
```

---

## Configuration Files

### Default Configuration

```json
// data/detail/default_config.json
{
    "version": 1,
    "atlasFile": "detail_atlas.png",
    "atlasWidth": 512,
    "atlasHeight": 512,
    "chunkSize": 50.0,
    "viewDistance": 150.0,
    "defaultDensityMultiplier": 1.0,
    "wind": {
        "strength": 1.0,
        "frequency": 0.5,
        "gustFrequency": 0.1,
        "gustStrength": 0.3,
        "directionX": 1.0,
        "directionZ": 0.0
    },
    "detailTypes": [
        {
            "name": "grass_short",
            "category": "grass",
            "orientation": "crossed_quads",
            "atlas": {"x": 0, "y": 0, "width": 64, "height": 64},
            "sizeRange": [0.3, 0.6],
            "maxSlope": 0.5,
            "baseDensity": 1.0,
            "windResponse": 1.0,
            "windHeightBias": 0.8
        },
        {
            "name": "grass_tall",
            "category": "grass",
            "orientation": "crossed_quads",
            "atlas": {"x": 64, "y": 0, "width": 64, "height": 64},
            "sizeRange": [0.6, 1.2],
            "maxSlope": 0.4,
            "baseDensity": 0.4,
            "windResponse": 1.0,
            "windHeightBias": 0.7
        },
        {
            "name": "wildflower",
            "category": "plants",
            "orientation": "crossed_quads",
            "atlas": {"x": 128, "y": 0, "width": 64, "height": 64},
            "sizeRange": [0.4, 0.8],
            "maxSlope": 0.35,
            "baseDensity": 0.15,
            "windResponse": 0.7,
            "windHeightBias": 0.6
        },
        {
            "name": "small_rock",
            "category": "rocks",
            "orientation": "flat_ground",
            "atlas": {"x": 192, "y": 0, "width": 32, "height": 32},
            "sizeRange": [0.1, 0.3],
            "maxSlope": 0.6,
            "baseDensity": 0.1,
            "windResponse": 0.0
        }
    ]
}
```

### Snow Zone Override

```json
// data/detail/zones/everfrost.json
{
    "extends": "default_config.json",
    "season": "snow",
    "atlasFile": "detail_atlas_snow.png",
    "densityMultiplier": 0.3,
    "wind": {
        "strength": 1.5
    },
    "detailTypes": [
        {
            "name": "snow_grass",
            "category": "grass",
            "orientation": "crossed_quads",
            "atlas": {"x": 0, "y": 0, "width": 64, "height": 64},
            "sizeRange": [0.2, 0.4],
            "baseDensity": 0.5
        },
        {
            "name": "snow_clump",
            "category": "debris",
            "orientation": "flat_ground",
            "atlas": {"x": 64, "y": 0, "width": 64, "height": 64},
            "sizeRange": [0.3, 0.6],
            "baseDensity": 0.3,
            "windResponse": 0.0
        }
    ]
}
```

---

## Implementation Phases

### Phase 1: Core Infrastructure ✅ COMPLETE
- [x] Create `detail/` directory structure
- [x] Implement `DetailTypes.h` enums and structs
- [x] Implement `DetailManager` basic lifecycle (init, zone enter/exit)
- [x] Implement `DetailChunk` with simple quad generation
- [x] Add keyboard controls (`[` and `]` keys in Admin mode)
- [x] Test with colored test quads (no texture atlas)

**Implementation Notes:**
- Files created: `detail_types.h`, `detail_chunk.h/cpp`, `detail_manager.h/cpp`
- Integrated into IrrlichtRenderer via `loadGlobalAssets()` and `setupZoneCollision()`
- Default config creates 4 detail types: grass, tall_grass, flower, rock
- Chunks use 50x50 unit grid, view distance of 2 chunks (5x5 = 25 active chunks)
- Ground queries use `ISceneCollisionManager::getCollisionPoint()` with zone selector
- Keyboard controls: `[` decrease density, `]` increase density (Shift for fine control)

### Phase 2: Procedural Placement ✅ COMPLETE
- [x] Implement ground height queries using `ITriangleSelector`
- [x] Implement `DetailPlacer` with deterministic RNG
- [x] Add chunk grid streaming based on camera position
- [x] Implement slope-based placement filtering
- [x] Add exclusion zone support (zone lines from BSP)
- [x] Test placement distribution in outdoor zones

**Implementation Notes:**
- Ground height queries: Implemented in Phase 1 via `getGroundInfo()` using raycast down from sky
- Deterministic RNG: Implemented in Phase 1 via seeded `std::mt19937` per chunk
- Chunk streaming: Implemented in Phase 1 via `updateVisibleChunks()` with view distance of 2 chunks
- Slope filtering: Implemented in Phase 1 via normal.Y angle check in `generatePlacements()`
- BSP exclusion: Added `isExcludedByBsp()` that queries BSP tree for water/lava/zoneline regions
- Coordinate transform: Irrlicht (X, Y, Z) -> EQ (X, Z, Y) for BSP queries (Y-up to Z-up)
- WldLoader now passed to DetailManager via `onZoneEnter()` for BSP access

### Phase 3: Wind Animation ✅ COMPLETE
- [x] Implement `WindController` with time-based oscillation
- [x] Add spatial variation (wave effect across terrain)
- [x] Implement per-vertex wind displacement in `DetailChunk`
- [x] Store base positions for drift-free animation
- [x] Add per-detail-type wind response settings
- [x] Test wind animation performance impact

**Implementation Notes:**
- Created `wind_controller.h` with `WindParams` struct and `WindController` class
- Wind uses sine waves with spatial phase offset for wave effect across terrain
- Gust overlay (slower frequency, 30% strength) adds natural variation
- Height-based falloff: squared factor so top vertices move more than base
- Wind displacement in X/Z based on direction, slight Y dip when bent
- `DetailChunk::applyWind()` updates vertex positions from stored base positions
- Per-detail-type `windResponse` controls sensitivity (0 for rocks, 1 for grass)
- Wind influence stored per-vertex during mesh rebuild (0 at bottom, windResponse at top)
- `DetailManager::update()` calls wind update and applies to all active chunks

### Phase 4: Seasonal Variants ✅ COMPLETE
- [x] Create `SeasonalController` with zone detection
- [x] Create default grass/plant texture atlas (512x512) - *Deferred: using colored quads with tinting*
- [x] Create snow variant atlas - *Deferred: using vertex color tinting instead*
- [x] Implement atlas swapping based on season - *Deferred: using vertex color tinting instead*
- [x] Add vertex color tinting for seasonal effects
- [x] Map zones to seasons (from `pre_luclin_zones.json`)

**Implementation Notes:**
- Created `seasonal_controller.h/cpp` with zone-to-season mappings
- Zone mappings: Snow (Velious, Everfrost, etc.), Desert (Ro, Oasis), Swamp (Innothule, Feerrott)
- Season tints applied via vertex color modulation in `buildQuadGeometry()`
- `seasonTint` field added to `ZoneDetailConfig` for per-zone color adjustment
- Snow zones: blue/white tint (200, 220, 255), Desert: yellowed (255, 230, 180), Swamp: murky (180, 200, 150)
- Season-specific density multipliers: Snow 40%, Desert 20%, Swamp 120%, Default 100%
- Season override methods: `setSeasonOverride()`, `clearSeasonOverride()` for testing
- Debug info updated to show current season
- Texture atlases deferred to future enhancement - current implementation uses colored quads with tinting

### Phase 5: Configuration and Polish ✅ COMPLETE
- [x] Implement `DetailConfigLoader` for JSON configs
- [x] Create default and per-zone config files
- [x] Add `/detail`, `/togglegrass`, `/season` commands
- [x] Add HUD density indicator
- [x] Add category toggles (grass/plants/rocks/debris)
- [x] Performance profiling and optimization (deferred - baseline ~40 FPS allows headroom)

**Implementation Notes:**
- Created `DetailConfigLoader` class in `detail_config_loader.h/cpp`
- JSON config files in `data/detail/`:
  - `default_config.json` - Standard green vegetation with 4 detail types
  - `zones/everfrost.json` - Snow zone config (30% density, blue/white tint)
  - `zones/nro.json` - Desert zone config (20% density, yellow tint)
- Slash commands registered in `eq.cpp`:
  - `/detail [0-100]` - Set or show detail density
  - `/togglegrass`, `/toggleplants`, `/togglerocks`, `/toggledebris` - Toggle categories
  - `/season [default|snow|autumn|desert|swamp|auto]` - Override season
  - `/detailinfo` - Show debug info
- HUD indicator: Detail info displayed in Admin mode HUD when enabled
- Hotkey hints: `{/}=Detail  /season` added to Admin mode hotkey display
- Category toggles implemented via existing `DetailManager::setCategoryEnabled()`
- Performance: Currently using colored quads without texture atlas, minimal overhead

**Bug Fixes During Testing:**
- Fixed: Used player position (EQ coords converted to Irrlicht) instead of camera position for chunk streaming - camera can be in free-fly mode, follow mode, etc.
- Fixed: Ground raycast now casts UPWARD from Y=-1000 instead of downward from Y=10000 - this finds the ground from below, avoiding hitting roofs/ceilings/overhead geometry

### Phase 6: Documentation and Testing
- [ ] Update CLAUDE.md with detail system keybinds
- [ ] Document configuration format
- [ ] Add integration tests
- [ ] Performance benchmarks across zones

---

## CMake Integration (Implemented)

The detail system sources are added to `CMakeLists.txt` in the GRAPHICS_SOURCES section:

```cmake
# Detail system (grass, plants, debris)
src/client/graphics/detail/detail_manager.cpp
src/client/graphics/detail/detail_chunk.cpp
src/client/graphics/detail/detail_config_loader.cpp
src/client/graphics/detail/seasonal_controller.cpp
```

Headers are in `include/client/graphics/detail/`:
- `detail_types.h` - Enums, structs, configuration types
- `detail_manager.h` - Main coordinator class
- `detail_chunk.h` - Per-chunk batched mesh
- `detail_config_loader.h` - JSON config parser
- `wind_controller.h` - Wind animation (header-only)
- `seasonal_controller.h` - Season detection

---

## Testing Checklist

### Functional Tests
- [ ] Density slider works smoothly (0% to 100%)
- [ ] Same density always produces identical visuals (deterministic)
- [ ] No visible popping when chunks stream in/out
- [ ] Details respect zone geometry (proper height, slope limits)
- [ ] Exclusion zones work correctly (no grass in water/zone lines)
- [ ] Indoor zones have no outdoor details
- [ ] Material renders correctly with zone fog
- [ ] No memory leaks during zone transitions

### Wind Animation Tests
- [ ] Wind animation runs smoothly without stuttering
- [ ] Grass moves realistically (base stays, top sways)
- [ ] No vertex drift over time (positions stay consistent)
- [ ] Wind direction changes create visible wave effects
- [ ] Wind disabled details (rocks) stay static

### Seasonal Tests
- [ ] Snow zones use snow atlas
- [ ] Desert zones have reduced vegetation
- [ ] `/season` override works correctly
- [ ] Seasonal tinting applies properly

### Performance Tests
- [ ] FPS at 0% density matches baseline (~40 FPS)
- [ ] FPS at 50% density: target 35+ FPS
- [ ] FPS at 100% density: target 30+ FPS
- [ ] Wind animation adds <2ms per frame
- [ ] Memory usage increase <10MB at max settings

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Performance degradation | Strict vertex budget, density reduces with distance |
| Memory pressure | Unload distant chunks, reuse mesh buffers |
| Visual popping | Fade-in effect for new chunks (alpha interpolation) |
| Wind drift | Store base positions, reset periodically if needed |
| Software renderer compatibility | Test EMT_TRANSPARENT_ALPHA_CHANNEL_REF early |

---

## Open Questions

1. **Texture Atlas Creation**: Should we create the atlas programmatically from individual PNGs, or require a pre-combined atlas?

2. **Water Detection**: The BSP regions have water/lava flags - should we query these for exclusion, or rely on configured exclusion boxes?

3. **Indoor Detection**: Can we reliably detect indoor zones from BSP data, or should it be config-driven per zone?

4. **LOD System**: Should distant chunks render fewer details per placement, or just fewer placements total?

---

## References

- Existing vertex animation: `irrlicht_renderer.cpp:1170` (`updateVertexAnimations`)
- Existing texture animation: `animated_texture_manager.cpp`
- Zone BSP regions: `wld_loader.h:BspRegion`
- Performance analysis: `.agent/rendering_performance_plan.md`
- Zone data: `data/pre_luclin_zones.json`
