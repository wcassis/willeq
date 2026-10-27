#pragma once

#include "particle_types.h"
#include "particle_emitter.h"
#include "unified_particle.h"
#include "spell_particle_types.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/constrained_texture_cache.h"
#include <irrlicht.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
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
class ParticleManager : public TextureEvictionListener {
public:
    ParticleManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver);
    ~ParticleManager();

    // Non-copyable
    ParticleManager(const ParticleManager&) = delete;
    ParticleManager& operator=(const ParticleManager&) = delete;

    // Set constrained texture cache (mandatory, must be called before init).
    // P34: registers/deregisters `this` as a TextureEvictionListener (same
    // idiom as AnimatedTextureManager/SkyRenderer/StormCloudLayer/
    // DetailManager) so atlasTexture_ can never go dangling on eviction —
    // see onTextureEvicted()'s doc comment for the bug this closes.
    void setConstrainedTextureCache(ConstrainedTextureCache* cache);

    // TextureEvictionListener — see .cpp for the P34 root-cause writeup.
    void onTextureEvicted(const std::string& name, irr::video::ITexture* texture) override;

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

    /**
     * B995: the Options window's particle pages (particle_options.h): the
     * sim bridge hands them over every frame. Environment density scales the
     * ambient emitters; opacity / near clip are applied at draw by both this
     * manager's billboards and the unified renderer.
     */
    void setParticleKnobs(const ParticleKnobs& knobs);
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
    irr::video::ITexture* getSpellSpriteAtlasTexture() const {
        return spellSpriteAtlasTexture_;
    }

    // === Unified Particle System (GLES2 point sprites) ===

    /**
     * Initialize the unified particle renderer (GLES2 only).
     * Call after GLES2 context is ready.
     * @param poolSize Particle pool size (0 = read from SpellEffectsConfig)
     */
    bool initUnifiedRenderer(int poolSize = 0);

    // T426 test seam. initUnifiedRenderer() needs a live GL context, and
    // createSpellEffect() refuses to enqueue
    // without it -- so a headless test that drives the real SpellVisualFX ->
    // ParticleManager -> SimulationWorker chain (tests/test_caster_own_landing.cpp)
    // cannot get a single command out of the manager. Nothing in production
    // calls this; the renderer always goes through initUnifiedRenderer().
    void Test_SetUnifiedRendererInitializedForSeam(bool initialized) {
        unifiedRendererInitialized_ = initialized;
    }

    /**
     * Update unified particles — now a no-op (physics moved to SimulationWorker).
     * Kept for billboard emitter envState updates only.
     */
    void updateUnified(float deltaTime, const glm::vec3& cameraPos = glm::vec3(0));

    /**
     * Render unified particles via point sprites.
     * Uses external render buffer from SimulationWorker output.
     */
    void renderUnified(const irr::core::matrix4& viewMatrix,
                       const irr::core::matrix4& projMatrix,
                       const irr::core::vector3df& cameraPos,
                       float fogStart, float fogEnd, const float* fogColor,
                       float screenHeight,
                       const std::vector<UnifiedParticle>* externalRenderBuffer = nullptr);

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
     * Toggle unified fire particles on/off (enqueues command to worker).
     */
    void toggleUnifiedFire();
    bool isUnifiedFireEnabled() const { return unifiedFireEnabled_; }

    /**
     * Get count of alive unified particles (from last worker output).
     */
    int getUnifiedActiveCount() const { return lastWorkerActiveCount_; }

    /**
     * Set active count from worker output (called by renderer after applying results).
     */
    void setWorkerActiveCount(int count) { lastWorkerActiveCount_ = count; }

    /**
     * Drain pending commands for SimulationWorker (moves commands out).
     */
    std::vector<ParticleCommandData> drainCommands() {
        std::vector<ParticleCommandData> cmds;
        cmds.swap(pendingCommands_);
        return cmds;
    }

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
    bool isWeatherParticlesActive() const { return weatherActive_; }

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
     * Test if an entity position can be resolved via the callback.
     * Used by SpellVisualFX to probe for hand bone availability before creating effects.
     */
    bool resolveEntityPosition(uint16_t entityID, glm::vec3& outPos) const;

    /**
     * B1170 (#489): a pseudo-entity whose position the FX layer sets itself
     * (a spell missile object, refactor/B1170/README.md). resolveEntityPosition
     * answers an anchor's id from here before asking the callback.
     */
    void setFxAnchorPosition(uint16_t entityID, const glm::vec3& pos) { fxAnchors_[entityID] = pos; }
    void clearFxAnchor(uint16_t entityID) { fxAnchors_.erase(entityID); }

    /**
     * Entity direction callback for spray effects (returns normalized direction in Irrlicht Y-up).
     *
     * B293 (#124): invoked once per entity in the previous frame's
     * SimulationWorker particleOutput.directionRequestEntities, by the
     * renderer's direction-resolution arm beside the position one (see
     * resolveEntityDirection and SimBridgeComponent::applySimulationOutput).
     * useDynamicDirection emitters (spray casts) spawn along it.
     */
    using EntityDirCallback = std::function<bool(uint16_t entity_id, glm::vec3& out_dir)>;
    void setEntityDirectionCallback(EntityDirCallback cb) { entityDirCallback_ = std::move(cb); }
    bool resolveEntityDirection(uint16_t entityID, glm::vec3& outDir) const;

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

    // B1138: createSpellEffectAtPosition() was DELETED with its only callers
    // (SpellVisualFX::createImpactAtPosition/createRainEffect/createGroundCircle,
    // reached only from model_viewer's pre-B1131 heuristics). The worker's
    // ParticleCommand::CreateSpellEffectAtPos arm and SpellAttach::GROUND_TARGET
    // followed it in B1139.

    // T397 Leg B (board B220): removeSpellEffect(uint32_t) was DELETED here.
    // Zero callers tree-wide, nothing kept the id, and it wrote
    // preAssignedEffectID while the worker matched effectID, so it could
    // never have removed anything. Recover with:
    // git show 8382268d:include/client/graphics/environment/particle_manager.h

    /**
     * Remove all spell effects attached to an entity (caster or target).
     * The entity id lands in ParticleCommandData::entityID, which is the
     * field SimulationWorker's RemoveSpellEffectsEntity arm matches on.
     */
    void removeSpellEffectsForEntity(uint16_t entityID);

    /**
     * Remove only the named STYLES attached to an entity (T426, board B400).
     * `styles` are SpellEffectDef::name values -- "CastGlow", "CastSpray",
     * "Impact", "SpellComplete", "CastGlow", ... An empty list means every
     * style, i.e. the overload above.
     *
     * The reason this exists: RemoveSpellEffectsEntity matches an effect whose
     * CASTER or TARGET is the entity, and a self-target cast puts the caster's
     * spawn id in both, so an unfiltered "take the cast glow down" removed the
     * caster's own impact burst as well.
     */
    void removeSpellEffectsForEntity(uint16_t entityID,
                                     std::vector<std::string> styles);

    /**
     * B1122: RELEASE the named styles attached to an entity: their emitters
     * stop `seconds` from now and their live sprites finish their life,
     * where removeSpellEffectsForEntity() frees them at once.
     */
    void releaseSpellEffectsForEntity(uint16_t entityID,
                                      std::vector<std::string> styles, float seconds);

    /**
     * Clear all active spell effects.
     */
    void clearAllSpellEffects();

    /**
     * Set nearby light sources for per-particle weather illumination.
     * Call each frame from renderer with lights near camera.
     * Weather particles are only visible where illuminated by these lights.
     */
    void setWeatherLights(const std::vector<Graphics::ParticleLight>& lights) { weatherLights_ = lights; }

    /**
     * Get current weather lights (for particle input snapshotting).
     */
    const std::vector<Graphics::ParticleLight>& getWeatherLights() const { return weatherLights_; }

    /**
     * Get current ambient color (for particle input snapshotting).
     */
    const glm::vec3& getAmbientColor() const { return ambientColor_; }

    /**
     * Configure the material every particle billboard is drawn with.
     *
     * T400 (board B225): extracted from init() so the blend mode can be
     * PINNED by a test without standing up a device, an atlas and a texture
     * cache. It is ALPHA-BLEND, not additive:
     * COpenGLES2Driver::applyMaterialRenderState() maps
     * EMT_TRANSPARENT_ALPHA_CHANNEL to
     * glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) with depth writes
     * off, and selects SOLID3D_FS, which takes its alpha from
     * texture2D(uTexture, uv).a -- a SINGLE sampler. That is why the atlas
     * must carry its alpha IN-BAND (a 32-bit BMP on the raw path, an
     * RGBA8888 payload in the EQE1 pack) rather than in an ETC1 `_alpha`
     * companion, which only the alpha-TEST shader variants ever sample.
     * Straight (non-premultiplied) RGB is what this blend expects.
     */
    static void configureParticleMaterial(irr::video::SMaterial& material,
                                          irr::video::ITexture* atlasTexture);

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

    // T417 (board B300): the SECOND particle sheet -- the official client's
    // own spel*.tga art, packed by tools/spell_sprite_atlas_builder. Loaded
    // and re-acquired exactly like the procedural atlas above; a failure is
    // NOT fatal, because every procedural particle still draws without it
    // and only real-sprite spell effects are lost.
    bool loadSpellSpriteAtlas(const std::string& path);

    // Path passed to loadParticleAtlas() by init() — retained so onZoneEnter()
    // can re-acquire the atlas via the exact same path if a prior eviction
    // (onTextureEvicted()) nulled atlasTexture_ (P34).
    static constexpr const char* kAtlasTexturePath = "data/textures/particle_atlas.bmp";
    static constexpr const char* kSpellSpriteAtlasPath =
        "data/textures/spell_sprite_atlas.bmp";

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
    irr::video::ITexture* spellSpriteAtlasTexture_ = nullptr;  // T417
    ConstrainedTextureCache* constrainedCache_ = nullptr;
    irr::video::SMaterial particleMaterial_;

    // Emitters
    std::vector<std::unique_ptr<ParticleEmitter>> emitters_;

    // State
    bool enabled_ = true;
    bool initialized_ = false;
    EffectQuality quality_ = EffectQuality::Medium;
    float userDensity_ = 1.0f;
    ParticleKnobs knobs_ = identityParticleKnobs();  // B995
    float envDensity() const;
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
    // Pool/emitter/spell state owned by SimulationWorker.
    // ParticleManager is now a thin facade that enqueues commands.

#ifdef EQT_HAS_GLES2
    std::unique_ptr<UnifiedParticleRenderer> unifiedRenderer_;
#endif

    bool unifiedFireEnabled_ = true;
    bool unifiedRendererInitialized_ = false;
    bool weatherActive_ = false;        // Tracks local weather activation state
    int lastWorkerActiveCount_ = 0;     // From last worker output

    // Ambient color for weather tinting (snapshotted into particle input)
    glm::vec3 ambientColor_{1.0f};
    // Weather lights (snapshotted into particle input)
    std::vector<Graphics::ParticleLight> weatherLights_;

    // Entity callbacks (kept for resolveEntityPosition and renderer-side resolution)
    EntityPosCallback entityPosCallback_;
    EntityDirCallback entityDirCallback_;
    std::unordered_map<uint16_t, glm::vec3> fxAnchors_;  // B1170: missile objects

    // Command queue (main thread accumulates, worker drains via drainCommands())
    std::vector<ParticleCommandData> pendingCommands_;

    // Pre-assigned spell effect ID counter (main thread only, monotonically increasing)
    std::atomic<uint32_t> nextLocalSpellEffectID_{1};
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT
