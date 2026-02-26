#pragma once

#include "client/graphics/simulation_worker.h"
#include <irrlicht.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace EQT {
namespace Graphics {

namespace Environment {

/**
 * Configuration for tumbleweeds loaded from JSON.
 */
struct TumbleweedSettings {
    bool enabled = true;
    int maxActive = 10;
    float spawnRate = 0.1f;
    float spawnDistance = 80.0f;
    float despawnDistance = 120.0f;
    float minSpeed = 2.0f;
    float maxSpeed = 8.0f;
    float windInfluence = 1.5f;
    float bounceDecay = 0.6f;
    float maxLifetime = 60.0f;
    float groundOffset = 0.3f;
    float sizeMin = 0.6f;
    float sizeMax = 1.4f;
    int maxBounces = 20;
};

/**
 * A single tumbleweed instance (scene node pool entry).
 */
struct TumbleweedInstance {
    bool active = false;
    int poolIndex = -1;
    irr::scene::IMeshSceneNode* node = nullptr;
};

/**
 * Bounding box for placeable objects.
 */
struct PlaceableBounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

/**
 * TumbleweedManager - Thin facade for rolling tumbleweed system.
 *
 * Physics simulation runs on SimulationWorker background thread.
 * This class manages:
 * - Scene node pool (create/show/hide mesh nodes)
 * - Command queue (settings → worker)
 * - Applying worker position/rotation results to nodes
 */
class TumbleweedManager {
public:
    TumbleweedManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver);
    ~TumbleweedManager();

    bool init();
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_ && settings_.enabled; }
    int getActiveCount() const { return cachedActiveCount_; }
    std::string getDebugInfo() const;
    void reloadSettings();

    // === Zone Transitions (queue commands for worker) ===

    void onZoneEnter(const std::string& zoneName, ZoneBiome biome);
    void onZoneLeave();

    // === Worker Integration ===

    std::vector<TumbleweedCommandData> drainCommands();
    void applyWorkerResults(const SimulationOutput::TumbleweedOutput& results);

private:
    irr::scene::IMesh* createTumbleweedMesh();

    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;

    // Scene node pool
    std::vector<TumbleweedInstance> pool_;
    irr::scene::IMesh* tumbleweedMesh_ = nullptr;
    irr::video::ITexture* tumbleweedTexture_ = nullptr;

    // State
    TumbleweedSettings settings_;
    bool enabled_ = true;
    bool initialized_ = false;
    int cachedActiveCount_ = 0;

    // Command queue
    std::vector<TumbleweedCommandData> pendingCommands_;

    // Map from worker poolIndex → pool slot
    TumbleweedInstance* findOrCreatePoolSlot(int poolIndex);
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
