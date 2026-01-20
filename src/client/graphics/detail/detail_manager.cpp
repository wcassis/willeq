#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/detail/detail_chunk.h"
#include "client/graphics/detail/detail_texture_atlas.h"
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
            LOG_WARN(MOD_GRAPHICS, "DetailManager: No surface map found at '{}', "
                     "falling back to on-the-fly texture detection", mapPath);
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

    // Create texture atlas if not already created
    if (!atlasTexture_ && driver_) {
        DetailTextureAtlas atlasGenerator;
        atlasTexture_ = atlasGenerator.createAtlas(driver_);
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

    // Create default config for this zone
    config_ = createDefaultConfig(zoneName);

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

    LOG_INFO(MOD_GRAPHICS, "DetailManager: Entered zone '{}', season: {}, {} detail types, density mult: {:.1f}",
             zoneName, SeasonalController::getSeasonName(currentSeason_),
             config_.detailTypes.size(), config_.densityMultiplier);
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

void DetailManager::update(const irr::core::vector3df& playerPosIrrlicht, float deltaTimeMs) {
    if (!enabled_ || density_ < 0.01f || currentZone_.empty()) {
        return;
    }

    // Update wind animation state
    windController_.update(deltaTimeMs);

    // Update which chunks are visible based on player position
    updateVisibleChunks(playerPosIrrlicht);

    // Apply wind animation to active chunks
    if (config_.windStrength > 0.01f) {
        for (DetailChunk* chunk : activeChunks_) {
            if (chunk) {
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

std::string DetailManager::getDebugInfo() const {
    return fmt::format("Detail: {:.0f}% density, {} chunks, {} placements, season: {}, surfmap: {}",
                       density_ * 100.0f,
                       activeChunks_.size(),
                       getVisiblePlacementCount(),
                       SeasonalController::getSeasonName(currentSeason_),
                       surfaceMap_.isLoaded() ? "loaded" : "off");
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

    // Raycast UPWARD from below to find the ground surface from underneath
    // This avoids hitting roofs, ceilings, and other overhead geometry
    // Start from well below ground level and cast upward
    irr::core::vector3df start(x, -1000.0f, z);
    irr::core::vector3df end(x, 1000.0f, z);
    irr::core::vector3df hitPoint;
    irr::core::triangle3df hitTriangle;
    irr::scene::ISceneNode* outNode = nullptr;

    irr::core::line3df ray(start, end);
    if (collisionManager_->getCollisionPoint(ray, zoneSelector_, hitPoint, hitTriangle, outNode)) {
        outY = hitPoint.Y;
        // For upward raycast, the normal should point down for ground surfaces
        // Flip it so it points up (away from ground)
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

    // Check for water/lava first (these should be excluded entirely)
    if (name.find("water") != std::string::npos ||
        name.find("lava") != std::string::npos ||
        name.find("lavaf") != std::string::npos ||
        name.find("falls") != std::string::npos ||
        name.find("fount") != std::string::npos) {
        return logAndReturn(SurfaceType::Water, "water/lava");
    }

    // Grass surfaces - check early so grass textures aren't caught by other patterns
    if (name.find("grass") != std::string::npos ||
        name.find("gras") != std::string::npos ||
        name.find("lawn") != std::string::npos ||
        name.find("turf") != std::string::npos) {
        return logAndReturn(SurfaceType::Grass, "grass");
    }

    // Stone/rock surfaces - including EQ naming patterns
    if (name.find("stone") != std::string::npos ||
        name.find("rock") != std::string::npos ||
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
        name.find("ceil") != std::string::npos ||     // Ceiling = indoor
        name.find("carpet") != std::string::npos) {   // Carpet = indoor
        return logAndReturn(SurfaceType::Stone, "stone/rock/floor/tile");
    }

    // Brick surfaces
    if (name.find("brick") != std::string::npos ||
        name.find("city") != std::string::npos ||     // citywall = stone/brick
        name.find("cyw") != std::string::npos ||      // cyw = city wall prefix (cywleav, etc.)
        name.find("leav") != std::string::npos ||     // eaves/leaves on buildings
        name.find("eave") != std::string::npos) {     // roof eaves
        return logAndReturn(SurfaceType::Brick, "brick/city");
    }

    // Wall surfaces - treat as brick/stone (indoor)
    if (name.find("wall") != std::string::npos ||
        name.find("waal") != std::string::npos ||     // EQ typo
        name.find("wail") != std::string::npos ||     // EQ typo
        name.find("wafl") != std::string::npos) {     // EQ abbreviation
        return logAndReturn(SurfaceType::Brick, "wall");
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

    // Snow surfaces
    if (name.find("snow") != std::string::npos ||
        name.find("ice") != std::string::npos ||
        name.find("frost") != std::string::npos) {
        return logAndReturn(SurfaceType::Snow, "snow");
    }

    // Dirt surfaces
    if (name.find("dirt") != std::string::npos ||
        name.find("drt") != std::string::npos ||       // EQ abbreviation for dirt
        name.find("xdrt") != std::string::npos ||      // exterior dirt pattern
        name.find("mud") != std::string::npos ||
        name.find("path") != std::string::npos ||
        name.find("road") != std::string::npos) {
        return logAndReturn(SurfaceType::Dirt, "dirt");
    }

    // Roof textures - no vegetation
    if (name.find("roof") != std::string::npos) {
        return logAndReturn(SurfaceType::Stone, "roof");
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

    return config;
}

} // namespace Detail
} // namespace Graphics
} // namespace EQT
