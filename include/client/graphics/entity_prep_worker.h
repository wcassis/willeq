#ifndef EQT_GRAPHICS_ENTITY_PREP_WORKER_H
#define EQT_GRAPHICS_ENTITY_PREP_WORKER_H

#include "client/graphics/entity_renderer.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>

namespace EQT {
namespace Graphics {

// Forward declarations
class RaceModelLoader;
class EquipmentModelLoader;

// Background worker thread that handles CPU-heavy entity model preparation
// (S3D archive load, WLD parse, animation merge, variant texture decode,
// equipment S3D extraction + texture decode) off the main thread.
// Main thread only does the fast GL upload (~2ms/texture) after prep completes.
class EntityPrepWorker {
public:
    struct PrepRequest {
        uint16_t spawnId;
        uint16_t raceId;
        uint8_t gender;
        EntityAppearance appearance;
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

    // Queue a prep request (thread-safe, called from main thread)
    void requestPrep(const PrepRequest& req);

    // Check for completed results (thread-safe, called from main thread)
    // Returns true if a result was available
    bool pollResult(PrepResult& out);

    // Check if a specific entity (by spawnId) is being prepped or queued
    bool isPendingForEntity(uint16_t spawnId) const;

    // Check if a specific race/gender is being prepped or queued (legacy)
    bool isPending(uint16_t raceId, uint8_t gender) const;

    // Get number of pending requests (queued + in-progress)
    size_t getPendingCount() const;

private:
    void workerLoop();

    // Decode variant textures for an entity's appearance (body-part overrides)
    void prepVariantTextures(const PrepRequest& req, PrepResult& result);

    // Extract and decode equipment model data for an entity
    void prepEquipmentModels(const PrepRequest& req, PrepResult& result);

    RaceModelLoader* modelLoader_;
    EquipmentModelLoader* equipLoader_;
    std::thread worker_;
    std::atomic<bool> running_{false};

    mutable std::mutex queueMutex_;
    std::condition_variable cv_;
    std::deque<PrepRequest> requestQueue_;

    mutable std::mutex resultMutex_;
    std::deque<PrepResult> resultQueue_;

    // Track what's currently being prepped (for isPending)
    mutable std::mutex activeMutex_;
    uint32_t activeKey_ = 0;  // (raceId << 8) | gender, 0 = none
    uint16_t activeSpawnId_ = 0;  // Currently active spawn ID

    // Track queued/active spawnIds for isPendingForEntity
    std::set<uint16_t> pendingSpawnIds_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ENTITY_PREP_WORKER_H
