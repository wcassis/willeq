#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/detail/detail_chunk.h"
#include "client/graphics/detail/detail_texture_atlas.h"
#include "client/graphics/detail/detail_config_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "common/logging.h"
#include <fmt/format.h>
#include <cmath>
#include <algorithm>
#include <cctype>
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
    if (smgr_) {
        collisionManager_ = smgr_->getSceneCollisionManager();
    }

    // Initialize foliage disturbance manager with default config
    disturbanceManager_ = std::make_unique<FoliageDisturbanceManager>(disturbanceConfig_);

    // Initialize footprint manager
    footprintManager_ = std::make_unique<FootprintManager>(smgr_, driver_);
    footprintManager_->setConfig(footprintConfig_);

    LOG_INFO(MOD_GRAPHICS, "DetailManager: Initialized");
}

DetailManager::~DetailManager() {
    onZoneExit();
    LOG_INFO(MOD_GRAPHICS, "DetailManager: Destroyed");
}

void DetailManager::setSurfaceMapsPath(const std::string& path) {
    surfaceMapsPath_ = path;
    LOG_INFO(MOD_GRAPHICS, "DetailManager: Surface maps path set to '{}'", path);
}

void DetailManager::onZoneEnter(const std::string& zoneName,
                                irr::scene::ITriangleSelector* zoneSelector,
                                irr::scene::IMeshSceneNode* zoneMeshNode,
                                const std::shared_ptr<WldLoader>& wldLoader,
                                const std::shared_ptr<ZoneGeometry>& zoneGeometry) {
    // Clear any existing state
    onZoneExit();

    currentZone_ = zoneName;
    zoneSelector_ = zoneSelector;
    zoneMeshNode_ = zoneMeshNode;

    // Try to load pre-computed surface map for fast surface type lookups
    if (!surfaceMapsPath_.empty()) {
        std::string mapPath = surfaceMapsPath_ + "/" + zoneName + "_surface.map";
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
    // Prefer passed zoneGeometry (same as renderer uses), fall back to wldLoader
    if (zoneGeometry) {
        zoneGeometry_ = zoneGeometry;
    } else if (wldLoader) {
        zoneGeometry_ = wldLoader->getCombinedGeometry();
    }

    if (zoneGeometry_) {
        // Texture names are stored in the geometry itself, not WldLoader
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Zone geometry ptr={}, triangles={}, textureNames.size={}",
                 (void*)zoneGeometry_.get(), zoneGeometry_->triangles.size(), zoneGeometry_->textureNames.size());
        zoneTextureNames_ = zoneGeometry_->textureNames;
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
        LOG_INFO(MOD_GRAPHICS, "DetailManager: {} unique texture indices used, {} triangles point to empty textures",
                 textureIndexCounts.size(), emptyTextureCount);

        // Log first few texture names by index
        size_t texCount = zoneTextureNames_.size();
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Texture names array size: {}", texCount);
        for (size_t i = 0; i < std::min(texCount, size_t(10)); ++i) {
            const std::string& tn = zoneTextureNames_[i];
            LOG_INFO(MOD_GRAPHICS, "  [{}] = '{}' (len={})", i, tn, tn.size());
        }

        // Log all unique texture names for debugging
        std::set<std::string> uniqueTextures(zoneTextureNames_.begin(), zoneTextureNames_.end());
        LOG_INFO(MOD_GRAPHICS, "DetailManager: Unique textures in zone:");
        for (const auto& tex : uniqueTextures) {
            if (!tex.empty()) {
                LOG_INFO(MOD_GRAPHICS, "  - {}", tex);
            }
        }
    }

    // Load texture atlas from pre-generated file
    if (!atlasTexture_ && driver_) {
        std::string atlasPath = "data/textures/detail_atlas.png";
        atlasTexture_ = driver_->getTexture(atlasPath.c_str());
        if (!atlasTexture_) {
            LOG_WARN(MOD_GRAPHICS, "DetailManager: Atlas not found at {}, disabling detail objects. Run generate_textures tool.", atlasPath);
            enabled_ = false;
            return;
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
        footprintManager_->setCollisionSelector(zoneSelector_);
    }

    LOG_INFO(MOD_GRAPHICS, "DetailManager: Entered zone '{}', season: {}, {} detail types, density mult: {:.1f}, foliage disturb: {}, footprints: {}",
             zoneName, SeasonalController::getSeasonName(currentSeason_),
             config_.detailTypes.size(), config_.densityMultiplier,
             disturbanceConfig_.enabled ? "enabled" : "disabled",
             footprintConfig_.enabled ? "enabled" : "disabled");
}

void DetailManager::onZoneExit() {
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

    zoneSelector_ = nullptr;
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

    // Update wind animation state
    windController_.update(deltaTimeMs);

    // Update foliage disturbance state
    if (disturbanceManager_ && disturbanceConfig_.enabled) {
        disturbanceManager_->clearSources();

        // Add player as disturbance source if moving
        if (playerMoving) {
            DisturbanceSource source;
            source.position = playerPos;
            source.velocity = playerVelocity;
            source.radius = disturbanceConfig_.playerRadius;
            source.strength = disturbanceConfig_.playerStrength;
            disturbanceManager_->addDisturbanceSource(source);
        }

        disturbanceManager_->update(deltaTimeMs);
    }

    // Update footprints
    if (footprintManager_ && footprintConfig_.enabled) {
        float deltaTimeSec = deltaTimeMs / 1000.0f;
        footprintManager_->update(deltaTimeSec, playerPos, playerHeading, playerMoving);
    }

    // Update which chunks are visible based on camera position
    updateVisibleChunks(cameraPos);

    // Apply wind and disturbance animation to active chunks
    bool useDisturbance = disturbanceManager_ && disturbanceConfig_.enabled;

    for (DetailChunk* chunk : activeChunks_) {
        if (chunk) {
            if (useDisturbance) {
                chunk->applyWindAndDisturbance(windController_, *disturbanceManager_, config_);
            } else if (config_.windStrength > 0.01f) {
                chunk->applyWind(windController_, config_);
            }
        }
    }
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

    // Show/hide all chunks
    for (auto* chunk : activeChunks_) {
        if (enabled) {
            chunk->attach();
        } else {
            chunk->detach();
        }
    }
}

void DetailManager::setFoliageDisturbanceConfig(const FoliageDisturbanceConfig& config) {
    disturbanceConfig_ = config;
    if (disturbanceManager_) {
        disturbanceManager_->setConfig(config);
    }
    LOG_INFO(MOD_GRAPHICS, "DetailManager: Foliage disturbance config updated, enabled={}",
             config.enabled ? "true" : "false");
}

const FoliageDisturbanceConfig& DetailManager::getFoliageDisturbanceConfig() const {
    return disturbanceConfig_;
}

bool DetailManager::isFoliageDisturbanceEnabled() const {
    return disturbanceConfig_.enabled && disturbanceManager_ != nullptr;
}

void DetailManager::setFootprintConfig(const FootprintConfig& config) {
    footprintConfig_ = config;
    if (footprintManager_) {
        footprintManager_->setConfig(config);
    }
    LOG_INFO(MOD_GRAPHICS, "DetailManager: Footprint config updated, enabled={}",
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
    if (zoneSelector_ && collisionManager_) {
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
            chunks_.erase(it);
        }
    }
}

void DetailManager::rebuildAllChunkMeshes() {
    for (auto& [key, chunk] : chunks_) {
        if (chunk) {
            chunk->rebuildMesh(density_, categoryMask_, config_, atlasTexture_);
        }
    }
}

bool DetailManager::getGroundInfo(float x, float z, float& outY,
                                  irr::core::vector3df& outNormal,
                                  SurfaceType& outSurfaceType) const {
    if (!zoneSelector_ || !collisionManager_) {
        LOG_DEBUG(MOD_GRAPHICS, "DetailManager::getGroundInfo: No selector or collision manager");
        return false;
    }

    // Raycast DOWNWARD from above to find the ground surface
    // Start from moderate height (below sky domes/ceilings but above terrain)
    // Many zones have invisible ceilings at Y=80+ that we need to avoid hitting
    irr::core::vector3df start(x, 60.0f, z);
    irr::core::vector3df end(x, -500.0f, z);
    irr::core::vector3df hitPoint;
    irr::core::triangle3df hitTriangle;
    irr::scene::ISceneNode* outNode = nullptr;

    irr::core::line3df ray(start, end);
    if (collisionManager_->getCollisionPoint(ray, zoneSelector_, hitPoint, hitTriangle, outNode)) {
        outY = hitPoint.Y;
        // Ensure normal points up (away from ground)
        outNormal = hitTriangle.getNormal().normalize();
        if (outNormal.Y < 0) {
            outNormal = -outNormal;
        }

        // Get surface type - prefer pre-computed surface map (O(1) lookup)
        outSurfaceType = SurfaceType::Grass;  // Default

        if (surfaceMap_.isLoaded()) {
            // Fast path: use pre-computed surface map
            // Note: Surface map uses EQ coordinates (X, Y horizontal, Z up)
            // Irrlicht hit point is (X, Y, Z) where Y is up
            // So we pass hitPoint.X (EQ X) and hitPoint.Z (EQ Y) to the surface map
            outSurfaceType = surfaceMap_.getSurfaceType(hitPoint.X, hitPoint.Z);

            LOG_DEBUG(MOD_GRAPHICS, "DetailManager: Surface map lookup at ({:.1f}, {:.1f}) -> {}",
                     hitPoint.X, hitPoint.Z, static_cast<int>(outSurfaceType));
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
                SurfaceType geomSurface = getSurfaceTypeAtPosition(hitPoint.X, hitPoint.Z);
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

ZoneDetailConfig DetailManager::createDefaultConfig(const std::string& zoneName) const {
    ZoneDetailConfig config;
    config.zoneName = zoneName;
    config.isOutdoor = true;
    config.chunkSize = 50.0f;
    config.viewDistance = 150.0f;
    config.densityMultiplier = 1.0f;

    // Create detail types with proper UV coordinates for the atlas
    // Atlas is 256x256 with 64x64 tiles (4x4 grid)
    // Row 0: GrassShort, GrassTall, Flower1, Flower2
    // Row 1: Rock1, Rock2, Debris, Mushroom

    // Grass - only on natural grass/dirt surfaces
    DetailType grass;
    grass.name = "grass";
    grass.category = DetailCategory::Grass;
    grass.orientation = DetailOrientation::CrossedQuads;
    grass.minSize = 1.0f;
    grass.maxSize = 2.4f;
    grass.baseDensity = 1.0f;
    grass.maxSlope = 0.5f;
    grass.windResponse = 1.0f;
    grass.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass) |
                            static_cast<uint32_t>(SurfaceType::Dirt);
    grass.testColor = irr::video::SColor(255, 0, 255, 0);  // Bright green
    // GrassShort: tile (0,0)
    getTileUV(AtlasTile::GrassShort, grass.u0, grass.v0, grass.u1, grass.v1);
    config.detailTypes.push_back(grass);

    // Tall grass - only on grass surfaces
    DetailType tallGrass;
    tallGrass.name = "tall_grass";
    tallGrass.category = DetailCategory::Grass;
    tallGrass.orientation = DetailOrientation::CrossedQuads;
    tallGrass.minSize = 2.0f;
    tallGrass.maxSize = 4.0f;
    tallGrass.baseDensity = 0.3f;
    tallGrass.maxSlope = 0.4f;
    tallGrass.windResponse = 1.0f;
    tallGrass.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass);
    tallGrass.testColor = irr::video::SColor(255, 0, 200, 0);  // Green
    // GrassTall: tile (1,0)
    getTileUV(AtlasTile::GrassTall, tallGrass.u0, tallGrass.v0, tallGrass.u1, tallGrass.v1);
    config.detailTypes.push_back(tallGrass);

    // ==========================================
    // Additional grass varieties (Row 7)
    // These add visual variety with dead blades, different shapes
    // ==========================================

    // Mixed grass (short) - has ~20% dead/dying blades intermixed
    DetailType grassMixed1;
    grassMixed1.name = "grass_mixed_short";
    grassMixed1.category = DetailCategory::Grass;
    grassMixed1.orientation = DetailOrientation::CrossedQuads;
    grassMixed1.minSize = 1.0f;
    grassMixed1.maxSize = 2.2f;
    grassMixed1.baseDensity = 0.4f;
    grassMixed1.maxSlope = 0.5f;
    grassMixed1.windResponse = 0.9f;
    grassMixed1.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass) |
                                   static_cast<uint32_t>(SurfaceType::Dirt);
    grassMixed1.testColor = irr::video::SColor(255, 100, 180, 60);
    getTileUV(AtlasTile::GrassMixed1, grassMixed1.u0, grassMixed1.v0, grassMixed1.u1, grassMixed1.v1);
    config.detailTypes.push_back(grassMixed1);

    // Mixed grass (tall) - has ~20% dead/dying blades intermixed
    DetailType grassMixed2;
    grassMixed2.name = "grass_mixed_tall";
    grassMixed2.category = DetailCategory::Grass;
    grassMixed2.orientation = DetailOrientation::CrossedQuads;
    grassMixed2.minSize = 2.0f;
    grassMixed2.maxSize = 3.8f;
    grassMixed2.baseDensity = 0.25f;
    grassMixed2.maxSlope = 0.45f;
    grassMixed2.windResponse = 1.0f;
    grassMixed2.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass);
    grassMixed2.testColor = irr::video::SColor(255, 80, 170, 50);
    getTileUV(AtlasTile::GrassMixed2, grassMixed2.u0, grassMixed2.v0, grassMixed2.u1, grassMixed2.v1);
    config.detailTypes.push_back(grassMixed2);

    // Grass clump - dense cluster radiating from center, varied heights
    DetailType grassClump;
    grassClump.name = "grass_clump";
    grassClump.category = DetailCategory::Grass;
    grassClump.orientation = DetailOrientation::CrossedQuads;
    grassClump.minSize = 1.8f;
    grassClump.maxSize = 3.5f;
    grassClump.baseDensity = 0.2f;
    grassClump.maxSlope = 0.4f;
    grassClump.windResponse = 0.85f;
    grassClump.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass) |
                                  static_cast<uint32_t>(SurfaceType::Dirt);
    grassClump.testColor = irr::video::SColor(255, 90, 175, 55);
    getTileUV(AtlasTile::GrassClump, grassClump.u0, grassClump.v0, grassClump.u1, grassClump.v1);
    config.detailTypes.push_back(grassClump);

    // Wispy grass - thin, delicate single-pixel blades
    DetailType grassWispy;
    grassWispy.name = "grass_wispy";
    grassWispy.category = DetailCategory::Grass;
    grassWispy.orientation = DetailOrientation::CrossedQuads;
    grassWispy.minSize = 1.2f;
    grassWispy.maxSize = 2.8f;
    grassWispy.baseDensity = 0.35f;
    grassWispy.maxSlope = 0.55f;
    grassWispy.windResponse = 1.2f;  // More responsive - delicate
    grassWispy.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass);
    grassWispy.testColor = irr::video::SColor(255, 115, 200, 70);
    getTileUV(AtlasTile::GrassWispy, grassWispy.u0, grassWispy.v0, grassWispy.u1, grassWispy.v1);
    config.detailTypes.push_back(grassWispy);

    // Broad grass - wide, flat leaf blades (like ornamental grass)
    DetailType grassBroad;
    grassBroad.name = "grass_broad";
    grassBroad.category = DetailCategory::Grass;
    grassBroad.orientation = DetailOrientation::CrossedQuads;
    grassBroad.minSize = 1.5f;
    grassBroad.maxSize = 3.2f;
    grassBroad.baseDensity = 0.15f;
    grassBroad.maxSlope = 0.4f;
    grassBroad.windResponse = 0.7f;  // Less responsive - sturdy
    grassBroad.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass) |
                                  static_cast<uint32_t>(SurfaceType::Dirt);
    grassBroad.testColor = irr::video::SColor(255, 75, 155, 45);
    getTileUV(AtlasTile::GrassBroad, grassBroad.u0, grassBroad.v0, grassBroad.u1, grassBroad.v1);
    config.detailTypes.push_back(grassBroad);

    // Curved grass - wind-blown appearance with curved tips
    DetailType grassCurved;
    grassCurved.name = "grass_curved";
    grassCurved.category = DetailCategory::Grass;
    grassCurved.orientation = DetailOrientation::CrossedQuads;
    grassCurved.minSize = 1.6f;
    grassCurved.maxSize = 3.4f;
    grassCurved.baseDensity = 0.3f;
    grassCurved.maxSlope = 0.45f;
    grassCurved.windResponse = 1.1f;
    grassCurved.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass);
    grassCurved.testColor = irr::video::SColor(255, 90, 175, 50);
    getTileUV(AtlasTile::GrassCurved, grassCurved.u0, grassCurved.v0, grassCurved.u1, grassCurved.v1);
    config.detailTypes.push_back(grassCurved);

    // Seed head grass - grass with seed heads at top (wheat-like)
    DetailType grassSeedHead;
    grassSeedHead.name = "grass_seed_head";
    grassSeedHead.category = DetailCategory::Grass;
    grassSeedHead.orientation = DetailOrientation::CrossedQuads;
    grassSeedHead.minSize = 2.2f;
    grassSeedHead.maxSize = 4.2f;
    grassSeedHead.baseDensity = 0.18f;
    grassSeedHead.maxSlope = 0.4f;
    grassSeedHead.windResponse = 0.9f;
    grassSeedHead.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass) |
                                     static_cast<uint32_t>(SurfaceType::Dirt);
    grassSeedHead.testColor = irr::video::SColor(255, 180, 165, 100);  // Golden seed heads
    getTileUV(AtlasTile::GrassSeedHead, grassSeedHead.u0, grassSeedHead.v0, grassSeedHead.u1, grassSeedHead.v1);
    config.detailTypes.push_back(grassSeedHead);

    // Broken grass - bent/broken blades (trampled or weather-damaged)
    DetailType grassBroken;
    grassBroken.name = "grass_broken";
    grassBroken.category = DetailCategory::Grass;
    grassBroken.orientation = DetailOrientation::CrossedQuads;
    grassBroken.minSize = 1.2f;
    grassBroken.maxSize = 2.6f;
    grassBroken.baseDensity = 0.12f;  // Less common
    grassBroken.maxSlope = 0.5f;
    grassBroken.windResponse = 0.6f;  // Less responsive - damaged
    grassBroken.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass) |
                                   static_cast<uint32_t>(SurfaceType::Dirt);
    grassBroken.testColor = irr::video::SColor(255, 95, 165, 55);
    getTileUV(AtlasTile::GrassBroken, grassBroken.u0, grassBroken.v0, grassBroken.u1, grassBroken.v1);
    config.detailTypes.push_back(grassBroken);

    // Flowers - only on grass surfaces
    DetailType flower;
    flower.name = "flower";
    flower.category = DetailCategory::Plants;
    flower.orientation = DetailOrientation::CrossedQuads;
    flower.minSize = 1.6f;
    flower.maxSize = 3.2f;
    flower.baseDensity = 0.25f;  // Balanced density
    flower.maxSlope = 0.35f;
    flower.windResponse = 0.7f;
    flower.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass);
    flower.testColor = irr::video::SColor(255, 255, 0, 255);  // Magenta
    // Flower1: tile (2,0)
    getTileUV(AtlasTile::Flower1, flower.u0, flower.v0, flower.u1, flower.v1);
    config.detailTypes.push_back(flower);

    // Rocks - allowed on natural AND hard surfaces
    DetailType rock;
    rock.name = "rock";
    rock.category = DetailCategory::Rocks;
    rock.orientation = DetailOrientation::CrossedQuads;  // Use crossed quads for visibility
    rock.minSize = 1.0f;
    rock.maxSize = 2.0f;
    rock.baseDensity = 0.15f;  // Balanced density
    rock.maxSlope = 0.7f;
    rock.windResponse = 0.0f;
    rock.allowedSurfaces = static_cast<uint32_t>(SurfaceType::All) &
                           ~static_cast<uint32_t>(SurfaceType::Water) &
                           ~static_cast<uint32_t>(SurfaceType::Lava);  // Anywhere except water/lava
    rock.testColor = irr::video::SColor(255, 150, 150, 150);  // Gray
    // Rock1: tile (0,1)
    getTileUV(AtlasTile::Rock1, rock.u0, rock.v0, rock.u1, rock.v1);
    config.detailTypes.push_back(rock);

    // Debris - allowed on natural AND hard surfaces (twigs, leaves, etc)
    DetailType debris;
    debris.name = "debris";
    debris.category = DetailCategory::Debris;
    debris.orientation = DetailOrientation::CrossedQuads;
    debris.minSize = 0.8f;
    debris.maxSize = 1.6f;
    debris.baseDensity = 0.12f;  // Balanced density
    debris.maxSlope = 0.6f;
    debris.windResponse = 0.0f;
    debris.allowedSurfaces = static_cast<uint32_t>(SurfaceType::All) &
                             ~static_cast<uint32_t>(SurfaceType::Water) &
                             ~static_cast<uint32_t>(SurfaceType::Lava);  // Anywhere except water/lava
    debris.testColor = irr::video::SColor(255, 200, 150, 100);  // Light brown
    // Debris: tile (2,1)
    getTileUV(AtlasTile::Debris, debris.u0, debris.v0, debris.u1, debris.v1);
    config.detailTypes.push_back(debris);

    // Mushrooms - only on natural grass/dirt surfaces
    DetailType mushroom;
    mushroom.name = "mushroom";
    mushroom.category = DetailCategory::Mushrooms;
    mushroom.orientation = DetailOrientation::CrossedQuads;
    mushroom.minSize = 0.8f;
    mushroom.maxSize = 1.6f;
    mushroom.baseDensity = 0.08f;  // Balanced density
    mushroom.maxSlope = 0.4f;
    mushroom.windResponse = 0.0f;
    mushroom.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Grass) |
                               static_cast<uint32_t>(SurfaceType::Dirt);
    mushroom.testColor = irr::video::SColor(255, 255, 50, 50);  // Red
    // Mushroom: tile (3,1)
    getTileUV(AtlasTile::Mushroom, mushroom.u0, mushroom.v0, mushroom.u1, mushroom.v1);
    config.detailTypes.push_back(mushroom);

    // ==========================================
    // Snow biome (Velious) - SurfaceType::Snow
    // ==========================================

    // Ice crystals - small crystalline ice formations
    DetailType iceCrystal;
    iceCrystal.name = "ice_crystal";
    iceCrystal.category = DetailCategory::Rocks;  // Crystal formations are rock-like
    iceCrystal.orientation = DetailOrientation::CrossedQuads;
    iceCrystal.minSize = 0.6f;
    iceCrystal.maxSize = 1.4f;
    iceCrystal.baseDensity = 0.2f;
    iceCrystal.maxSlope = 0.5f;
    iceCrystal.windResponse = 0.0f;  // Ice doesn't blow in wind
    iceCrystal.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Snow);
    iceCrystal.testColor = irr::video::SColor(255, 180, 220, 255);  // Light blue
    getTileUV(AtlasTile::IceCrystal, iceCrystal.u0, iceCrystal.v0, iceCrystal.u1, iceCrystal.v1);
    config.detailTypes.push_back(iceCrystal);

    // Snowdrifts - small piles of snow
    DetailType snowdrift;
    snowdrift.name = "snowdrift";
    snowdrift.category = DetailCategory::Debris;  // Ground cover like debris
    snowdrift.orientation = DetailOrientation::CrossedQuads;
    snowdrift.minSize = 1.0f;
    snowdrift.maxSize = 2.5f;
    snowdrift.baseDensity = 0.35f;
    snowdrift.maxSlope = 0.4f;
    snowdrift.windResponse = 0.0f;
    snowdrift.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Snow);
    snowdrift.testColor = irr::video::SColor(255, 240, 245, 255);  // White-blue
    getTileUV(AtlasTile::Snowdrift, snowdrift.u0, snowdrift.v0, snowdrift.u1, snowdrift.v1);
    config.detailTypes.push_back(snowdrift);

    // Dead grass - brown/dead grass poking through snow
    DetailType deadGrass;
    deadGrass.name = "dead_grass";
    deadGrass.category = DetailCategory::Grass;
    deadGrass.orientation = DetailOrientation::CrossedQuads;
    deadGrass.minSize = 1.2f;
    deadGrass.maxSize = 2.4f;
    deadGrass.baseDensity = 0.25f;
    deadGrass.maxSlope = 0.45f;
    deadGrass.windResponse = 0.5f;  // Less wind response than healthy grass
    deadGrass.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Snow);
    deadGrass.testColor = irr::video::SColor(255, 139, 119, 101);  // Brown
    getTileUV(AtlasTile::DeadGrass, deadGrass.u0, deadGrass.v0, deadGrass.u1, deadGrass.v1);
    config.detailTypes.push_back(deadGrass);

    // Snow-covered rocks
    DetailType snowRock;
    snowRock.name = "snow_rock";
    snowRock.category = DetailCategory::Rocks;
    snowRock.orientation = DetailOrientation::CrossedQuads;
    snowRock.minSize = 0.8f;
    snowRock.maxSize = 1.8f;
    snowRock.baseDensity = 0.15f;
    snowRock.maxSlope = 0.6f;
    snowRock.windResponse = 0.0f;
    snowRock.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Snow);
    snowRock.testColor = irr::video::SColor(255, 200, 200, 210);  // Grayish white
    getTileUV(AtlasTile::SnowRock, snowRock.u0, snowRock.v0, snowRock.u1, snowRock.v1);
    config.detailTypes.push_back(snowRock);

    // ==========================================
    // Sand biome (Ro deserts, beaches) - SurfaceType::Sand
    // ==========================================

    // Seashells - scattered on beaches
    DetailType shell;
    shell.name = "shell";
    shell.category = DetailCategory::Debris;
    shell.orientation = DetailOrientation::CrossedQuads;
    shell.minSize = 0.4f;
    shell.maxSize = 1.0f;
    shell.baseDensity = 0.4f;
    shell.maxSlope = 0.3f;
    shell.windResponse = 0.0f;
    shell.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Sand);
    shell.testColor = irr::video::SColor(255, 245, 235, 220);  // Cream/shell color
    getTileUV(AtlasTile::Shell, shell.u0, shell.v0, shell.u1, shell.v1);
    config.detailTypes.push_back(shell);

    // Beach grass - sparse dune grass
    DetailType beachGrass;
    beachGrass.name = "beach_grass";
    beachGrass.category = DetailCategory::Grass;
    beachGrass.orientation = DetailOrientation::CrossedQuads;
    beachGrass.minSize = 1.0f;
    beachGrass.maxSize = 2.2f;
    beachGrass.baseDensity = 0.5f;  // Sparse but visible in desert/beach
    beachGrass.maxSlope = 0.4f;
    beachGrass.windResponse = 0.8f;
    beachGrass.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Sand);
    beachGrass.testColor = irr::video::SColor(255, 180, 180, 120);  // Tan/yellow
    getTileUV(AtlasTile::BeachGrass, beachGrass.u0, beachGrass.v0, beachGrass.u1, beachGrass.v1);
    config.detailTypes.push_back(beachGrass);

    // Pebbles - small stones on sand
    DetailType pebbles;
    pebbles.name = "pebbles";
    pebbles.category = DetailCategory::Rocks;
    pebbles.orientation = DetailOrientation::CrossedQuads;
    pebbles.minSize = 0.5f;
    pebbles.maxSize = 1.2f;
    pebbles.baseDensity = 0.4f;
    pebbles.maxSlope = 0.5f;
    pebbles.windResponse = 0.0f;
    pebbles.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Sand);
    pebbles.testColor = irr::video::SColor(255, 160, 150, 140);  // Gray-brown
    getTileUV(AtlasTile::Pebbles, pebbles.u0, pebbles.v0, pebbles.u1, pebbles.v1);
    config.detailTypes.push_back(pebbles);

    // Driftwood - beach debris
    DetailType driftwood;
    driftwood.name = "driftwood";
    driftwood.category = DetailCategory::Debris;
    driftwood.orientation = DetailOrientation::CrossedQuads;
    driftwood.minSize = 0.8f;
    driftwood.maxSize = 2.0f;
    driftwood.baseDensity = 0.3f;  // Sparse but visible
    driftwood.maxSlope = 0.35f;
    driftwood.windResponse = 0.0f;
    driftwood.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Sand);
    driftwood.testColor = irr::video::SColor(255, 139, 119, 101);  // Brown wood
    getTileUV(AtlasTile::Driftwood, driftwood.u0, driftwood.v0, driftwood.u1, driftwood.v1);
    config.detailTypes.push_back(driftwood);

    // Small cactus - desert vegetation
    DetailType cactus;
    cactus.name = "cactus";
    cactus.category = DetailCategory::Plants;
    cactus.orientation = DetailOrientation::CrossedQuads;
    cactus.minSize = 1.0f;
    cactus.maxSize = 2.5f;
    cactus.baseDensity = 0.25f;  // Sparse but visible
    cactus.maxSlope = 0.3f;
    cactus.windResponse = 0.0f;  // Cacti don't sway
    cactus.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Sand);
    cactus.testColor = irr::video::SColor(255, 100, 140, 80);  // Desert green
    getTileUV(AtlasTile::Cactus, cactus.u0, cactus.v0, cactus.u1, cactus.v1);
    config.detailTypes.push_back(cactus);

    // ==========================================
    // Jungle biome (Kunark tropical) - SurfaceType::Jungle
    // ==========================================

    // Tropical fern - large leafy fern
    DetailType fern;
    fern.name = "fern";
    fern.category = DetailCategory::Plants;
    fern.orientation = DetailOrientation::CrossedQuads;
    fern.minSize = 1.5f;
    fern.maxSize = 3.5f;
    fern.baseDensity = 0.4f;  // Dense tropical vegetation
    fern.maxSlope = 0.4f;
    fern.windResponse = 0.6f;
    fern.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Jungle);
    fern.testColor = irr::video::SColor(255, 60, 140, 60);  // Dark green
    getTileUV(AtlasTile::Fern, fern.u0, fern.v0, fern.u1, fern.v1);
    config.detailTypes.push_back(fern);

    // Tropical flower - colorful jungle flower
    DetailType tropicalFlower;
    tropicalFlower.name = "tropical_flower";
    tropicalFlower.category = DetailCategory::Plants;
    tropicalFlower.orientation = DetailOrientation::CrossedQuads;
    tropicalFlower.minSize = 1.2f;
    tropicalFlower.maxSize = 2.8f;
    tropicalFlower.baseDensity = 0.25f;
    tropicalFlower.maxSlope = 0.35f;
    tropicalFlower.windResponse = 0.5f;
    tropicalFlower.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Jungle);
    tropicalFlower.testColor = irr::video::SColor(255, 255, 100, 150);  // Pink/red
    getTileUV(AtlasTile::TropicalFlower, tropicalFlower.u0, tropicalFlower.v0, tropicalFlower.u1, tropicalFlower.v1);
    config.detailTypes.push_back(tropicalFlower);

    // Dense tropical grass
    DetailType jungleGrass;
    jungleGrass.name = "jungle_grass";
    jungleGrass.category = DetailCategory::Grass;
    jungleGrass.orientation = DetailOrientation::CrossedQuads;
    jungleGrass.minSize = 1.5f;
    jungleGrass.maxSize = 3.0f;
    jungleGrass.baseDensity = 0.5f;  // Very dense
    jungleGrass.maxSlope = 0.45f;
    jungleGrass.windResponse = 0.8f;
    jungleGrass.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Jungle);
    jungleGrass.testColor = irr::video::SColor(255, 80, 160, 40);  // Bright jungle green
    getTileUV(AtlasTile::JungleGrass, jungleGrass.u0, jungleGrass.v0, jungleGrass.u1, jungleGrass.v1);
    config.detailTypes.push_back(jungleGrass);

    // Ground vines
    DetailType vine;
    vine.name = "vine";
    vine.category = DetailCategory::Plants;
    vine.orientation = DetailOrientation::CrossedQuads;
    vine.minSize = 1.0f;
    vine.maxSize = 2.5f;
    vine.baseDensity = 0.15f;
    vine.maxSlope = 0.5f;
    vine.windResponse = 0.3f;
    vine.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Jungle);
    vine.testColor = irr::video::SColor(255, 50, 120, 50);  // Dark green
    getTileUV(AtlasTile::Vine, vine.u0, vine.v0, vine.u1, vine.v1);
    config.detailTypes.push_back(vine);

    // Small bamboo shoots
    DetailType bamboo;
    bamboo.name = "bamboo";
    bamboo.category = DetailCategory::Plants;
    bamboo.orientation = DetailOrientation::CrossedQuads;
    bamboo.minSize = 2.0f;
    bamboo.maxSize = 4.0f;
    bamboo.baseDensity = 0.1f;
    bamboo.maxSlope = 0.3f;
    bamboo.windResponse = 0.4f;
    bamboo.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Jungle);
    bamboo.testColor = irr::video::SColor(255, 140, 180, 80);  // Light green-yellow
    getTileUV(AtlasTile::Bamboo, bamboo.u0, bamboo.v0, bamboo.u1, bamboo.v1);
    config.detailTypes.push_back(bamboo);

    // ==========================================
    // Swamp biome (Innothule, Kunark marshes) - SurfaceType::Swamp
    // ==========================================

    // Cattails - tall marsh reeds with brown tops
    DetailType cattail;
    cattail.name = "cattail";
    cattail.category = DetailCategory::Plants;
    cattail.orientation = DetailOrientation::CrossedQuads;
    cattail.minSize = 2.0f;
    cattail.maxSize = 4.5f;
    cattail.baseDensity = 0.3f;
    cattail.maxSlope = 0.3f;
    cattail.windResponse = 0.5f;
    cattail.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Swamp);
    cattail.testColor = irr::video::SColor(255, 100, 140, 80);  // Marsh green
    getTileUV(AtlasTile::Cattail, cattail.u0, cattail.v0, cattail.u1, cattail.v1);
    config.detailTypes.push_back(cattail);

    // Swamp mushrooms - fungi growing in wet areas
    DetailType swampMushroom;
    swampMushroom.name = "swamp_mushroom";
    swampMushroom.category = DetailCategory::Mushrooms;
    swampMushroom.orientation = DetailOrientation::CrossedQuads;
    swampMushroom.minSize = 0.8f;
    swampMushroom.maxSize = 2.0f;
    swampMushroom.baseDensity = 0.2f;
    swampMushroom.maxSlope = 0.4f;
    swampMushroom.windResponse = 0.0f;
    swampMushroom.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Swamp);
    swampMushroom.testColor = irr::video::SColor(255, 160, 120, 100);  // Brownish
    getTileUV(AtlasTile::SwampMushroom, swampMushroom.u0, swampMushroom.v0, swampMushroom.u1, swampMushroom.v1);
    config.detailTypes.push_back(swampMushroom);

    // Marsh reeds - shorter than cattails
    DetailType reed;
    reed.name = "reed";
    reed.category = DetailCategory::Grass;
    reed.orientation = DetailOrientation::CrossedQuads;
    reed.minSize = 1.5f;
    reed.maxSize = 3.0f;
    reed.baseDensity = 0.4f;
    reed.maxSlope = 0.35f;
    reed.windResponse = 0.6f;
    reed.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Swamp);
    reed.testColor = irr::video::SColor(255, 90, 130, 70);  // Dark swamp green
    getTileUV(AtlasTile::Reed, reed.u0, reed.v0, reed.u1, reed.v1);
    config.detailTypes.push_back(reed);

    // Lily pads at water edges (for ground near swamp water)
    DetailType lilyPad;
    lilyPad.name = "lily_pad";
    lilyPad.category = DetailCategory::Plants;
    lilyPad.orientation = DetailOrientation::FlatGround;  // Flat on ground
    lilyPad.minSize = 1.0f;
    lilyPad.maxSize = 2.5f;
    lilyPad.baseDensity = 0.15f;
    lilyPad.maxSlope = 0.15f;  // Only on very flat ground
    lilyPad.windResponse = 0.0f;
    lilyPad.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Swamp);
    lilyPad.testColor = irr::video::SColor(255, 60, 120, 60);  // Green
    getTileUV(AtlasTile::LilyPad, lilyPad.u0, lilyPad.v0, lilyPad.u1, lilyPad.v1);
    config.detailTypes.push_back(lilyPad);

    // Soggy marsh grass
    DetailType swampGrass;
    swampGrass.name = "swamp_grass";
    swampGrass.category = DetailCategory::Grass;
    swampGrass.orientation = DetailOrientation::CrossedQuads;
    swampGrass.minSize = 1.2f;
    swampGrass.maxSize = 2.5f;
    swampGrass.baseDensity = 0.35f;
    swampGrass.maxSlope = 0.4f;
    swampGrass.windResponse = 0.7f;
    swampGrass.allowedSurfaces = static_cast<uint32_t>(SurfaceType::Swamp);
    swampGrass.testColor = irr::video::SColor(255, 80, 110, 60);  // Dull swamp green
    getTileUV(AtlasTile::SwampGrass, swampGrass.u0, swampGrass.v0, swampGrass.u1, swampGrass.v1);
    config.detailTypes.push_back(swampGrass);

    return config;
}

} // namespace Detail
} // namespace Graphics
} // namespace EQT
