#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/detail/detail_chunk.h"
#include "client/graphics/detail/detail_texture_atlas.h"
#include "client/graphics/detail/detail_config_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/simulation_worker.h"
#include "common/logging.h"
#include "client/data_paths.h"
#include <fmt/format.h>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <map>

namespace EQT {
namespace Graphics {
namespace Detail {

DetailManager::DetailManager(irr::scene::ISceneManager* smgr,
                             irr::video::IVideoDriver* driver)
    : smgr_(smgr)
    , driver_(driver)
{
    // Ground raycast callback is set via onZoneEnter()

    // Initialize foliage disturbance manager with default config
    disturbanceManager_ = std::make_unique<FoliageDisturbanceManager>(disturbanceConfig_);

    // Initialize footprint manager
    footprintManager_ = std::make_unique<FootprintManager>(smgr_, driver_);
    footprintManager_->setConfig(footprintConfig_);

    LOG_INFO(MOD_GRAPHICS, "DetailManager: Initialized");
}

DetailManager::~DetailManager() {
    if (constrainedCache_) {
        constrainedCache_->removeEvictionListener(this);
    }
    onZoneExit();
    LOG_INFO(MOD_GRAPHICS, "DetailManager: Destroyed");
}

void DetailManager::setConstrainedTextureCache(ConstrainedTextureCache* cache) {
    if (constrainedCache_) {
        constrainedCache_->removeEvictionListener(this);
    }
    constrainedCache_ = cache;
    if (constrainedCache_) {
        constrainedCache_->addEvictionListener(this);
    }
}

void DetailManager::onTextureEvicted(const std::string& name, irr::video::ITexture* /*texture*/) {
    if (name == "detail_atlas") {
        atlasTexture_ = nullptr;
    }
}

void DetailManager::setSurfaceMapsPath(const std::string& path) {
    surfaceMapsPath_ = path;
    LOG_INFO(MOD_GRAPHICS, "DetailManager: Surface maps path set to '{}'", path);
}

void DetailManager::onZoneEnter(const std::string& zoneName,
                                const GroundRaycastFunc& groundRaycast,
                                irr::scene::IMeshSceneNode* zoneMeshNode,
                                const std::shared_ptr<WldLoader>& wldLoader,
                                const std::shared_ptr<ZoneGeometry>& zoneGeometry) {
    // Clear any existing state
    onZoneExit();

    currentZone_ = zoneName;
    groundRaycast_ = groundRaycast;
    zoneMeshNode_ = zoneMeshNode;

    // Try to load pre-computed surface map for fast surface type lookups
    if (!surfaceMapsPath_.empty()) {
        // T399 (board B235): resolved on the COMPLETE path, so the
        // directory knob (setSurfaceMapsPath) keeps working and an absolute
        // override is returned untouched.
        std::string mapPath = eqt::ResolveDataPath(surfaceMapsPath_ + "/" + zoneName + "_surface.map");
        if (surfaceMap_.load(mapPath)) {
            LOG_INFO(MOD_GRAPHICS, "DetailManager: Loaded surface map from '{}' "
                     "({}x{} grid, cell size {})",
                     mapPath, surfaceMap_.getGridWidth(), surfaceMap_.getGridHeight(),
                     surfaceMap_.getCellSize());
        } else {
            LOG_INFO(MOD_GRAPHICS, "DetailManager: No surface map found at '{}', "
                     "detail system disabled for this zone", mapPath);
        }
    }

    // Store zone geometry data for texture lookups
    if (zoneGeometry) {
        zoneGeometry_ = zoneGeometry;
    }

    if (zoneGeometry_) {
        zoneTextureNames_ = zoneGeometry_->textureNames();
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Loaded zone geometry with {} triangles, {} textures",
                 zoneGeometry_->triangles.size(), zoneTextureNames_.size());

        // Log texture index statistics
        std::map<uint32_t, int> textureIndexCounts;
        int emptyTextureCount = 0;
        for (const auto& tri : zoneGeometry_->triangles) {
            textureIndexCounts[tri.textureIndex]++;
            if (tri.textureIndex < zoneTextureNames_.size()) {
                if (zoneTextureNames_[tri.textureIndex].empty()) {
                    emptyTextureCount++;
                }
            }
        }
        LOG_DEBUG(MOD_GRAPHICS, "DetailManager: {} unique texture indices, {} triangles point to empty textures",
                 textureIndexCounts.size(), emptyTextureCount);

        // Log texture names at debug level
        size_t texCount = zoneTextureNames_.size();
        LOG_DEBUG(MOD_GRAPHICS, "DetailManager: Texture names array size: {}", texCount);
        for (size_t i = 0; i < std::min(texCount, size_t(10)); ++i) {
            const std::string& tn = zoneTextureNames_[i];
            LOG_DEBUG(MOD_GRAPHICS, "  [{}] = '{}' (len={})", i, tn, tn.size());
        }

        // Log all unique texture names for debugging
        std::set<std::string> uniqueTextures(zoneTextureNames_.begin(), zoneTextureNames_.end());
        LOG_DEBUG(MOD_GRAPHICS, "DetailManager: Unique textures in zone:");
        for (const auto& tex : uniqueTextures) {
            if (!tex.empty()) {
                LOG_DEBUG(MOD_GRAPHICS, "  - {}", tex);
            }
        }
    }

    // Load texture atlas from pre-generated BMP via constrained cache
    if (!atlasTexture_ && constrainedCache_) {
        std::string atlasPath = eqt::ResolveDataPath("data/textures/detail_atlas.bmp");
        std::ifstream file(atlasPath, std::ios::binary | std::ios::ate);
        if (!file) {
            LOG_FATAL(MOD_GRAPHICS, "DetailManager: Required texture {} not found — run generate_textures first", atlasPath);
            std::exit(1);
        }
        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<char> data(fileSize);
        file.read(data.data(), fileSize);

        const TextureOrigin origin = TextureOrigin::plainFile(atlasPath);  // B947: restore origin
        atlasTexture_ = constrainedCache_->getOrLoad("detail_atlas", data, "uncategorized", "", false, &origin);
        if (!atlasTexture_) atlasTexture_ = constrainedCache_->uploadTexture("detail_atlas");
        if (!atlasTexture_) {
            LOG_FATAL(MOD_GRAPHICS, "DetailManager: Failed to load required texture: {}", atlasPath);
            std::exit(1);
        }
    }

    // Store BSP tree for water/lava/zoneline exclusion queries
    if (wldLoader) {
        bspTree_ = wldLoader->getBspTree();
        if (bspTree_ && !bspTree_->regions.empty()) {
            // Count exclusion regions for logging
            size_t waterCount = 0, lavaCount = 0, zonelineCount = 0;
            for (const auto& region : bspTree_->regions) {
                if (region) {
                    for (auto type : region->regionTypes) {
                        if (type == RegionType::Water || type == RegionType::WaterBlockLOS ||
                            type == RegionType::FreezingWater) {
                            waterCount++;
                            break;
                        } else if (type == RegionType::Lava) {
                            lavaCount++;
                            break;
                        } else if (type == RegionType::Zoneline) {
                            zonelineCount++;
                            break;
                        }
                    }
                }
            }
            LOG_INFO(MOD_GRAPHICS, "DetailManager: BSP tree loaded with {} regions "
                     "(water: {}, lava: {}, zoneline: {})",
                     bspTree_->regions.size(), waterCount, lavaCount, zonelineCount);
        }
    }

    // Load config from JSON file or use hardcoded default
    DetailConfigLoader loader;
    auto loadedConfig = loader.loadDefaultConfig("data");
    if (loadedConfig) {
        setZoneConfig(*loadedConfig);
        config_.zoneName = zoneName;
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Loaded config with {} detail types", config_.detailTypes.size());
    } else {
        LOG_WARN(MOD_GRAPHICS, "DetailManager: Could not load JSON config, using hardcoded defaults");
        setZoneConfig(createDefaultConfig(zoneName));
    }

    // Detect season for this zone and apply seasonal modifiers
    currentSeason_ = seasonalController_.detectSeason(zoneName);
    seasonTint_ = seasonalController_.getSeasonTint(currentSeason_);
    config_.season = currentSeason_;
    config_.seasonTint = seasonTint_;

    // Apply season-specific density multiplier
    float seasonDensity = seasonalController_.getSeasonDensityMultiplier(currentSeason_);
    config_.densityMultiplier *= seasonDensity;

    // Configure wind controller for this zone
    WindParams windParams;
    windParams.strength = config_.windStrength;
    windParams.frequency = config_.windFrequency;
    windParams.gustFrequency = 0.1f;
    windParams.gustStrength = 0.3f;
    windParams.direction = irr::core::vector2df(1.0f, 0.3f).normalize();  // Slight angle
    windController_.setParams(windParams);

    // Load foliage disturbance config
    disturbanceConfig_ = loader.loadFoliageDisturbanceConfig("data");
    if (disturbanceManager_) {
        disturbanceManager_->setConfig(disturbanceConfig_);
    }

    // Load footprint config and set up manager
    footprintConfig_ = loader.loadFootprintConfig("data");
    if (footprintManager_) {
        footprintManager_->setConfig(footprintConfig_);
        footprintManager_->setSurfaceMap(getSurfaceMap());
        footprintManager_->setAtlasTexture(atlasTexture_);
        footprintManager_->setGroundRaycast(groundRaycast_);
    }

    LOG_INFO(MOD_GRAPHICS, "DetailManager: Entered zone '{}', season: {}, {} detail types, density mult: {:.1f}, foliage disturb: {}, footprints: {}",
             zoneName, SeasonalController::getSeasonName(currentSeason_),
             config_.detailTypes.size(), config_.densityMultiplier,
             disturbanceConfig_.enabled ? "enabled" : "disabled",
             footprintConfig_.enabled ? "enabled" : "disabled");
}

void DetailManager::setZoneConfig(const ZoneDetailConfig& config) {
    config_ = config;
    viewDistanceChunks_ = detailChunkRadius(config_.viewDistance, config_.chunkSize);
    LOG_INFO(MOD_GRAPHICS,
             "DetailManager: view distance {} over chunk size {} -> {} chunk "
             "radius ({}x{} grid)",
             config_.viewDistance, config_.chunkSize, viewDistanceChunks_,
             2 * viewDistanceChunks_ + 1, 2 * viewDistanceChunks_ + 1);
}

void DetailManager::onZoneExit() {
    // Queue ClearAll for SimulationWorker
    DetailCommandData cmd;
    cmd.type = DetailCommand::ClearAll;
    pendingCommands_.push_back(std::move(cmd));

    // Detach and clear all chunks
    for (auto& [key, chunk] : chunks_) {
        if (chunk) {
            chunk->detach();
        }
    }
    chunks_.clear();
    activeChunks_.clear();

    // Clear footprints
    if (footprintManager_) {
        footprintManager_->clear();
    }

    groundRaycast_ = nullptr;
    zoneMeshNode_ = nullptr;
    additionalMeshNodes_.clear();
    bspTree_.reset();
    zoneGeometry_.reset();
    zoneTextureNames_.clear();
    currentZone_.clear();
    lastCameraChunk_ = {INT32_MAX, INT32_MAX};

    LOG_INFO(MOD_GRAPHICS, "DetailManager: Exited zone");
}

void DetailManager::addMeshNodeForTextureLookup(irr::scene::IMeshSceneNode* node) {
    if (node && node->getMesh()) {
        additionalMeshNodes_.push_back(node);
    }
}

void DetailManager::update(const irr::core::vector3df& cameraPos, float deltaTimeMs,
                           const irr::core::vector3df& playerPos,
                           const irr::core::vector3df& playerVelocity,
                           float playerHeading,
                           bool playerMoving) {
    // Require pre-computed surface map - don't fall back to on-the-fly detection
    if (!enabled_ || density_ < 0.01f || currentZone_.empty() || !surfaceMap_.isLoaded()) {
        return;
    }

    // Wind time advancement handled by SimulationWorker via detailWindTime_
    // (windController_ still tracks time for getParams() accessors)
    windController_.update(deltaTimeMs);

    // Update footprints (uses ITriangleSelector raycasts — stays on main thread)
    if (footprintManager_ && footprintConfig_.enabled) {
        float deltaTimeSec = deltaTimeMs / 1000.0f;
        footprintManager_->update(deltaTimeSec, playerPos, playerHeading, playerMoving);
    }

    // Update which chunks are visible based on camera position
    // (creates/destroys Irrlicht scene nodes — stays on main thread)
    updateVisibleChunks(cameraPos);

    // Wind and disturbance vertex animation handled by SimulationWorker
}

void DetailManager::setDensity(float density) {
    density = std::clamp(density, 0.0f, 1.0f);
    if (std::abs(density - density_) < 0.001f) {
        return;
    }

    density_ = density;
    LOG_INFO(MOD_GRAPHICS, "DetailManager: Density set to {:.0f}%", density_ * 100.0f);

    // Rebuild all chunk meshes with new density
    rebuildAllChunkMeshes();
}

void DetailManager::adjustDensity(float delta) {
    setDensity(density_ + delta);
}

void DetailManager::setCategoryEnabled(DetailCategory cat, bool enabled) {
    uint32_t catBits = static_cast<uint32_t>(cat);
    if (enabled) {
        categoryMask_ |= catBits;
    } else {
        categoryMask_ &= ~catBits;
    }
    rebuildAllChunkMeshes();
}

bool DetailManager::isCategoryEnabled(DetailCategory cat) const {
    return (categoryMask_ & cat) != 0;
}

void DetailManager::setSeasonOverride(Season season) {
    seasonalController_.setSeasonOverride(season);
    currentSeason_ = season;
    seasonTint_ = seasonalController_.getSeasonTint(season);
    config_.season = season;
    config_.seasonTint = seasonTint_;

    // Apply new season density multiplier
    float seasonDensity = seasonalController_.getSeasonDensityMultiplier(season);
    config_.densityMultiplier = seasonDensity;  // Reset to season default

    LOG_INFO(MOD_GRAPHICS, "DetailManager: Season override set to '{}'",
             SeasonalController::getSeasonName(season));

    // Rebuild chunks with new seasonal colors
    rebuildAllChunkMeshes();
}

void DetailManager::clearSeasonOverride() {
    seasonalController_.clearOverride();

    // Re-detect season for current zone
    if (!currentZone_.empty()) {
        currentSeason_ = seasonalController_.detectSeason(currentZone_);
        seasonTint_ = seasonalController_.getSeasonTint(currentSeason_);
        config_.season = currentSeason_;
        config_.seasonTint = seasonTint_;

        float seasonDensity = seasonalController_.getSeasonDensityMultiplier(currentSeason_);
        config_.densityMultiplier = seasonDensity;

        LOG_INFO(MOD_GRAPHICS, "DetailManager: Season override cleared, auto-detected: '{}'",
                 SeasonalController::getSeasonName(currentSeason_));

        rebuildAllChunkMeshes();
    }
}

void DetailManager::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;

    enabled_ = enabled;

    if (!enabled) {
        // Queue ClearAll for SimulationWorker
        DetailCommandData cmd;
        cmd.type = DetailCommand::ClearAll;
        pendingCommands_.push_back(std::move(cmd));

        // Release chunk mesh data and placements to free memory
        for (auto& [key, chunk] : chunks_) {
            if (chunk) chunk->detach();
        }
        size_t freedChunks = chunks_.size();
        chunks_.clear();
        activeChunks_.clear();
        if (footprintManager_) footprintManager_->clear();
        // Reset so updateVisibleChunks() rebuilds on re-enable
        lastCameraChunk_ = {INT32_MAX, INT32_MAX};
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Disabled, released {} chunks", freedChunks);
    } else {
        // Chunks rebuild on next update() via updateVisibleChunks()
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Enabled, chunks will rebuild on next update");
    }
}

void DetailManager::setFoliageDisturbanceConfig(const FoliageDisturbanceConfig& config) {
    disturbanceConfig_ = config;
    if (disturbanceManager_) {
        disturbanceManager_->setConfig(config);
    }
    LOG_DEBUG(MOD_GRAPHICS, "DetailManager: Foliage disturbance config updated, enabled={}",
             config.enabled ? "true" : "false");
}

const FoliageDisturbanceConfig& DetailManager::getFoliageDisturbanceConfig() const {
    return disturbanceConfig_;
}

bool DetailManager::isFoliageDisturbanceEnabled() const {
    return disturbanceConfig_.enabled && disturbanceManager_ != nullptr;
}

std::vector<DetailCommandData> DetailManager::drainCommands() {
    std::vector<DetailCommandData> cmds;
    cmds.swap(pendingCommands_);
    return cmds;
}

void DetailManager::applyWorkerResults(const SimulationOutput::DetailOutput& results) {
    for (const auto& shadow : results.chunkShadows) {
        if (!shadow.dirty) continue;
        ChunkKey key{shadow.keyX, shadow.keyZ};
        auto it = chunks_.find(key);
        if (it != chunks_.end() && it->second) {
            it->second->applyWorkerShadow(shadow.positions);
        }
    }
}

void DetailManager::setFootprintConfig(const FootprintConfig& config) {
    footprintConfig_ = config;
    if (footprintManager_) {
        footprintManager_->setConfig(config);
    }
    LOG_DEBUG(MOD_GRAPHICS, "DetailManager: Footprint config updated, enabled={}",
             config.enabled ? "true" : "false");
}

const FootprintConfig& DetailManager::getFootprintConfig() const {
    return footprintConfig_;
}

bool DetailManager::isFootprintEnabled() const {
    return footprintConfig_.enabled && footprintManager_ != nullptr;
}

void DetailManager::renderFootprints() {
    if (footprintManager_ && footprintConfig_.enabled) {
        footprintManager_->render();
    }
}

std::string DetailManager::getDebugInfo() const {
    std::string disturbInfo = disturbanceConfig_.enabled ? "on" : "off";
    if (disturbanceConfig_.enabled && disturbanceManager_) {
        disturbInfo = fmt::format("on({})", disturbanceManager_->getResidualCount());
    }

    return fmt::format("Detail: {:.0f}% density, {} chunks, {} placements, season: {}, surfmap: {}, disturb: {}",
                       density_ * 100.0f,
                       activeChunks_.size(),
                       getVisiblePlacementCount(),
                       SeasonalController::getSeasonName(currentSeason_),
                       surfaceMap_.isLoaded() ? "loaded" : "off",
                       disturbInfo);
}

size_t DetailManager::getTotalPlacementCount() const {
    size_t total = 0;
    for (const auto& [key, chunk] : chunks_) {
        if (chunk) {
            total += chunk->getPlacementCount();
        }
    }
    return total;
}

size_t DetailManager::getVisiblePlacementCount() const {
    size_t total = 0;
    for (const auto* chunk : activeChunks_) {
        if (chunk) {
            total += chunk->getVisibleCount();
        }
    }
    return total;
}

bool DetailManager::isExcluded(const irr::core::vector3df& pos) const {
    for (const auto& box : config_.exclusionBoxes) {
        if (box.isPointInside(pos)) {
            return true;
        }
    }
    return false;
}

bool DetailManager::isExcludedByBsp(const irr::core::vector3df& pos) const {
    if (!bspTree_ || bspTree_->regions.empty()) {
        return false;
    }

    // BSP tree uses EQ coordinates (Y and Z are swapped compared to Irrlicht)
    // Irrlicht: (X, Y, Z) where Y is up
    // EQ: (X, Y, Z) where Z is up
    // Transform: Irrlicht (x, y, z) -> EQ (x, z, y)
    auto region = bspTree_->findRegionForPoint(pos.X, pos.Z, pos.Y);
    if (!region) {
        return false;
    }

    // Check if this region is water, lava, or a zone line
    for (auto type : region->regionTypes) {
        switch (type) {
            case RegionType::Water:
            case RegionType::WaterBlockLOS:
            case RegionType::FreezingWater:
            case RegionType::Lava:
            case RegionType::Zoneline:
                return true;
            default:
                break;
        }
    }

    return false;
}

} // namespace Detail
} // namespace Graphics
} // namespace EQT
