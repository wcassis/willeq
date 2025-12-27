/*
 * WillEQ Spell Visual Effects System
 *
 * Manages visual effects for spell casting, impacts, and persistent auras.
 * Uses Irrlicht particle systems and billboards for spell visualization.
 */

#ifndef EQT_GRAPHICS_SPELL_VISUAL_FX_H
#define EQT_GRAPHICS_SPELL_VISUAL_FX_H

#include <irrlicht.h>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include <utility>
#include <string>

namespace EQ {

// Forward declarations
class SpellDatabase;
struct SpellData;

// Visual effect types
enum class SpellFXType : uint8_t {
    None = 0,
    CastGlow,           // Glow around caster during casting
    ProjectileTravel,   // Bolt traveling to target
    ImpactBurst,        // Explosion/flash on impact
    AuraPersistent,     // Ongoing aura around entity
    RainEffect,         // Rain spell particles (AE)
    BeamConnect,        // Beam connecting caster to target
    GroundCircle,       // AE indicator on ground
};

// Individual visual effect instance
struct SpellFXInstance {
    SpellFXType type = SpellFXType::None;
    uint32_t spell_id = 0;
    uint16_t source_entity = 0;
    uint16_t target_entity = 0;
    irr::core::vector3df source_pos;
    irr::core::vector3df target_pos;
    irr::video::SColor color{255, 255, 255, 255};
    float lifetime = 0;         // Total duration in seconds (0 = permanent until removed)
    float elapsed = 0;          // Time elapsed in seconds
    float scale = 1.0f;
    bool active = true;

    // Scene nodes (may be nullptr if not applicable)
    irr::scene::ISceneNode* scene_node = nullptr;
    irr::scene::IBillboardSceneNode* billboard = nullptr;
    irr::scene::IParticleSystemSceneNode* particle_system = nullptr;
};

// Callback for entity position lookup
using EntityPositionCallback = std::function<bool(uint16_t entity_id,
    irr::core::vector3df& out_pos)>;

class SpellVisualFX {
public:
    SpellVisualFX(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                   const std::string& eqClientPath = "");
    ~SpellVisualFX();

    // Set spell database for spell info lookup
    void setSpellDatabase(SpellDatabase* spell_db) { m_spell_db = spell_db; }

    // Set callback for getting entity positions
    void setEntityPositionCallback(EntityPositionCallback callback) {
        m_entity_pos_callback = callback;
    }

    // Update all active effects (call each frame)
    // delta_time: seconds since last update
    void update(float delta_time);

    // ========================================================================
    // Effect Creation
    // ========================================================================

    // Create casting glow around caster
    // Returns effect ID for later removal
    uint32_t createCastGlow(uint16_t caster_id, uint32_t spell_id, uint32_t duration_ms);

    // Create projectile traveling from caster to target
    void createProjectile(uint16_t caster_id, uint16_t target_id, uint32_t spell_id);

    // Create impact effect at target location
    void createImpact(uint16_t target_id, uint32_t spell_id);

    // Create impact at specific position (for AE spells)
    void createImpactAtPosition(const irr::core::vector3df& pos, uint32_t spell_id);

    // Create spell completion effect (burst of particles when cast succeeds)
    void createSpellComplete(uint16_t caster_id, uint32_t spell_id);

    // Create persistent buff aura around entity
    uint32_t createBuffAura(uint16_t entity_id, uint32_t spell_id);

    // Create rain effect for AE spells
    void createRainEffect(const irr::core::vector3df& center, float radius,
                          uint32_t spell_id, float duration);

    // Create ground circle for targeted AE
    void createGroundCircle(const irr::core::vector3df& center, float radius,
                           uint32_t spell_id, float duration);

    // ========================================================================
    // Effect Removal
    // ========================================================================

    // Remove casting glow (when cast ends)
    void removeCastGlow(uint16_t caster_id);

    // Remove buff aura (when buff fades)
    void removeBuffAura(uint16_t entity_id, uint32_t spell_id);

    // Remove all effects for an entity (when entity despawns)
    void removeAllForEntity(uint16_t entity_id);

    // Clear all effects
    void clearAllEffects();

    // ========================================================================
    // Queries
    // ========================================================================

    bool hasCastGlow(uint16_t caster_id) const;
    bool hasBuffAura(uint16_t entity_id, uint32_t spell_id) const;
    size_t getActiveEffectCount() const { return m_effects.size(); }

    // ========================================================================
    // Particle Settings
    // ========================================================================

    // Get/set particle density multiplier (0.0 = off, 1.0 = normal, 2.0 = double)
    float getParticleMultiplier() const { return m_particle_multiplier; }
    void setParticleMultiplier(float mult) { m_particle_multiplier = std::max(0.0f, std::min(mult, 3.0f)); }

    // Adjust particle multiplier by delta (for hotkey control)
    void adjustParticleMultiplier(float delta);

private:
    // Get spell color based on resist type/school
    irr::video::SColor getSpellColor(uint32_t spell_id) const;
    irr::video::SColor getSpellColor(const SpellData* spell) const;

    // Remove and clean up an effect
    void removeEffect(size_t index);
    void cleanupEffectNode(SpellFXInstance& effect);

    // Create scene nodes for different effect types
    void createGlowNode(SpellFXInstance& effect);
    void createProjectileNode(SpellFXInstance& effect);
    void createImpactNode(SpellFXInstance& effect);
    void createAuraNode(SpellFXInstance& effect);
    void createRainNode(SpellFXInstance& effect);
    void createGroundCircleNode(SpellFXInstance& effect);

    // Update individual effect types
    void updateProjectile(SpellFXInstance& effect, float delta_time,
                          std::vector<std::pair<uint16_t, uint32_t>>& pendingImpacts);
    void updateGlow(SpellFXInstance& effect, float delta_time);
    void updateAura(SpellFXInstance& effect, float delta_time);

    irr::scene::ISceneManager* m_smgr;
    irr::video::IVideoDriver* m_driver;
    SpellDatabase* m_spell_db = nullptr;
    EntityPositionCallback m_entity_pos_callback;

    std::vector<SpellFXInstance> m_effects;
    uint32_t m_next_effect_id = 1;

    // Particle density multiplier (0.0 = off, 1.0 = normal, up to 3.0)
    float m_particle_multiplier = 1.0f;

    // Particle textures by resist type
    irr::video::ITexture* m_particle_texture = nullptr;  // Default particle
    irr::video::ITexture* m_fire_texture = nullptr;      // Fire spells
    irr::video::ITexture* m_frost_texture = nullptr;     // Cold spells
    irr::video::ITexture* m_magic_texture = nullptr;     // Magic spells
    irr::video::ITexture* m_poison_texture = nullptr;    // Poison spells
    irr::video::ITexture* m_disease_texture = nullptr;   // Disease spells
    irr::video::ITexture* m_chromatic_texture = nullptr; // Chromatic/Prismatic spells
    irr::video::ITexture* m_corruption_texture = nullptr;// Corruption/darkness spells
    std::string m_eq_client_path;

    void loadParticleTextures();
    void createFallbackTexture();
    irr::video::ITexture* loadDDSTexture(const std::string& path, const std::string& name);
    irr::video::ITexture* getTextureForSpell(uint32_t spell_id) const;

    // Projectile speed in units per second
    static constexpr float PROJECTILE_SPEED = 500.0f;

    // Default effect durations
    static constexpr float DEFAULT_IMPACT_DURATION = 1.5f;  // Impact particles last 1.5 seconds
    static constexpr float DEFAULT_PROJECTILE_DURATION = 2.0f;
};

} // namespace EQ

#endif // EQT_GRAPHICS_SPELL_VISUAL_FX_H
