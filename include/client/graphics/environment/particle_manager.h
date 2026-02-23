#pragma once

#include "particle_types.h"
#include "particle_emitter.h"
#include "unified_particle.h"
#include "spell_particle_types.h"
#include <irrlicht.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>

namespace EQT {
namespace Graphics {
namespace Detail {
    class SurfaceMap;  // Forward declaration
}
namespace Environment {

class UnifiedParticleRenderer;  // Forward declaration

/**
 * ParticleManager - Central manager for all environmental particle effects.
 *
 * Responsibilities:
 * - Manages all particle emitters
 * - Handles zone transitions and biome selection
 * - Renders all particles using billboards
 * - Enforces particle budget limits
 * - Responds to quality settings changes
 */
class ParticleManager {
public:
    ParticleManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver);
    ~ParticleManager();

    // Non-copyable
    ParticleManager(const ParticleManager&) = delete;
    ParticleManager& operator=(const ParticleManager&) = delete;

    /**
     * Initialize the particle system.
     * Call after renderer is fully initialized.
     * @param eqClientPath Path to EQ client for loading textures
     * @return true if initialized successfully
     */
    bool init(const std::string& eqClientPath);

    /**
     * Update all particles.
     * @param deltaTime Time since last update (seconds)
     */
    void update(float deltaTime);

    /**
     * Render all particles.
     * Call during the main render pass.
     */
    void render();

    // === Zone Transitions ===

    /**
     * Called when entering a new zone.
     * Sets up appropriate emitters based on zone biome.
     */
    void onZoneEnter(const std::string& zoneName, ZoneBiome biome);

    /**
     * Called when leaving a zone.
     * Clears all particles and resets emitters.
     */
    void onZoneLeave();

    // === Settings ===

    /**
     * Set the overall quality level.
     * Affects particle budget and visual complexity.
     */
    void setQuality(EffectQuality quality);
    EffectQuality getQuality() const { return quality_; }

    /**
     * Set the density multiplier (0-1).
     * Stacks with quality setting.
     */
    void setDensity(float density);
    float getDensity() const { return userDensity_; }

    /**
     * Enable or disable a specific particle type.
     */
    void setTypeEnabled(ParticleType type, bool enabled);
    bool isTypeEnabled(ParticleType type) const;

    /**
     * Enable or disable the entire particle system.
     */
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }

    // === Environment State ===

    /**
     * Update time of day (0-24).
     */
    void setTimeOfDay(float hour);
    float getTimeOfDay() const { return envState_.timeOfDay; }

    /**
     * Update current weather.
     */
    void setWeather(WeatherType weather);
    WeatherType getWeather() const { return envState_.weather; }

    /**
     * Update wind parameters.
     */
    void setWind(const glm::vec3& direction, float strength);
    glm::vec3 getWindDirection() const { return envState_.windDirection; }
    float getWindStrength() const { return envState_.windStrength; }

    /**
     * Update player position for spawning particles around player.
     */
    void setPlayerPosition(const glm::vec3& pos, float heading);

    // === Statistics ===

    /**
     * Get total number of active particles across all emitters.
     */
    int getTotalActiveParticles() const;

    /**
     * Get current particle budget (max particles).
     */
    int getParticleBudget() const { return budget_.maxTotal; }

    /**
     * Get current biome.
     */
    ZoneBiome getCurrentBiome() const { return currentBiome_; }

    /**
     * Get debug info string for HUD display.
     */
    std::string getDebugInfo() const;

    /**
     * Reload settings from config file for all emitters.
     * Call after editing config/environment_effects.json.
     */
    void reloadSettings();

    /**
     * Set fire source positions for ember/smoke emitters.
     * Positions are in EQ coordinates (Z-up).
     */
    void setFireSources(const std::vector<glm::vec3>& positions);

    /**
     * Set the surface map for terrain detection.
     * Called by renderer after loading zone surface data.
     * Propagates to all emitters that need terrain data.
     */
    void setSurfaceMap(const Detail::SurfaceMap* surfaceMap);

    /**
     * Register an external emitter for rendering.
     * The emitter is not owned by ParticleManager.
     * Call unregisterExternalEmitter before destroying the emitter.
     */
    void registerExternalEmitter(ParticleEmitter* emitter);

    /**
     * Unregister an external emitter.
     */
    void unregisterExternalEmitter(ParticleEmitter* emitter);

    /**
     * Get the current environment state (for external emitters to use).
     */
    const EnvironmentState& getEnvironmentState() const { return envState_; }

    /**
     * Get the particle atlas texture (for water ripples and other effects).
     */
    irr::video::ITexture* getAtlasTexture() const { return atlasTexture_; }

    // === Unified Particle System (GLES2 point sprites) ===

    /**
     * Initialize the unified particle renderer (GLES2 only).
     * Call after GLES2 context is ready.
     * @param poolSize Particle pool size (0 = read from SpellEffectsConfig)
     */
    bool initUnifiedRenderer(int poolSize = 0);

    /**
     * Update unified particles (spawn, motion, death).
     * Call every frame for smooth fire/weather motion.
     * @param deltaTime Time since last update (seconds)
     * @param cameraPos Camera position in Irrlicht Y-up coordinates (for weather spawning)
     */
    void updateUnified(float deltaTime, const glm::vec3& cameraPos = glm::vec3(0));

    /**
     * Render unified particles via point sprites.
     * Call after the existing billboard render pass.
     */
    void renderUnified(const irr::core::matrix4& viewMatrix,
                       const irr::core::matrix4& projMatrix,
                       const irr::core::vector3df& cameraPos,
                       float fogStart, float fogEnd, const float* fogColor,
                       float screenHeight);

    /**
     * Create fire emitters at light source positions.
     * Positions are in EQ coordinates (Z-up); converted internally to Irrlicht Y-up.
     * lightRadii: per-source light radius for classifying torch vs campfire.
     */
    void createFireEmitters(const std::vector<glm::vec3>& positions,
                            const std::vector<float>& lightRadii);

    /**
     * Clear all unified emitters and their particles.
     */
    void clearUnifiedEmitters();

    /**
     * Toggle unified fire particles on/off.
     */
    void toggleUnifiedFire() { unifiedFireEnabled_ = !unifiedFireEnabled_; }
    bool isUnifiedFireEnabled() const { return unifiedFireEnabled_; }

    /**
     * Get count of alive unified particles.
     */
    int getUnifiedActiveCount() const { return unifiedActiveCount_; }

    // === Weather Particles ===

    /**
     * Activate weather particles (rain or snow).
     * @param type 1=rain, 2=snow
     * @param intensity 1-10
     */
    void activateWeatherParticles(uint8_t type, uint8_t intensity);

    /**
     * Deactivate weather particles (clear weather).
     */
    void deactivateWeatherParticles();

    /**
     * Check if weather particles are currently active.
     */
    bool isWeatherParticlesActive() const { return weatherEmitterID_ != 0; }

    /**
     * Set ambient color for tinting weather particles.
     */
    void setAmbientColor(const glm::vec3& color) { ambientColor_ = color; }

    // === Spell Effects ===

    /**
     * Entity position callback (returns Irrlicht Y-up coords).
     */
    using EntityPosCallback = std::function<bool(uint16_t entity_id, glm::vec3& out_pos)>;
    void setEntityPositionCallback(EntityPosCallback cb) { entityPosCallback_ = std::move(cb); }

    /**
     * Entity direction callback for spray effects (returns normalized direction in Irrlicht Y-up).
     */
    using EntityDirCallback = std::function<bool(uint16_t entity_id, glm::vec3& out_dir)>;
    void setEntityDirectionCallback(EntityDirCallback cb) { entityDirCallback_ = std::move(cb); }

    /**
     * Create a spell particle effect.
     * @param useDynamicDir If true, emitters use direction callback for spray velocity
     * @return Effect ID for later removal
     */
    uint32_t createSpellEffect(const SpellEffectDef& def,
                               uint16_t casterID, uint16_t targetID,
                               float duration = 0.0f,
                               bool useDynamicDir = false,
                               float projectileTravelDuration = 0.0f);

    /**
     * Create a spell particle effect at a fixed world position (no entity attachment).
     * Used for PBAE impacts, rain, ground circles at world coordinates.
     * @return Effect ID for later removal
     */
    uint32_t createSpellEffectAtPosition(const SpellEffectDef& def,
                                          const glm::vec3& worldPos,
                                          float duration = 0.0f);

    /**
     * Remove a specific spell effect by ID.
     */
    void removeSpellEffect(uint32_t effectID);

    /**
     * Remove all spell effects attached to an entity (caster or target).
     */
    void removeSpellEffectsForEntity(uint16_t entityID);

    /**
     * Clear all active spell effects.
     */
    void clearAllSpellEffects();

    /**
     * Light source for weather particle illumination.
     * Positions in Irrlicht Y-up coordinates.
     */
    struct ParticleLight {
        glm::vec3 position;
        float radius;
        glm::vec3 color;
    };

    /**
     * Set nearby light sources for per-particle weather illumination.
     * Call each frame from renderer with lights near camera.
     * Weather particles are only visible where illuminated by these lights.
     */
    void setWeatherLights(const std::vector<ParticleLight>& lights) { weatherLights_ = lights; }

private:
    /**
     * Create emitters appropriate for the given biome.
     */
    void setupEmittersForBiome(ZoneBiome biome);

    /**
     * Clear all emitters.
     */
    void clearEmitters();

    /**
     * Load the particle texture atlas.
     */
    bool loadParticleAtlas(const std::string& path);

    /**
     * Render a single billboard-oriented quad.
     */
    void renderBillboard(const Particle& p, const irr::core::vector3df& cameraPos,
                         const irr::core::vector3df& cameraUp);

    /**
     * Get UV coordinates for a tile in the atlas.
     */
    void getAtlasUVs(uint8_t tileIndex, float& u0, float& v0, float& u1, float& v1) const;

    // Irrlicht components
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::video::ITexture* atlasTexture_ = nullptr;
    irr::video::SMaterial particleMaterial_;

    // Emitters
    std::vector<std::unique_ptr<ParticleEmitter>> emitters_;

    // State
    bool enabled_ = true;
    bool initialized_ = false;
    EffectQuality quality_ = EffectQuality::Medium;
    float userDensity_ = 1.0f;
    ParticleBudget budget_;
    ZoneBiome currentBiome_ = ZoneBiome::Unknown;
    std::string currentZoneName_;

    // Type enable flags
    bool typeEnabled_[static_cast<size_t>(ParticleType::Count)];

    // Environment state
    EnvironmentState envState_;

    // Surface map for terrain detection (owned externally, e.g., by DetailObjectManager)
    const Detail::SurfaceMap* surfaceMap_ = nullptr;

    // External emitters (not owned, just rendered)
    std::vector<ParticleEmitter*> externalEmitters_;

    // === Unified particle system (GLES2 point sprites) ===

    std::vector<UnifiedParticle> unifiedPool_;       // Fixed pool of 1024
    std::vector<uint16_t> freeList_;                  // Free indices for allocation
    int unifiedActiveCount_ = 0;
    uint16_t nextEmitterID_ = 1;

#ifdef EQT_HAS_GLES2
    std::unique_ptr<UnifiedParticleRenderer> unifiedRenderer_;
#endif

    std::unordered_map<uint16_t, ActiveEmitter> unifiedEmitters_;
    bool unifiedFireEnabled_ = true;
    bool unifiedRendererInitialized_ = false;

    // Weather particle state
    uint16_t weatherEmitterID_ = 0;    // Active weather emitter ID (0 = none)
    glm::vec3 ambientColor_{1.0f};     // Ambient tint for weather particles
    std::vector<ParticleLight> weatherLights_;  // Nearby lights for per-particle illumination

    // Spell effect state
    EntityPosCallback entityPosCallback_;
    EntityDirCallback entityDirCallback_;
    std::vector<SpellEffectInstance> activeSpellEffects_;
    uint32_t nextSpellEffectID_ = 1;

    // Temp buffer for collecting alive particles for rendering
    std::vector<UnifiedParticle> unifiedRenderBuf_;

    // Allocate a particle from the free list, returns index or -1 if full
    int allocateUnifiedParticle();

    // Return a particle to the free list
    void freeUnifiedParticle(int index);

    // Spawn a single weather particle within the camera-relative volume
    void spawnWeatherParticle(const EmitterConfig& cfg, uint16_t emitterID,
                              const glm::vec3& cameraPos, float transitionAlpha);

    // Spawn a single particle for spell effects (BURST/RADIAL_EXPAND/ORBITAL)
    // dynamicDir: if non-null, used as spray direction (overrides velocityBase)
    void spawnSpellParticle(const EmitterConfig& cfg, uint16_t emitterID,
                            const glm::vec3& emitterPos,
                            const glm::vec3* dynamicDir = nullptr);

    // Update spell effect lifecycle (triggers, entity tracking, cleanup)
    void updateSpellEffects(float deltaTime);
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
