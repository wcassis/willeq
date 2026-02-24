#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/eq/race_model_loader.h"
#include "common/logging.h"
#include <chrono>

namespace EQT {
namespace Graphics {

EntityPrepWorker::EntityPrepWorker(RaceModelLoader* modelLoader)
    : modelLoader_(modelLoader) {
}

EntityPrepWorker::~EntityPrepWorker() {
    stop();
}

void EntityPrepWorker::start() {
    if (running_.load()) return;
    running_.store(true);
    worker_ = std::thread(&EntityPrepWorker::workerLoop, this);
    LOG_INFO(MOD_GRAPHICS, "EntityPrepWorker: background thread started");
}

void EntityPrepWorker::stop() {
    if (!running_.load()) return;
    running_.store(false);
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    LOG_INFO(MOD_GRAPHICS, "EntityPrepWorker: background thread stopped");
}

void EntityPrepWorker::requestPrep(const PrepRequest& req) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        // Check for duplicate: don't re-queue same race/gender
        for (const auto& existing : requestQueue_) {
            if (existing.raceId == req.raceId && existing.gender == req.gender) {
                return;  // Already queued
            }
        }
        requestQueue_.push_back(req);
    }
    cv_.notify_one();
    LOG_DEBUG(MOD_GRAPHICS, "EntityPrepWorker: queued prep for spawn={} race={} gender={}",
              req.spawnId, req.raceId, req.gender);
}

bool EntityPrepWorker::pollResult(PrepResult& out) {
    std::lock_guard<std::mutex> lock(resultMutex_);
    if (resultQueue_.empty()) return false;
    out = resultQueue_.front();
    resultQueue_.pop_front();
    return true;
}

bool EntityPrepWorker::isPending(uint16_t raceId, uint8_t gender) const {
    // Check active work
    {
        std::lock_guard<std::mutex> lock(activeMutex_);
        uint32_t key = (static_cast<uint32_t>(raceId) << 8) | gender;
        if (activeKey_ == key) return true;
    }
    // Check queue
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        for (const auto& req : requestQueue_) {
            if (req.raceId == raceId && req.gender == gender) return true;
        }
    }
    return false;
}

size_t EntityPrepWorker::getPendingCount() const {
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        count = requestQueue_.size();
    }
    {
        std::lock_guard<std::mutex> lock(activeMutex_);
        if (activeKey_ != 0) count++;
    }
    return count;
}

void EntityPrepWorker::workerLoop() {
    while (running_.load()) {
        PrepRequest req;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            cv_.wait(lock, [this] { return !requestQueue_.empty() || !running_.load(); });
            if (!running_.load()) break;
            if (requestQueue_.empty()) continue;
            req = requestQueue_.front();
            requestQueue_.pop_front();
        }

        // Mark as active
        uint32_t key = (static_cast<uint32_t>(req.raceId) << 8) | req.gender;
        {
            std::lock_guard<std::mutex> lock(activeMutex_);
            activeKey_ = key;
        }

        auto start = std::chrono::steady_clock::now();

        // Check if model data is already loaded (race model may have been loaded by another entity)
        bool alreadyCached = modelLoader_->isModelDataCached(key);

        bool success = false;
        if (alreadyCached) {
            success = true;
            LOG_DEBUG(MOD_GRAPHICS, "EntityPrepWorker: race={} gender={} already cached, skipping prep",
                      req.raceId, req.gender);
        } else {
            // CPU-heavy work (300-500ms on ARM) — runs OFF main thread
            // This does: S3D load → WLD parse → animation merge → cache in loadedModels_
            // Does NOT create Irrlicht textures, meshes, or scene nodes (no GL calls)
            success = modelLoader_->preloadModelData(req.raceId, req.gender);

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            LOG_INFO(MOD_GRAPHICS, "EntityPrepWorker: preload race={} gender={} took {}ms success={}",
                     req.raceId, req.gender, elapsed, success);
        }

        // Clear active marker
        {
            std::lock_guard<std::mutex> lock(activeMutex_);
            activeKey_ = 0;
        }

        // Push result
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            resultQueue_.push_back({req.spawnId, req.raceId, req.gender, req.appearance, success});
        }
    }
}

} // namespace Graphics
} // namespace EQT
