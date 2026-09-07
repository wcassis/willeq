// detail_manager_chunks.cpp -- DetailManager's chunk residency: which detail
// chunks are loaded around the camera, loading one, unloading the distant
// ones, and rebuilding every loaded chunk's mesh.
//
// Split out of detail_manager.cpp by T453 (contiguous range 505..648 of the
// 1,219-line original, moved verbatim).
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

} // namespace Detail
} // namespace Graphics
} // namespace EQT
