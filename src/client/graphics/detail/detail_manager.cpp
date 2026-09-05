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

        atlasTexture_ = constrainedCache_->getOrLoad("detail_atlas", data);
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
        config_ = *loadedConfig;
        config_.zoneName = zoneName;
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Loaded config with {} detail types", config_.detailTypes.size());
    } else {
        LOG_WARN(MOD_GRAPHICS, "DetailManager: Could not load JSON config, using hardcoded defaults");
        config_ = createDefaultConfig(zoneName);
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

void DetailManager::updateVisibleChunks(const irr::core::vector3df& cameraPos) {
    // Calculate which chunk the camera is in
    ChunkKey cameraChunk;
    cameraChunk.x = static_cast<int32_t>(std::floor(cameraPos.X / config_.chunkSize));
    cameraChunk.z = static_cast<int32_t>(std::floor(cameraPos.Z / config_.chunkSize));

    // Only update if player moved to a different chunk
    if (cameraChunk.x == lastCameraChunk_.x && cameraChunk.z == lastCameraChunk_.z) {
        return;
    }
    lastCameraChunk_ = cameraChunk;

    // Determine which chunks should be active
    std::vector<ChunkKey> neededChunks;
    for (int dx = -viewDistanceChunks_; dx <= viewDistanceChunks_; ++dx) {
        for (int dz = -viewDistanceChunks_; dz <= viewDistanceChunks_; ++dz) {
            ChunkKey key;
            key.x = cameraChunk.x + dx;
            key.z = cameraChunk.z + dz;
            neededChunks.push_back(key);
        }
    }

    // Load new chunks
    for (const auto& key : neededChunks) {
        if (chunks_.find(key) == chunks_.end()) {
            loadChunk(key);
        }
    }

    // Update active chunks list
    activeChunks_.clear();
    for (const auto& key : neededChunks) {
        auto it = chunks_.find(key);
        if (it != chunks_.end() && it->second) {
            activeChunks_.push_back(it->second.get());

            // Ensure chunk is attached
            if (!it->second->isAttached()) {
                it->second->attach();
            }
        }
    }

    // Unload distant chunks
    unloadDistantChunks(cameraPos);
}

void DetailManager::loadChunk(const ChunkKey& key) {
    auto chunk = std::make_unique<DetailChunk>(key, config_.chunkSize, smgr_, driver_);

    // Generate seed from chunk position for deterministic placement
    uint32_t seed = static_cast<uint32_t>(key.x * 73856093 ^ key.z * 19349663);

    // Create ground query function
    GroundQueryFunc groundQuery = nullptr;
    if (groundRaycast_) {
        groundQuery = [this](float x, float z, float& outY, irr::core::vector3df& outNormal,
                            SurfaceType& outSurfaceType) {
            return getGroundInfo(x, z, outY, outNormal, outSurfaceType);
        };
    }

    // Create exclusion check function (combines AABB boxes and BSP regions)
    ExclusionCheckFunc exclusionCheck = [this](const irr::core::vector3df& pos) {
        return isExcluded(pos) || isExcludedByBsp(pos);
    };

    // Generate placements
    chunk->generatePlacements(config_, groundQuery, exclusionCheck, seed);

    // Build mesh with current density
    chunk->rebuildMesh(density_, categoryMask_, config_, atlasTexture_);

    // Queue AddChunk command for SimulationWorker
    if (!chunk->getBasePositions().empty()) {
        DetailCommandData cmd;
        cmd.type = DetailCommand::AddChunk;
        cmd.chunkKeyX = key.x;
        cmd.chunkKeyZ = key.z;
        cmd.basePositions = chunk->getBasePositions();
        cmd.windInfluence = chunk->getWindInfluence();
        pendingCommands_.push_back(std::move(cmd));
    }

    chunks_[key] = std::move(chunk);
}

void DetailManager::unloadDistantChunks(const irr::core::vector3df& cameraPos) {
    // Calculate unload distance (slightly larger than view distance to avoid thrashing)
    float unloadDist = (viewDistanceChunks_ + 2) * config_.chunkSize;
    float unloadDistSq = unloadDist * unloadDist;

    // Find chunks to unload
    std::vector<ChunkKey> toRemove;
    for (auto& [key, chunk] : chunks_) {
        float chunkCenterX = (key.x + 0.5f) * config_.chunkSize;
        float chunkCenterZ = (key.z + 0.5f) * config_.chunkSize;

        float dx = chunkCenterX - cameraPos.X;
        float dz = chunkCenterZ - cameraPos.Z;
        float distSq = dx * dx + dz * dz;

        if (distSq > unloadDistSq) {
            toRemove.push_back(key);
        }
    }

    // Remove distant chunks
    for (const auto& key : toRemove) {
        auto it = chunks_.find(key);
        if (it != chunks_.end()) {
            if (it->second) {
                it->second->detach();
            }
            // Queue RemoveChunk command for SimulationWorker
            DetailCommandData cmd;
            cmd.type = DetailCommand::RemoveChunk;
            cmd.chunkKeyX = key.x;
            cmd.chunkKeyZ = key.z;
            pendingCommands_.push_back(std::move(cmd));
            chunks_.erase(it);
        }
    }
}

void DetailManager::rebuildAllChunkMeshes() {
    for (auto& [key, chunk] : chunks_) {
        if (chunk) {
            chunk->rebuildMesh(density_, categoryMask_, config_, atlasTexture_);
            // Re-queue AddChunk with fresh data after rebuild
            if (!chunk->getBasePositions().empty()) {
                DetailCommandData cmd;
                cmd.type = DetailCommand::AddChunk;
                cmd.chunkKeyX = key.x;
                cmd.chunkKeyZ = key.z;
                cmd.basePositions = chunk->getBasePositions();
                cmd.windInfluence = chunk->getWindInfluence();
                pendingCommands_.push_back(std::move(cmd));
            }
        }
    }
}

bool DetailManager::getGroundInfo(float x, float z, float& outY,
                                  irr::core::vector3df& outNormal,
                                  SurfaceType& outSurfaceType) const {
    if (!groundRaycast_) {
        LOG_DEBUG(MOD_GRAPHICS, "DetailManager::getGroundInfo: No ground raycast callback");
        return false;
    }

    // Use BSP-filtered ground raycast from renderer
    // startY=60.0 — below sky domes/ceilings but above terrain
    irr::core::vector3df hitNormal;
    irr::core::triangle3df hitTriangle;

    if (groundRaycast_(x, z, 60.0f, outY, hitNormal, hitTriangle)) {
        // Ensure normal points up (away from ground)
        outNormal = hitNormal;
        if (outNormal.Y < 0) {
            outNormal = -outNormal;
        }

        // Get surface type - prefer pre-computed surface map (O(1) lookup)
        outSurfaceType = SurfaceType::Grass;  // Default

        // Query position in Irrlicht coords: x=EQ_X, z=EQ_Y (the input params)
        if (surfaceMap_.isLoaded()) {
            // Fast path: use pre-computed surface map
            // Surface map uses EQ coordinates (X, Y horizontal)
            // Input x=EQ_X, z=EQ_Y
            outSurfaceType = surfaceMap_.getSurfaceType(x, z);

            LOG_DEBUG(MOD_GRAPHICS, "DetailManager: Surface map lookup at ({:.1f}, {:.1f}) -> {}",
                     x, z, static_cast<int>(outSurfaceType));
        } else {
            // Slow path: on-the-fly texture detection (fallback if no surface map)
            static bool loggedZoneMeshStatus = false;
            if (!loggedZoneMeshStatus) {
                loggedZoneMeshStatus = true;
                LOG_DEBUG(MOD_GRAPHICS, "DetailManager::getGroundInfo: No surface map, using on-the-fly detection. "
                         "zoneMeshNode_={}, additionalMeshNodes_.size={}",
                         (void*)zoneMeshNode_, additionalMeshNodes_.size());
            }
            // Check for mesh nodes (zoneMeshNode_ in non-PVS mode, additionalMeshNodes_ in PVS mode)
            if (zoneMeshNode_ || !additionalMeshNodes_.empty()) {
                outSurfaceType = getTextureFromMeshNode(hitTriangle);
            }

            // Fall back to zone geometry lookup if mesh lookup didn't find a specific texture
            if (outSurfaceType == SurfaceType::Grass) {
                SurfaceType geomSurface = getSurfaceTypeAtPosition(x, z);
                // Only use geometry lookup if it found a specific non-default surface
                if (geomSurface != SurfaceType::Grass) {
                    outSurfaceType = geomSurface;
                }
            }
        }

        return true;
    }

    return false;
}

SurfaceType DetailManager::getTextureFromMeshNode(const irr::core::triangle3df& hitTriangle) const {
    // Build list of all mesh nodes to search
    std::vector<irr::scene::IMeshSceneNode*> meshNodes;
    if (zoneMeshNode_) {
        meshNodes.push_back(zoneMeshNode_);
    }
    for (auto* node : additionalMeshNodes_) {
        if (node) {
            meshNodes.push_back(node);
        }
    }

    if (meshNodes.empty()) {
        return SurfaceType::Grass;
    }

    // Get hit triangle vertices for comparison
    const auto& p1 = hitTriangle.pointA;
    const auto& p2 = hitTriangle.pointB;
    const auto& p3 = hitTriangle.pointC;
    irr::core::vector3df hitCenter = (p1 + p2 + p3) / 3.0f;

    // Debug: log mesh info once
    static bool loggedMeshInfo = false;
    if (!loggedMeshInfo) {
        loggedMeshInfo = true;
        size_t totalBuffers = 0;
        for (auto* node : meshNodes) {
            if (node->getMesh()) {
                totalBuffers += node->getMesh()->getMeshBufferCount();
            }
        }
        LOG_DEBUG(MOD_GRAPHICS, "DetailManager: {} mesh nodes with {} total buffers for texture lookup",
                 meshNodes.size(), totalBuffers);
    }

    float closestDist = 999999.0f;
    std::string closestTexName;

    // Search through all mesh nodes and their buffers
    for (auto* meshNode : meshNodes) {
        irr::scene::IMesh* mesh = meshNode->getMesh();
        if (!mesh) continue;

        for (irr::u32 bufIdx = 0; bufIdx < mesh->getMeshBufferCount(); ++bufIdx) {
            irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(bufIdx);
            if (!buffer) continue;

            // Check vertex type
            irr::video::E_VERTEX_TYPE vtype = buffer->getVertexType();

            // Check if buffer has this triangle by comparing vertex positions
            const irr::u16* indices = buffer->getIndices();
            irr::u32 indexCount = buffer->getIndexCount();

            // Handle different vertex types
            for (irr::u32 i = 0; i + 2 < indexCount; i += 3) {
                irr::core::vector3df v1, v2, v3;

                if (vtype == irr::video::EVT_STANDARD) {
                    irr::video::S3DVertex* vertices = static_cast<irr::video::S3DVertex*>(buffer->getVertices());
                    v1 = vertices[indices[i]].Pos;
                    v2 = vertices[indices[i + 1]].Pos;
                    v3 = vertices[indices[i + 2]].Pos;
                } else if (vtype == irr::video::EVT_2TCOORDS) {
                    irr::video::S3DVertex2TCoords* vertices = static_cast<irr::video::S3DVertex2TCoords*>(buffer->getVertices());
                    v1 = vertices[indices[i]].Pos;
                    v2 = vertices[indices[i + 1]].Pos;
                    v3 = vertices[indices[i + 2]].Pos;
                } else if (vtype == irr::video::EVT_TANGENTS) {
                    irr::video::S3DVertexTangents* vertices = static_cast<irr::video::S3DVertexTangents*>(buffer->getVertices());
                    v1 = vertices[indices[i]].Pos;
                    v2 = vertices[indices[i + 1]].Pos;
                    v3 = vertices[indices[i + 2]].Pos;
                } else {
                    continue;
                }

                // Calculate distance to triangle center
                irr::core::vector3df triCenter = (v1 + v2 + v3) / 3.0f;
                float dist = triCenter.getDistanceFrom(hitCenter);

                if (dist < closestDist) {
                    closestDist = dist;
                    const irr::video::SMaterial& mat = buffer->getMaterial();
                    irr::video::ITexture* tex = mat.getTexture(0);
                    if (tex) {
                        irr::io::path texPath = tex->getName();
                        closestTexName = texPath.c_str();
                    }
                }

                // Compare with small tolerance - try multiple orderings
                float tolerance = 1.0f;
                bool match = (v1.equals(p1, tolerance) && v2.equals(p2, tolerance) && v3.equals(p3, tolerance)) ||
                            (v1.equals(p2, tolerance) && v2.equals(p3, tolerance) && v3.equals(p1, tolerance)) ||
                            (v1.equals(p3, tolerance) && v2.equals(p1, tolerance) && v3.equals(p2, tolerance)) ||
                            (v1.equals(p1, tolerance) && v2.equals(p3, tolerance) && v3.equals(p2, tolerance)) ||
                            (v1.equals(p2, tolerance) && v2.equals(p1, tolerance) && v3.equals(p3, tolerance)) ||
                            (v1.equals(p3, tolerance) && v2.equals(p2, tolerance) && v3.equals(p1, tolerance));

                if (match) {
                    // Found the matching triangle - get texture from this buffer's material
                    const irr::video::SMaterial& mat = buffer->getMaterial();
                    irr::video::ITexture* tex = mat.getTexture(0);
                    if (tex) {
                        irr::io::path texPath = tex->getName();
                        std::string texName = texPath.c_str();

                        // Extract just the filename if it's a full path
                        size_t lastSlash = texName.find_last_of("/\\");
                        if (lastSlash != std::string::npos) {
                            texName = texName.substr(lastSlash + 1);
                        }

                        // Debug logging (limited)
                        static int meshTexLogCount = 0;
                        if (meshTexLogCount++ < 10) {
                            LOG_DEBUG(MOD_GRAPHICS, "DetailManager: Mesh texture lookup found '{}' (exact match)", texName);
                        }

                        return classifyTexture(texName);
                    }
                    return SurfaceType::Grass;
                }
            }
        }
    }

    // No exact match - use closest triangle's texture if close enough
    static int noMatchLogCount = 0;
    if (noMatchLogCount++ < 5) {
        LOG_DEBUG(MOD_GRAPHICS, "DetailManager: No exact triangle match, closest dist={:.1f}, tex='{}'",
                 closestDist, closestTexName);
    }

    // If closest is within reasonable distance, use its texture
    // Use a larger threshold (50 units) since detail chunks query over larger areas
    // and mesh batching can result in triangles being grouped with nearby surfaces
    if (closestDist < 50.0f && !closestTexName.empty()) {
        size_t lastSlash = closestTexName.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            closestTexName = closestTexName.substr(lastSlash + 1);
        }
        return classifyTexture(closestTexName);
    }

    // Triangle not found in mesh buffers
    return SurfaceType::Grass;
}

SurfaceType DetailManager::getSurfaceTypeAtPosition(float x, float z) const {
    if (!zoneGeometry_ || zoneGeometry_->triangles.empty() || zoneTextureNames_.empty()) {
        return SurfaceType::Grass;  // Default fallback
    }

    const auto& vertices = zoneGeometry_->vertices;
    const auto& triangles = zoneGeometry_->triangles;

    // Debug: log coordinate bounds to understand the geometry range
    static bool loggedCoords = false;
    if (!loggedCoords && !vertices.empty()) {
        loggedCoords = true;
        float minX = vertices[0].x, maxX = vertices[0].x;
        float minY = vertices[0].y, maxY = vertices[0].y;
        for (const auto& v : vertices) {
            if (v.x < minX) minX = v.x;
            if (v.x > maxX) maxX = v.x;
            if (v.y < minY) minY = v.y;
            if (v.y > maxY) maxY = v.y;
        }
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Query pos Irrlicht ({:.1f}, {:.1f})",  x, z);
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Zone geometry EQ X range: [{:.1f}, {:.1f}]", minX, maxX);
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Zone geometry EQ Y range: [{:.1f}, {:.1f}]", minY, maxY);
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Note - Irrlicht X=EQ X, Irrlicht Z=EQ Y");
    }

    // Find triangle containing this XZ point (ignoring Y for ground lookup)
    // Note: Zone geometry uses EQ coordinates where Y is horizontal, Z is up
    // Irrlicht uses X horizontal, Y up, Z horizontal
    // So we search in EQ XY space (which maps to Irrlicht XZ)

    // Debug: track closest triangle
    static int debugCount = 0;
    float closestDist = 999999.0f;
    size_t closestTriIdx = 0;

    for (size_t triIdx = 0; triIdx < triangles.size(); ++triIdx) {
        const auto& tri = triangles[triIdx];
        if (tri.v1 >= vertices.size() || tri.v2 >= vertices.size() || tri.v3 >= vertices.size()) {
            continue;
        }

        const auto& v1 = vertices[tri.v1];
        const auto& v2 = vertices[tri.v2];
        const auto& v3 = vertices[tri.v3];

        // Check if point (x, z) is inside the triangle's XY projection (EQ coords)
        // Note: Irrlicht X = EQ X, Irrlicht Z = EQ Y
        float x1 = v1.x, y1 = v1.y;
        float x2 = v2.x, y2 = v2.y;
        float x3 = v3.x, y3 = v3.y;

        // Track distance to triangle center for debugging
        float cx = (x1 + x2 + x3) / 3.0f;
        float cy = (y1 + y2 + y3) / 3.0f;
        float dist = std::sqrt((x - cx) * (x - cx) + (z - cy) * (z - cy));
        if (dist < closestDist) {
            closestDist = dist;
            closestTriIdx = triIdx;
        }

        // Barycentric coordinate test
        float denom = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
        if (std::abs(denom) < 0.0001f) continue;  // Degenerate triangle

        float a = ((y2 - y3) * (x - x3) + (x3 - x2) * (z - y3)) / denom;
        float b = ((y3 - y1) * (x - x3) + (x1 - x3) * (z - y3)) / denom;
        float c = 1.0f - a - b;

        // Check if point is inside triangle (with small tolerance)
        if (a >= -0.01f && b >= -0.01f && c >= -0.01f && a <= 1.01f && b <= 1.01f && c <= 1.01f) {
            // Found the triangle - get its texture
            static int logCount = 0;
            if (tri.textureIndex < zoneTextureNames_.size()) {
                const std::string& texName = zoneTextureNames_[tri.textureIndex];
                SurfaceType surfType = classifyTexture(texName);

                // Debug logging (limited)
                if (logCount++ < 20) {
                    LOG_DEBUG(MOD_GRAPHICS, "DetailManager: Position ({:.1f}, {:.1f}) -> texIdx {} -> texture '{}' -> surface {}",
                             x, z, tri.textureIndex, texName, static_cast<int>(surfType));
                }

                return surfType;
            } else {
                if (logCount++ < 20) {
                    LOG_DEBUG(MOD_GRAPHICS, "DetailManager: Position ({:.1f}, {:.1f}) -> texIdx {} OUT OF RANGE (max {})",
                             x, z, tri.textureIndex, zoneTextureNames_.size());
                }
            }
            break;
        }
    }

    // Debug: log closest triangle info if no match found
    if (debugCount++ < 5 && closestDist < 999999.0f) {
        const auto& tri = triangles[closestTriIdx];
        const auto& v1 = vertices[tri.v1];
        const auto& v2 = vertices[tri.v2];
        const auto& v3 = vertices[tri.v3];
        LOG_DEBUG(MOD_GRAPHICS, "DetailManager: No triangle match for ({:.1f}, {:.1f}), closest dist={:.1f}",
                 x, z, closestDist);
        LOG_DEBUG(MOD_GRAPHICS, "  Closest tri verts: ({:.1f},{:.1f}), ({:.1f},{:.1f}), ({:.1f},{:.1f})",
                 v1.x, v1.y, v2.x, v2.y, v3.x, v3.y);
    }

    return SurfaceType::Grass;  // Default if no triangle found
}

SurfaceType DetailManager::classifyTexture(const std::string& textureName) const {
    // Empty texture name - default to grass for outdoor zones
    // Hard surfaces in EQ zones (cobble, brick, floor) typically HAVE texture names
    // Missing texture data usually comes from terrain/ground meshes
    if (textureName.empty()) {
        return SurfaceType::Grass;
    }

    // Convert to lowercase for case-insensitive matching
    std::string name = textureName;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);

    // Helper to check if name starts with a prefix
    auto startsWith = [&name](const char* prefix) {
        return name.rfind(prefix, 0) == 0;
    };

    // Debug: log texture classification (limited)
    static int classifyLogCount = 0;

    // Helper to log classification
    auto logAndReturn = [&](SurfaceType surface, const char* reason) {
        if (classifyLogCount++ < 20) {
            LOG_DEBUG(MOD_GRAPHICS, "DetailManager: classifyTexture '{}' -> {} ({})",
                     name, static_cast<int>(surface), reason);
        }
        return surface;
    };

    // === EXCLUSIONS FIRST - textures that are NOT walkable ground ===

    // Check for water/lava first (these should be excluded entirely)
    if (name.find("water") != std::string::npos ||
        name.find("lava") != std::string::npos ||
        name.find("lavaf") != std::string::npos ||
        name.find("falls") != std::string::npos ||
        name.find("fount") != std::string::npos ||
        name.find("agua") != std::string::npos) {
        return logAndReturn(SurfaceType::Water, "water/lava");
    }

    // Wall surfaces - treat as brick/stone (indoor) - exclude from detail
    if (name.find("wall") != std::string::npos ||
        name.find("waal") != std::string::npos ||     // EQ typo
        name.find("wail") != std::string::npos ||     // EQ typo
        name.find("wafl") != std::string::npos) {     // EQ abbreviation
        return logAndReturn(SurfaceType::Brick, "wall");
    }

    // Roof textures - no vegetation
    if (name.find("roof") != std::string::npos ||
        name.find("ceil") != std::string::npos) {
        return logAndReturn(SurfaceType::Stone, "roof/ceiling");
    }

    // === BIOME-SPECIFIC GROUND TEXTURES ===
    // Check these BEFORE generic grass to properly categorize expansion content

    // Swamp/marsh textures (Innothule, Feerrott, Kunark swamps)
    if (name.find("swamp") != std::string::npos ||
        name.find("marsh") != std::string::npos ||
        name.find("bog") != std::string::npos ||
        name.find("muck") != std::string::npos ||
        name.find("slime") != std::string::npos ||
        name.find("sludge") != std::string::npos) {
        return logAndReturn(SurfaceType::Swamp, "swamp/marsh");
    }

    // Jungle textures (Kunark tropical zones)
    if (name.find("jungle") != std::string::npos ||
        name.find("fern") != std::string::npos ||
        name.find("palm") != std::string::npos ||
        name.find("tropical") != std::string::npos ||
        startsWith("ej") ||           // Emerald Jungle prefix
        startsWith("sbjung")) {       // Stonebrunt jungle
        return logAndReturn(SurfaceType::Jungle, "jungle/tropical");
    }

    // Firiona Vie grass is tropical (Kunark) - check for "fir" prefix but not "fire"
    if ((startsWith("fir") || name.find("firgrass") != std::string::npos) &&
        name.find("fire") == std::string::npos) {
        return logAndReturn(SurfaceType::Jungle, "firiona/tropical");
    }

    // Snow/ice textures (Velious zones) - expanded patterns
    if (name.find("snow") != std::string::npos ||
        name.find("ice") != std::string::npos ||
        name.find("frost") != std::string::npos ||
        name.find("frozen") != std::string::npos ||
        name.find("icsnow") != std::string::npos ||
        startsWith("gdr") ||          // Great Divide prefix
        startsWith("vel") ||          // Velketor prefix
        startsWith("wice") ||         // Velious water ice
        startsWith("thu")) {          // Thurgadin prefix
        return logAndReturn(SurfaceType::Snow, "snow/ice");
    }

    // === GENERIC GROUND TEXTURES ===

    // Grass surfaces
    if (name.find("grass") != std::string::npos ||
        name.find("gras") != std::string::npos ||
        name.find("lawn") != std::string::npos ||
        name.find("turf") != std::string::npos) {
        return logAndReturn(SurfaceType::Grass, "grass");
    }

    // Natural rock/cliff terrain (NOT man-made stone floors)
    if ((name.find("rock") != std::string::npos ||
         name.find("cliff") != std::string::npos ||
         name.find("boulder") != std::string::npos ||
         name.find("mountain") != std::string::npos ||
         name.find("crag") != std::string::npos) &&
        name.find("floor") == std::string::npos &&
        name.find("flor") == std::string::npos &&
        name.find("tile") == std::string::npos) {
        return logAndReturn(SurfaceType::Rock, "natural rock");
    }

    // Man-made stone surfaces - cobblestone, floors, tiles
    if (name.find("stone") != std::string::npos ||
        name.find("cobble") != std::string::npos ||
        name.find("coble") != std::string::npos ||    // EQ uses "coble" not "cobble"
        name.find("marble") != std::string::npos ||
        name.find("granite") != std::string::npos ||
        name.find("slate") != std::string::npos ||
        name.find("pave") != std::string::npos ||
        name.find("floor") != std::string::npos ||
        name.find("flor") != std::string::npos ||     // EQ abbreviation
        name.find("flr") != std::string::npos ||      // EQ abbreviation
        name.find("tile") != std::string::npos ||
        name.find("til") != std::string::npos ||      // EQ abbreviation
        name.find("carpet") != std::string::npos) {   // Carpet = indoor
        return logAndReturn(SurfaceType::Stone, "stone/floor/tile");
    }

    // Brick surfaces
    if (name.find("brick") != std::string::npos ||
        name.find("city") != std::string::npos ||     // citywall = stone/brick
        name.find("cyw") != std::string::npos ||      // cyw = city wall prefix
        name.find("leav") != std::string::npos ||     // eaves/leaves on buildings
        name.find("eave") != std::string::npos) {     // roof eaves
        return logAndReturn(SurfaceType::Brick, "brick/city");
    }

    // Wood surfaces
    if (name.find("wood") != std::string::npos ||
        name.find("plank") != std::string::npos ||
        name.find("board") != std::string::npos ||
        name.find("timber") != std::string::npos ||
        name.find("deck") != std::string::npos ||
        name.find("wdfloor") != std::string::npos ||
        name.find("jam") != std::string::npos) {       // Door jamb/frame
        return logAndReturn(SurfaceType::Wood, "wood");
    }

    // Gold/decorative building textures - treat as stone
    if (name.find("gold") != std::string::npos ||
        name.find("ornate") != std::string::npos ||
        name.find("decor") != std::string::npos ||
        name.find("trim") != std::string::npos) {
        return logAndReturn(SurfaceType::Stone, "decorative");
    }

    // Sand surfaces
    if (name.find("sand") != std::string::npos ||
        name.find("beach") != std::string::npos ||
        name.find("desert") != std::string::npos ||
        name.find("dune") != std::string::npos) {
        return logAndReturn(SurfaceType::Sand, "sand");
    }

    // Dirt surfaces
    if (name.find("dirt") != std::string::npos ||
        name.find("drt") != std::string::npos ||       // EQ abbreviation for dirt
        name.find("xdrt") != std::string::npos ||      // exterior dirt pattern
        name.find("mud") != std::string::npos ||
        name.find("ground") != std::string::npos ||
        name.find("path") != std::string::npos ||
        name.find("road") != std::string::npos) {
        return logAndReturn(SurfaceType::Dirt, "dirt");
    }

    // === KUNARK ZONE PREFIXES ===

    // Burning Woods - volcanic/ash ground
    if (startsWith("bw") && (name.find("ground") != std::string::npos ||
                             name.find("grass") != std::string::npos)) {
        return logAndReturn(SurfaceType::Dirt, "burning woods ash");
    }

    // Dreadlands - barren rock
    if (startsWith("dread") || startsWith("drd")) {
        return logAndReturn(SurfaceType::Rock, "dreadlands rock");
    }

    // Field of Bone - bone/dirt ground
    if (startsWith("fob") || name.find("bone") != std::string::npos) {
        return logAndReturn(SurfaceType::Dirt, "field of bone");
    }

    // Default to grass for unknown textures
    // Hard surfaces (cobble, brick, etc.) are usually explicitly named in EQ zones
    return logAndReturn(SurfaceType::Grass, "default/unknown");
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
