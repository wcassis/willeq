#pragma once

#include "client/graphics/simulation_worker.h"
#include "boids_types.h"
#include "particle_types.h"  // For ZoneBiome
#include <irrlicht.h>
#include <vector>
#include <string>

namespace EQT {
namespace Graphics {

namespace Environment {

/**
 * BoidsManager - Thin facade for ambient creature flocking system.
 *
 * Simulation runs on SimulationWorker background thread.
 * This class handles:
 * - Command queue (settings changes → worker)
 * - Render buffer (worker results → billboard rendering)
 * - Texture atlas and material management
 */
class BoidsManager {
public:
    BoidsManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver);
    ~BoidsManager();

    // Non-copyable
    BoidsManager(const BoidsManager&) = delete;
    BoidsManager& operator=(const BoidsManager&) = delete;

    /**
     * Initialize the boids system (load textures).
     */
    bool init(const std::string& eqClientPath);

    /**
     * Render all creatures from cached worker results.
     */
    void render();

    // === Zone Transitions (queue commands for worker) ===

    void onZoneEnter(const std::string& zoneName, ZoneBiome biome);
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome,
                     const glm::vec3& boundsMin, const glm::vec3& boundsMax);
    void onZoneLeave();

    // === Settings (queue commands for worker) ===

    void setQuality(int quality);
    int getQuality() const { return quality_; }

    void setDensity(float density);
    float getDensity() const { return userDensity_; }

    void setTypeEnabled(CreatureType type, bool enabled);
    bool isTypeEnabled(CreatureType type) const;

    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }

    // === Worker Integration ===

    /**
     * Drain pending commands (swap pattern, called by renderer when building input).
     */
    std::vector<BoidsCommandData> drainCommands();

    /**
     * Apply worker results for rendering.
     */
    void applyWorkerResults(const SimulationOutput::BoidsOutput& results);

    // === Statistics ===

    int getTotalActiveCreatures() const { return cachedActiveCount_; }
    ZoneBiome getCurrentBiome() const { return currentBiome_; }
    std::string getDebugInfo() const;

    void reloadSettings();

private:
    bool loadCreatureAtlas(const std::string& path);
    void getAtlasUVs(uint8_t tileIndex, float& u0, float& v0, float& u1, float& v1) const;

    // Irrlicht components
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::video::ITexture* atlasTexture_ = nullptr;
    irr::video::SMaterial creatureMaterial_;

    // State
    bool enabled_ = true;
    bool initialized_ = false;
    int quality_ = 2;
    float userDensity_ = 1.0f;
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;

    // Type enable flags
    bool typeEnabled_[static_cast<size_t>(CreatureType::Count)];

    // Command queue (main thread → worker)
    std::vector<BoidsCommandData> pendingCommands_;

    // Render buffer (worker → main thread rendering)
    struct CachedCreature {
        glm::vec3 position;
        float size;
        uint8_t textureIndex;
        float alpha;
    };
    std::vector<CachedCreature> cachedCreatures_;
    int cachedActiveCount_ = 0;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
