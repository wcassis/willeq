#ifndef EQT_GRAPHICS_ENTITY_PREP_WORKER_H
#define EQT_GRAPHICS_ENTITY_PREP_WORKER_H

#include "client/graphics/background_work_queue.h"
#include "client/graphics/entity_renderer.h"
#include <deque>
#include <memory>
#include <set>

namespace EQT {
namespace Graphics {

// Forward declarations
class RaceModelLoader;
class EquipmentModelLoader;

// Background worker that handles CPU-heavy entity model preparation
// (S3D archive load, WLD parse, animation merge, variant texture decode,
// equipment S3D extraction + texture decode) off the main thread.
// Main thread only does the fast GL upload (~2ms/texture) after prep completes.
//
// Uses BackgroundWorkQueue internally for thread management. Main thread owns
// pendingQueue_ (no mutex needed). Main thread calls dispatchOne() on GREEN
// frames to submit ONE item to the queue, gated by isIdle(). This preserves
// the one-at-a-time dispatch contract so the worker doesn't hog the shared
// memory bus during non-GREEN frames.
class EntityPrepWorker {
public:
    struct PrepRequest {
        uint16_t spawnId;
        uint16_t raceId;
        uint8_t gender;
        EntityAppearance appearance;
        uint8_t pvsDepth = 255;          // PVS depth from depth map (0 = player region)
        size_t bspRegion = SIZE_MAX;     // Cached BSP region for re-prioritization
    };

    struct PrepResult {
        uint16_t spawnId;
        uint16_t raceId;
        uint8_t gender;
        EntityAppearance appearance;
        bool success;
        // Model data is cached inside RaceModelLoader after prep completes.
        // Main thread calls buildEntityMesh() which finds cached data
        // and only needs to do mesh build + texture upload + node creation.

        // Per-entity variant textures (decoded ARGB, ready for GPU upload)
        std::vector<DecodedTexture> variantTextures;

        // Per-entity equipment data (geometry + decoded textures)
        struct EquipmentPrepData {
            int modelId = 0;
            uint32_t equipmentId = 0;
            bool isPrimary = true;
            std::shared_ptr<ZoneGeometry> geometry;
            std::map<std::string, std::shared_ptr<TextureInfo>> rawTextures;
            std::vector<DecodedTexture> decodedTextures;
        };
        std::vector<EquipmentPrepData> equipmentData;
    };

    explicit EntityPrepWorker(RaceModelLoader* modelLoader, EquipmentModelLoader* equipLoader = nullptr);
    ~EntityPrepWorker();

    void start();
    void stop();

    // Add a prep request to the pending queue (main-thread-only, no mutex)
    void requestPrep(const PrepRequest& req);

    // Dispatch the front item of pendingQueue_ to the worker thread.
    // Only call from main thread when governor is GREEN and worker is idle.
    void dispatchOne();

    // Check for completed results (called from main thread)
    // Returns true if a result was available
    bool pollResult(PrepResult& out);

    // True when the worker has no active work item and is waiting for dispatch
    bool isIdle() const;

    // Remove a specific entity from the pending queue (despawn/left visibility)
    void cancelPrep(uint16_t spawnId);

    // Check if a specific entity (by spawnId) is being prepped, dispatched, or queued
    bool isPendingForEntity(uint16_t spawnId) const;

    // Check if a specific race/gender is being prepped, dispatched, or queued
    bool isPending(uint16_t raceId, uint8_t gender) const;

    // Get number of pending requests (queued + dispatched + in-progress)
    size_t getPendingCount() const;

    // Re-sort pending queue with updated PVS depths (called on region change)
    void updateDepths(std::function<uint8_t(size_t)> depthLookup);

private:
    // Stable-sort pendingQueue_ by (pvsDepth, raceKey) for priority + cache locality
    void sortPendingByPriority();

    // Process a single prep request (runs on worker thread)
    PrepResult processRequest(PrepRequest&& req);

    // Decode variant textures for an entity's appearance (body-part overrides)
    void prepVariantTextures(const PrepRequest& req, PrepResult& result);

    // Extract and decode equipment model data for an entity
    void prepEquipmentModels(const PrepRequest& req, PrepResult& result);

    RaceModelLoader* modelLoader_;
    EquipmentModelLoader* equipLoader_;

    std::unique_ptr<BackgroundWorkQueue<PrepRequest, PrepResult>> queue_;

    // --- Main-thread-only (no mutex needed) ---
    std::deque<PrepRequest> pendingQueue_;
    std::set<uint16_t> pendingSpawnIds_;

    // --- In-flight tracking (main-thread-only, set in dispatchOne, cleared in pollResult) ---
    uint16_t dispatchedSpawnId_ = 0;
    uint32_t dispatchedKey_ = 0;  // (raceId << 8) | gender, 0 = none
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ENTITY_PREP_WORKER_H
